#include "pch.h"

#include <gst/d3d11/gstd3d11.h>
#include <gst/d3d11/gstd3d11memory.h>
#include <gst/gstevent.h>
#include <filesystem>

#include <fstream>
#include <string>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

#include "GstWinRTPipeline.h"
#include "GstWinRTPipeline.g.cpp"

#pragma comment(lib, "mfuuid.lib")

namespace winrt::MediaFramework::implementation
{
    GstWinRTPipeline::GstWinRTPipeline(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& uiDispatcherQueue)
        : _renderState(GstWinRTState::Uninitialized), _recordState(GstWinRTState::Uninitialized), _videoWidth(0), _videoHeight(0), _uiDispatcherQueue(uiDispatcherQueue)
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        _renderClock = gst_system_clock_obtain();

        if (!_watchdogTimer)
        {
            _watchdogTimer = _uiDispatcherQueue.CreateTimer();
            _watchdogTimer.Interval(std::chrono::milliseconds(500));
            _watchdogTimer.Tick([this](auto const&, auto const&)
                {
                    if (_renderClock)
                    {
                        GstClockTime now = gst_clock_get_time(_renderClock);
                        GstClockTime delayTime = now - _lastRenderSampleTime;
                        GstClockTime sinceLastEvent = now - _lastBufferEventTime;
                        _renderDelayTimeMsec = delayTime / GST_MSECOND;

                        if (now > _lastRenderSampleTime + (1 * GST_SECOND) && sinceLastEvent > BufferEventInterval)
                        {
                            SetRenderState(GstWinRTState::RenderBuffering, L"", 0);

                            wchar_t buf[256];
                            swprintf_s(buf, L"[Watchdog] No new render sample for %llu ms", _renderDelayTimeMsec);
                            SetLogMessage(buf, 3);
                            _lastBufferEventTime = now;
                        }
                    }
                }
            );
        }

        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        CreateDXGIFactory1(IID_PPV_ARGS(&_factory));

        ComPtr<IDXGIAdapter1> adapter;
        _factory->EnumAdapterByGpuPreference(
            0,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter)
        );

        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        _adapterLuid = (uint64_t(desc.AdapterLuid.HighPart) << 32 | uint32_t(desc.AdapterLuid.LowPart));

        _adapterIndex = UINT_MAX;
        for (UINT i = 0; ; i++) {
            ComPtr<IDXGIAdapter1> enumAdapter;
            if (FAILED(_factory->EnumAdapters1(i, &enumAdapter))) {
                break;
            }
            DXGI_ADAPTER_DESC1 d;
            enumAdapter->GetDesc1(&d);
            if (d.AdapterLuid.HighPart == desc.AdapterLuid.HighPart &&
                d.AdapterLuid.LowPart == desc.AdapterLuid.LowPart) {
                _adapterIndex = i;
                break;
            }
        }

        wchar_t buf[128];
        swprintf_s(buf, L"Chosen Adapter: %s\n", desc.Description);
        SetLogMessage(buf, 3);

        D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            levels, _countof(levels),
            D3D11_SDK_VERSION,
            _d3dDevice.ReleaseAndGetAddressOf(), nullptr, _d3dContext.ReleaseAndGetAddressOf()
        );

        ComPtr<ID3D11Multithread> mt;
        if (SUCCEEDED(_d3dContext.As(&mt))) {
            mt->SetMultithreadProtected(TRUE);
        }
    }

    void GstWinRTPipeline::Initialize(MediaFramework::StreamerData config, winrt::Microsoft::UI::Xaml::UIElement const& host,
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& videoPanel)
    {
        SetLogMessage(L"GstWinRTPipeline::Initialize", 3);

        _config = config;

        CreateVideoLayer(host, videoPanel);

        SetRenderState(GstWinRTState::Initialized, L"", 0);
        SetRecordState(GstWinRTState::Initialized, L"", 0);

        return;
    }

    bool GstWinRTPipeline::CreateVideoLayer(winrt::Microsoft::UI::Xaml::UIElement const& host,
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& videoPanel)
    {
        using namespace winrt;
        using namespace winrt::Microsoft::UI::Composition;
        using namespace winrt::Microsoft::UI::Xaml::Hosting;
        using namespace winrt::Microsoft::UI::Xaml;

        Visual hostVisual = ElementCompositionPreview::GetElementVisual(host);
        auto actualSize = host.ActualSize();
        Compositor compositor = hostVisual.Compositor();
        winrt::Windows::Foundation::Size size{ actualSize.x, actualSize.y };

        _d3dRenderer = std::make_unique<StreamingFrameRenderer>(
            _uiDispatcherQueue,
            _d3dDevice,
            _d3dContext,
            _swapChain,
            _factory,
            videoPanel,
            [this](MediaFramework::GstEventArgs args) {
                this->RaiseGstEvent(std::move(args));
            }
        );
        _d3dRenderer->Initialize((UINT)size.Width, (UINT)size.Height);

        return true;
    }

    void GstWinRTPipeline::CreateAndLaunchRenderPipeline()
    {
        GstD3D11Device* gstD3DDevice = gst_d3d11_device_new_wrapped(_d3dDevice.Get());
        GstContext* gstCtx = gst_d3d11_context_new(gstD3DDevice);

        //_config.SrcVideoPort = 5000;

        std::ostringstream  renderPipelineDesc;
        renderPipelineDesc << "udpsrc name=videoSrc port=" << _config.SrcVideoPort << " "
            << "buffer-size=1048576 "
            << "caps=\"application/x-rtp,media=video,clock-rate=(int)90000,encoding-name=H264\" "
            << "! rtpjitterbuffer latency=100 drop-on-latency=true "
            << "! queue2 max-size-buffers=5 max-size-bytes=0 max-size-time=0 "
            << "! rtph264depay "
            << "! h264parse "
            << "! queue max-size-buffers=5 leaky=downstream "
            //<< "! decodebin name=autodecoder "
            << "! d3d11h264dec name=d3d11h264dec0 qos=true discard-corrupted-frames=true compliance=flexible "
            << "! tee name=recordSplitter ! queue " // set tee for record
            << "! video/x-raw(memory:D3D11Memory),format=NV12 "
            << "! videorate "
            << "! video/x-raw(memory:D3D11Memory),format=NV12,framerate=60/1 "
            << "! d3d11convert name=d3d11convert0 "
            << "! d3d11upload name=d3d11upload0 "
            << "! video/x-raw(memory:D3D11Memory),format=BGRA "
            //<< "! identity name=fpsProbe sync=false "
            << "! tee name=captureSplitter ! queue " // set tee for capture
            << "! appsink name=renderSink emit-signals=true sync=false async=false max-buffers=10 drop=true "
            // branch for video appsink recording
            << "recordSplitter. ! queue "
            << "! d3d11download name=d3d11download0 !video/x-raw,format=NV12 "
            << "! videorate "
            << "! video/x-raw,format=NV12,framerate=60/1 "
            << "! appsink name=recordAppSink emit-signals=true sync=false async=false max-buffers=1 drop=true "
            // branch for image capture
            << "captureSplitter. ! queue ! d3d11download name=d3d11download1 ! video/x-raw,format=BGRA "
            << "! appsink name=captureSink emit-signals=true sync=false async=false max-buffers=1 drop=true "
            ;

        GError* error = nullptr;
        _renderPipeline = gst_parse_launch(renderPipelineDesc.str().c_str(), &error);

        if (_renderPipeline == nullptr)
        {
            hstring msg = winrt::to_hstring(error->message);
            SetRenderState(GstWinRTState::Error, msg, error->code);
            g_error_free(error);
            return;
        }

        GstElement* fpsProbe = gst_bin_get_by_name(GST_BIN(_renderPipeline), "fpsProbe");
        if (fpsProbe)
        {
            GstPad* srcpad = gst_element_get_static_pad(fpsProbe, "src");
            if (srcpad) {
                gst_pad_add_probe(
                    srcpad,
                    GST_PAD_PROBE_TYPE_BUFFER,
                    reinterpret_cast<GstPadProbeCallback>(+[](GstPad* pad, GstPadProbeInfo* info, gpointer user_data) -> GstPadProbeReturn
                        {
                            static gint64 next_deadline_ns = 0;
                            static gint64 last_ns = 0;
                            gint64 now_ns = gst_util_get_timestamp();

                            if (last_ns != 0)
                            {
                                double interval_ms = (now_ns - last_ns) / 1000000.0;
                                double fps = 1000.0 / interval_ms;
                                printf("[FPS Probe] interval: %.2f ms, ~%.2f FPS\n", interval_ms, fps);
                            }
                            last_ns = now_ns;
                            return GST_PAD_PROBE_OK;
                        }),
                    this,
                    nullptr
                );

                gst_object_unref(srcpad);
            }

            gst_object_unref(fpsProbe);
        }

        _renderSink = gst_bin_get_by_name(GST_BIN(_renderPipeline), "renderSink");
        if (_renderSink == nullptr)
        {
            SetRenderState(GstWinRTState::Error, L"_renderSink is nullptr!!!", 0);
            return;
        }

        _captureSink = gst_bin_get_by_name(GST_BIN(_renderPipeline), "captureSink");
        if (_captureSink == nullptr)
        {
            SetRenderState(GstWinRTState::Error, L"_captureSink is nullptr!!!", 0);
            return;
        }

        _recordAppSink = gst_bin_get_by_name(GST_BIN(_renderPipeline), "recordAppSink");
        if (_recordAppSink == nullptr)
        {
            SetRenderState(GstWinRTState::Error, L"_recordAppSink is nullptr!!!", 0);
            return;
        }

        SetRenderState(GstWinRTState::PipelineCreated, L"", 0);

        GstBus* bus = gst_element_get_bus(_renderPipeline);
        if (bus != nullptr)
        {
            gst_bus_add_signal_watch(bus);
            g_signal_connect(bus, "message::state-changed", G_CALLBACK(OnRenderPipelineStateChanged), this);
            gst_object_unref(bus);
        }

        gst_element_set_context(GST_ELEMENT(_renderPipeline), gstCtx);

        GstElement* autoDec = gst_bin_get_by_name(GST_BIN(_renderPipeline), "autodecoder");
        if (autoDec)
        {
            g_signal_connect(
                autoDec,
                "element-added",
                G_CALLBACK(+[](GstElement* dbin, GstElement* element, gpointer)
                    {
                        const gchar* factory_name = gst_element_factory_get_longname(gst_element_get_factory(element));
                        printf("[decodebin] added element: %s\n", factory_name);
                    }),
                nullptr
            );
        }

        GstElement* convert = gst_bin_get_by_name(GST_BIN(_renderPipeline), "d3d11convert0");
        if (convert) {
            g_object_set(convert, "adapter", _adapterIndex, nullptr);
            gst_object_unref(convert);
        }

        GstElement* upload = gst_bin_get_by_name(GST_BIN(_renderPipeline), "d3d11upload0");
        if (upload) {
            g_object_set(upload, "adapter", _adapterIndex, nullptr);
            gst_object_unref(upload);
        }

        GstElement* download = gst_bin_get_by_name(GST_BIN(_renderPipeline), "d3d11download0");
        if (download) {
            g_object_set(download, "adapter", _adapterIndex, nullptr);
            gst_object_unref(download);
        }

        GstElement* download1 = gst_bin_get_by_name(GST_BIN(_renderPipeline), "d3d11download1");
        if (download1) {
            g_object_set(download1, "adapter", _adapterIndex, nullptr);
            gst_object_unref(download1);
        }

        GstElement* convert1 = gst_bin_get_by_name(GST_BIN(_renderPipeline), "d3d11convert1");
        if (convert1) {
            g_object_set(convert1, "adapter", _adapterIndex, nullptr);
            gst_object_unref(convert1);
        }

        GstElement* upload1 = gst_bin_get_by_name(GST_BIN(_renderPipeline), "d3d11upload1");
        if (upload1) {
            g_object_set(upload1, "adapter", _adapterIndex, nullptr);
            gst_object_unref(upload1);
        }

        gst_app_sink_set_emit_signals(GST_APP_SINK(_renderSink), TRUE);
        _renderSinkHandlerId = g_signal_connect(_renderSink, "new-sample", G_CALLBACK(OnNewSample), this);

        gst_app_sink_set_emit_signals(GST_APP_SINK(_captureSink), TRUE);
        _captureSinkHandlerId = g_signal_connect(_captureSink, "new-sample", G_CALLBACK(OnCaptureSample), this);

        gst_app_sink_set_emit_signals(GST_APP_SINK(_recordAppSink), TRUE);
        _recordAppSinkHandlerId = g_signal_connect(_recordAppSink, "new_sample", G_CALLBACK(OnRecordSample), this);

        gst_context_unref(gstCtx);
        gst_object_unref(gstD3DDevice);
    }

    void GstWinRTPipeline::CreateAndLaunchVideoSinkPipeline()
    {
        winrt::hstring desc = _config.PipelineDescription;
        std::ostringstream  renderPipelineDesc;
        //renderPipelineDesc << "udpsrc name=videoSrc port=" << _config.SrcVideoPort << " "
        //    << "buffer-size=1048576 "
        //    << "caps=\"application/x-rtp,media=video,clock-rate=(int)90000,encoding-name=H265\" "
        //    << "! rtpjitterbuffer latency=100 drop-on-latency=true "
        //    << "! queue2 max-size-buffers=5 max-size-bytes=0 max-size-time=0 "
        //    << "! rtph265depay "
        //    << "! h265parse "
        //    << "! identity name=renderBufferSkip sync=false "
        //    << "! queue max-size-buffers=5 leaky=downstream "
        //    << "! decodebin name=autodecoder "
        //    << "! autovideosink sync=false "
        //    ;

        renderPipelineDesc << winrt::to_string(desc);

        GError* error = nullptr;
        _renderPipeline = gst_parse_launch(renderPipelineDesc.str().c_str(), &error);

        if (_renderPipeline == nullptr)
        {
            hstring msg = winrt::to_hstring(error->message);
            SetRenderState(GstWinRTState::Error, msg, error->code);
            g_error_free(error);
            return;
        }

        SetRenderState(GstWinRTState::PipelineCreated, L"", 0);

        GstBus* bus = gst_element_get_bus(_renderPipeline);
        if (bus != nullptr)
        {
            gst_bus_add_signal_watch(bus);
            g_signal_connect(bus, "message::state-changed", G_CALLBACK(OnRenderPipelineStateChanged), this);
            gst_object_unref(bus);
        }
    }

    void GstWinRTPipeline::Start()
    {
        if (_renderState == GstWinRTState::Initialized)
        {
            if (!_renderPipeline)
            {
                if (_config.PipelineMode == true)
                {
                    CreateAndLaunchVideoSinkPipeline();
                }
                else
                {
                    CreateAndLaunchRenderPipeline();
                }
            }

            StartGstMainLoop();

            if (_renderState != GstWinRTState::Error && _renderPipeline)
            {
                gst_element_set_state(_renderPipeline, GST_STATE_PLAYING);
                _lastRenderSampleTime = gst_clock_get_time(_renderClock);
                if (_config.PipelineMode == false)
                {
                    _watchdogTimer.Start();
                }
            }
        }

        return;
    }

    void GstWinRTPipeline::Pause()
    {
        gst_element_set_state(_renderPipeline, GST_STATE_PAUSED);
        return;
    }

    void GstWinRTPipeline::Stop()
    {
        if (_mfRecorder)
        {
            _mfRecorder->Stop();
        }

        if (_d3dRenderer)
        {
            _d3dRenderer->Stop();
        }

        if (_renderPipeline)
        {
            if (_renderSink && _renderSinkHandlerId != 0) {
                g_signal_handler_disconnect(_renderSink, _renderSinkHandlerId);
                _renderSinkHandlerId = 0;
            }
            if (_captureSink && _captureSinkHandlerId != 0) {
                g_signal_handler_disconnect(_captureSink, _captureSinkHandlerId);
                _captureSinkHandlerId = 0;
            }
            if (_recordAppSink && _recordAppSinkHandlerId != 0) {
                g_signal_handler_disconnect(_recordAppSink, _recordAppSinkHandlerId);
                _recordAppSinkHandlerId = 0;
            }

            gst_element_set_state(_renderSink, GST_STATE_NULL);
            gst_element_set_state(_captureSink, GST_STATE_NULL);
            gst_element_set_state(_recordAppSink, GST_STATE_NULL);
            gst_element_set_state(_recordAppSource, GST_STATE_NULL);

            gst_element_send_event(_renderPipeline, gst_event_new_eos());
            gst_element_set_state(_renderPipeline, GST_STATE_PAUSED);
            gst_element_set_state(_renderPipeline, GST_STATE_NULL);

            GstState state;
            gst_element_get_state(_renderPipeline, &state, NULL, 2 * GST_SECOND);
            printf("_renderPipeline state: %d\n", state);

            if (state == GST_STATE_NULL)
            {
                SetLogMessage(L"Stop() GstState::GST_STATE_NULL", 3);
                SetRenderState(GstWinRTState::RenderStopped, L"", 0);
                SetCaputerState(GstWinRTState::Initialized, L"", 0);
            }
        }

        if(_mfRecorder)
        {
            _mfRecorder.reset();
            SetRecordState(GstWinRTState::RecordStopped, L"", 0);
        }

        if (_d3dRenderer)
        {
            _d3dRenderer.reset();
        }

        return;
    }

    void GstWinRTPipeline::Close()
    {
        Stop();
        StopGstMainLoop();

        {
            // main render pipeline
            if (_recordAppSink)
            {
                gst_object_unref(_recordAppSink);
                _recordAppSink = nullptr;
            }

            if (_captureSink)
            {
                gst_object_unref(_captureSink);
                _captureSink = nullptr;
            }

            if (_renderSink) {
                gst_object_unref(_renderSink);
                _renderSink = nullptr;
            }

            // record pipeline
            if (_recordAppSource)
            {
                gst_object_unref(_recordAppSource);
                _recordAppSource = nullptr;
            }

            if (_renderPipeline)
            {
                gst_object_unref(_renderPipeline);
                _renderPipeline = nullptr;
            }

            if (_renderClock) {
                gst_object_unref(_renderClock);
                _renderClock = nullptr;
            }

            if (_watchdogTimer)
            {
                _watchdogTimer.Stop();
            }
        }

        if (_d3dContext)
        {
            _d3dContext->ClearState();
            _d3dContext->Flush();
        }

        _swapChain.Reset();
        _d3dContext.Reset();
        _factory.Reset();

        //ComPtr<ID3D11Debug> debug;
        //HRESULT hr = _d3dDevice->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void**>(debug.GetAddressOf()));
        //if (SUCCEEDED(hr))
        //{
        //    debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
        //}
        _d3dDevice.Reset();

        SetRenderState(GstWinRTState::Uninitialized, L"", 0);

        return;
    }

    void GstWinRTPipeline::Capture(winrt::hstring const& path)
    {
        SetCaputerState(GstWinRTState::CaptureStart, L"", 0);

        std::wstring wstrPath(path.c_str());
        _captureFilePath = wstrPath;
        _shouldCapture = true;
    }

    void GstWinRTPipeline::StartRecording(winrt::hstring const& path)
    {
        StartRecordingAsync(path);
    }

    void GstWinRTPipeline::PauseRecording()
    {
        
    }

    void GstWinRTPipeline::StopRecording()
    {
        StopRecordingAsync();
    }

    winrt::MediaFramework::GstWinRTState GstWinRTPipeline::GetRenderState()
    {
        return _renderState;
    }

    winrt::MediaFramework::GstWinRTState GstWinRTPipeline::GetRecordState()
    {
        return _recordState;
    }

    winrt::MediaFramework::GstWinRTState GstWinRTPipeline::GetCaptureState()
    {
        return _captureState;
    }

    uint64_t GstWinRTPipeline::GetRenderDelayTime()
    {
        return _renderDelayTimeMsec;
    }

    void GstWinRTPipeline::SetLeftHalfCrop(bool isLeftHalfCrop)
    {
        _d3dRenderer->SetLeftHalfCrop(isLeftHalfCrop);
    }

    bool GstWinRTPipeline::IsLeftHalfCrop() 
    {
        return _d3dRenderer->IsLeftHalfCrop();
    }

    void GstWinRTPipeline::SetRenderEnable(bool isEnable)
    {
        _d3dRenderer->SetRenderEnable(isEnable);
    }

    void GstWinRTPipeline::StartGstMainLoop()
    {
        auto context = g_main_context_default();
        _loop = g_main_loop_new(context, FALSE);
        _loopThread = std::thread([this] 
            { 
                g_main_loop_run(_loop); 
            }
        );
    }

    void GstWinRTPipeline::StopGstMainLoop()
    {
        if (_loop) {
            g_main_loop_quit(_loop);
            g_main_loop_unref(_loop);
            _loop = nullptr;
        }

        if (_loopThread.joinable()) {
            _loopThread.join();
        }
    }

    void GstWinRTPipeline::SetLogMessage(winrt::hstring const& message, int32_t logCode)
    {
        GstEventArgs args;
        args.EventType = GstEventType::MessageLogged;
        args.OldState = GstWinRTState::Uninitialized;
        args.NewState = GstWinRTState::Uninitialized;
        args.Message = message;
        args.Code = logCode;

        if (_uiDispatcherQueue) {
            _uiDispatcherQueue.TryEnqueue([args, this]
                {
                    RaiseGstEvent(args);
                }
            );
        }
        else
        {
            RaiseGstEvent(args);
        }
    }

    void GstWinRTPipeline::SetRenderState(GstWinRTState state, winrt::hstring const& message, int32_t code)
    {
        GstEventArgs args;
        args.EventType = GstEventType::RenderStateChanged;
        args.OldState = _renderState;
        args.NewState = state;
        args.Message = message;
        args.Code = code;

        _renderState = state;

        if (_uiDispatcherQueue) {
            _uiDispatcherQueue.TryEnqueue([args, this]
                {
                    RaiseGstEvent(args);
                }
            );
        }
        else
        {
            RaiseGstEvent(args);
        }
    }

    void GstWinRTPipeline::SetRecordState(GstWinRTState state, winrt::hstring const& message, int32_t code)
    {
        GstEventArgs args;
        args.EventType = GstEventType::RecordStateChanged;
        args.OldState = _recordState;
        args.NewState = state;
        args.Message = message;
        args.Code = code;

        _recordState = state;

        if (_uiDispatcherQueue) {
            _uiDispatcherQueue.TryEnqueue([args, this]
                {
                    RaiseGstEvent(args);
                }
            );
        }
        else
        {
            RaiseGstEvent(args);
        }
    }

    void GstWinRTPipeline::SetCaputerState(GstWinRTState state, winrt::hstring const& message, int32_t code)
    {
        GstEventArgs args;
        args.EventType = GstEventType::CaptureStateChanged;
        args.OldState = _captureState;
        args.NewState = state;
        args.Message = message;
        args.Code = code;

        _captureState = state;

        if (_uiDispatcherQueue) {
            _uiDispatcherQueue.TryEnqueue([args, this]
                {
                    RaiseGstEvent(args);
                }
            );
        }
        else
        {
            RaiseGstEvent(args);
        }
    }

    GstFlowReturn GstWinRTPipeline::OnRenderPipelineStateChanged(GstBus* bus, GstMessage* msg, gpointer userData)
    {
        GstState oldS, newS;
        gst_message_parse_state_changed(msg, &oldS, &newS, nullptr);
        auto self = static_cast<GstWinRTPipeline*>(userData);

        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->_renderPipeline)) {
            switch (newS) {
            case GstState::GST_STATE_NULL:
                printf("GstState::GST_STATE_NULL\r\n");
                self->SetLogMessage(L"GstState::GST_STATE_NULL", 3);
                self->SetRenderState(GstWinRTState::RenderStopped, L"", 0);
                self->SetCaputerState(GstWinRTState::Initialized, L"", 0);
                break;
            case GstState::GST_STATE_READY:
                printf("GstState::GST_STATE_READY\r\n");
                self->SetRenderState(GstWinRTState::RenderReady, L"", 0);
                self->SetCaputerState(GstWinRTState::Initialized, L"", 0);
                break;
            case GstState::GST_STATE_PLAYING:
                printf("GstState::GST_STATE_PLAYING\r\n");
                self->SetLogMessage(L"GstState::GST_STATE_PLAYING", 3);
                self->SetRenderState(GstWinRTState::Rendering, L"", 0);
                self->SetCaputerState(GstWinRTState::CaptureReady, L"", 0);
                break;
            case GstState::GST_STATE_PAUSED:
                printf("GstState::GST_STATE_PAUSED\r\n");
                self->SetRenderState(GstWinRTState::RenderPaused, L"", 0);
                self->SetCaputerState(GstWinRTState::Initialized, L"", 0);
                break;
            case GstState::GST_STATE_VOID_PENDING:
                printf("GstState::GST_STATE_VOID_PENDING\r\n");
                break;
            default:
                printf("GstState::  default\r\n");
                break;
            }
        }

        return GST_FLOW_OK;
    }

    GstFlowReturn GstWinRTPipeline::OnNewSample(GstAppSink* sink, gpointer user_data)
    {
        GstSample* sample = gst_app_sink_pull_sample(sink);
        auto self = static_cast<GstWinRTPipeline*>(user_data);

        static GstClockTime last_pts = GST_CLOCK_TIME_NONE;

        if (self->_renderState == GstWinRTState::RenderBuffering)
        {
            self->SetRenderState(GstWinRTState::Rendering, L"", 0);
        }
        self->_lastRenderSampleTime = gst_clock_get_time(self->_renderClock);

        bool wasPending = self->_renderPending.exchange(true);
        if (wasPending)
        {
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }

        GstCaps* caps = gst_sample_get_caps(sample);
        if (!caps) {
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }

        GstStructure* s = gst_caps_get_structure(caps, 0);
        int width = 0, height = 0;
        if (!gst_structure_get_int(s, "width", &width) || !gst_structure_get_int(s, "height", &height))
        {
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }

        self->_srcFrameWidth = width;
        self->_srcFrameHeight = height;

        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstClockTime pts = GST_CLOCK_TIME_NONE;
        if (buffer) {
            pts = GST_BUFFER_PTS(buffer);

            if (last_pts != GST_CLOCK_TIME_NONE && pts != GST_CLOCK_TIME_NONE) {
                double diff_ms = (pts - last_pts) / 1e6; // ns ¡æ ms
                printf("PTS: %" GST_TIME_FORMAT "   diff: %.3f ms\n", GST_TIME_ARGS(pts), diff_ms);
            }
            else if (pts != GST_CLOCK_TIME_NONE) {
                printf("PTS: %" GST_TIME_FORMAT "   (first sample)\n", GST_TIME_ARGS(pts));
            }
            last_pts = pts;
        }

        if (self->_d3dRenderer)
        {
            //self->_d3dRenderer->AddD3DTextureQueue(sample);
            self->_d3dRenderer->AddFrame(sample);
        }
        gst_sample_unref(sample);

        self->_renderPending = false;

        return GST_FLOW_OK;
    }

    GstFlowReturn GstWinRTPipeline::OnCaptureSample(GstAppSink* sink, gpointer user_data)
    {
        auto self = static_cast<GstWinRTPipeline*>(user_data);

        if (!self->_shouldCapture.load() || self->_isCaptureSaving.load()) {
            return GST_FLOW_OK;
        }

        self->_shouldCapture = false;
        self->_isCaptureSaving = true;

        GstSample* sample = gst_app_sink_pull_sample(sink);
        if (!sample)
            return GST_FLOW_ERROR;

        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstCaps* caps = gst_sample_get_caps(sample);
        if (!buffer || !caps) {
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }

        GstStructure* capStructure = gst_caps_get_structure(caps, 0);
        int width = 0, height = 0;
        gst_structure_get_int(capStructure, "width", &width);
        gst_structure_get_int(capStructure, "height", &height);

        GstMapInfo mapInfo;
        gst_buffer_map(buffer, &mapInfo, GST_MAP_READ);
        std::vector<uint8_t> bufferData(mapInfo.data, mapInfo.data + mapInfo.size);

        gst_buffer_unmap(buffer, &mapInfo);
        gst_sample_unref(sample);

        self->CaptureFrameAsync(std::move(bufferData), width, height, self->_captureFilePath );

        return GST_FLOW_OK;
    }

    GstFlowReturn GstWinRTPipeline::OnRecordSample(GstAppSink* sink, gpointer user_data)
    {
        auto self = static_cast<GstWinRTPipeline*>(user_data);
        if (!self->_mfRecorder)
        {
            return GST_FLOW_OK;
        }

        GstSample* sample = gst_app_sink_pull_sample(sink);
        if (!sample)
        {
            return GST_FLOW_OK;
        }

        bool wasPending = self->_recordPending.exchange(true);
        if (wasPending)
        {
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }

        if (self->_mfRecorder)
        {
            self->_mfRecorder->OnSample(sample);
        }
        gst_sample_unref(sample);

        self->_recordPending = false;

        return GST_FLOW_OK;
    }

    winrt::fire_and_forget GstWinRTPipeline::StartRecordingAsync(winrt::hstring const path)
    {
        co_await winrt::resume_background();

        _mfRecorder = std::make_unique<MediaFoundationRecorder>();
        HRESULT hr = _mfRecorder->Initialize(path.c_str(), _srcFrameWidth, _srcFrameHeight, 60, 16000000);
        if (SUCCEEDED(hr))
        {
            hr = _mfRecorder->Start();
        }

        bool ok = SUCCEEDED(hr);
        _uiDispatcherQueue.TryEnqueue([this, ok]() 
            {
                if (ok) {
                    SetRecordState(GstWinRTState::Recording, L"", 0);
                }
                else {
                    SetRecordState(GstWinRTState::Error, L"Recorder init failed", 0);
                }
            }
        );
    }

    winrt::fire_and_forget GstWinRTPipeline::StopRecordingAsync()
    {
        co_await winrt::resume_background();

        if (_mfRecorder)
        {
            _mfRecorder->Stop();
            _mfRecorder.reset();
        }

        SetRecordState(GstWinRTState::RecordStopped, L"", 0);
    }

    winrt::fire_and_forget GstWinRTPipeline::CaptureFrameAsync(std::vector<uint8_t> buffer, uint32_t width, uint32_t height, const std::wstring filePath)
    {
        co_await resume_background();

        try
        {
            co_await SaveBGRAAsPngAsync(buffer.data(), width, height, filePath);
            SetCaputerState(GstWinRTState::CaptureEnd, L"", 0);
        }
        catch (winrt::hresult_error const& ex)
        {
            OutputDebugStringW((L"PNG save fail: " + ex.message() + L"\n").c_str());
            SetCaputerState(GstWinRTState::Error, L"", 0);
        }

        _isCaptureSaving = false;
        SetCaputerState(GstWinRTState::CaptureReady, L"", 0);
    }

    winrt::Windows::Foundation::IAsyncAction GstWinRTPipeline::SaveBGRAAsPngAsync(
        uint8_t const* bgra, uint32_t width, uint32_t height, const std::wstring& filePath)
    {
        using namespace Windows::Foundation;
        using namespace Windows::Storage;
        using namespace Windows::Storage::Streams;
        using namespace Windows::Graphics::Imaging;

        std::filesystem::path fsPath(filePath);
        std::filesystem::path parent = fsPath.parent_path();
        std::filesystem::path filename = fsPath.filename();

        if (!parent.empty() && !std::filesystem::exists(parent))
        {
            std::filesystem::create_directories(parent);
        }

        StorageFolder folder = co_await StorageFolder::GetFolderFromPathAsync(winrt::hstring{ parent.c_str() });
        StorageFile file = co_await folder.CreateFileAsync(winrt::hstring{ filename.c_str() }, CreationCollisionOption::ReplaceExisting);
        IRandomAccessStream stream = co_await file.OpenAsync(FileAccessMode::ReadWrite);
        BitmapEncoder encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream);
        encoder.SetPixelData(
            BitmapPixelFormat::Bgra8,
            BitmapAlphaMode::Premultiplied,
            width, height,
            96, 96,
            array_view(bgra, bgra + (uint64_t)width * height * 4));
        co_await encoder.FlushAsync();
    }

    winrt::event_token GstWinRTPipeline::GstWinRTEvent(winrt::MediaFramework::GstWinRTEventHandler const& handler)
    {
        return m_gstEventRaised.add(handler);
    }

    void GstWinRTPipeline::GstWinRTEvent(winrt::event_token const& token) noexcept
    {
        m_gstEventRaised.remove(token);
    }

    void GstWinRTPipeline::RaiseGstEvent(MediaFramework::GstEventArgs args)
    {
        m_gstEventRaised(*this, args);
    }
}
