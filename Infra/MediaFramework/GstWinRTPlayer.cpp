#include "pch.h"
#include "GstWinRTPlayer.h"
#include "GstWinRTPlayer.g.cpp"

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

namespace winrt::MediaFramework::implementation
{
    GstWinRTPlayer::GstWinRTPlayer(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& uiDispatcherQueue)
        :_uiDispatcherQueue(uiDispatcherQueue)
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

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

    void GstWinRTPlayer::Initialize(
        winrt::Microsoft::UI::Xaml::UIElement const& host,
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel
    )
    {
        CreateVideoLayer(host, panel);
    }

    bool GstWinRTPlayer::CreateVideoLayer(
        winrt::Microsoft::UI::Xaml::UIElement const& host,
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& videoPanel
    )
    {
        using namespace winrt;
        using namespace winrt::Microsoft::UI::Composition;
        using namespace winrt::Microsoft::UI::Xaml::Hosting;
        using namespace winrt::Microsoft::UI::Xaml;

        _playerRenderer = std::make_unique<PlayerFrameRenderer>(
            _uiDispatcherQueue,
            _d3dDevice,
            _d3dContext,
            _swapChain,
            _factory,
            videoPanel,
            [this](MediaFramework::GstPlayerEventArgs args) {
                this->RaisePlayerEvent(std::move(args));
            }
        );
        _playerRenderer->Initialize((UINT)1920, (UINT)1080);

        return true;
    }

    void GstWinRTPlayer::CreateAndLaunchRenderPipeline()
    {
        GstD3D11Device* gstD3DDevice = gst_d3d11_device_new_wrapped(_d3dDevice.Get());
        GstContext* gstCtx = gst_d3d11_context_new(gstD3DDevice);

        std::ostringstream  playerPipelineDesc;
        playerPipelineDesc
            << "filesrc name=filesrc "
            << "! decodebin name=autodecoder "
            << "! video/x-raw(memory:D3D11Memory),format=NV12 "
            //<< "! videorate "
            //<< "! video/x-raw(memory:D3D11Memory),format=NV12,framerate=60/1 "
            << "! d3d11convert name=d3d11convert0 "
            << "! d3d11upload name=d3d11upload0 "
            << "! video/x-raw(memory:D3D11Memory),format=BGRA "
            << "! appsink name=playerSink emit-signals=true sync=true async=false max-buffers=10 drop=false";

        GError* error = nullptr;
        _playerPipeline = gst_parse_launch(playerPipelineDesc.str().c_str(), &error);

        if (_playerPipeline == nullptr)
        {
            hstring msg = winrt::to_hstring(error->message);
            SetState(GstPlayerState::Error, msg, error->code);
            g_error_free(error);
            return;
        }

        _decoder = gst_bin_get_by_name(GST_BIN(_playerPipeline), "autodecoder");
        if (_decoder)
        {
            g_signal_connect(
                _decoder,
                "element-added",
                G_CALLBACK(+[](GstElement* dbin, GstElement* element, gpointer)
                    {
                        const gchar* factory_name = gst_element_factory_get_longname(gst_element_get_factory(element));
                        printf("[decodebin] added element: %s\n", factory_name);
                    }),
                nullptr
            );
        }

        _filesrc = gst_bin_get_by_name(GST_BIN(_playerPipeline), "filesrc");
        if (_filesrc == nullptr)
        {
            SetState(GstPlayerState::Error, L"_filesrc is nullptr!!!", 0);
            //return;
        }

        _playerSink = gst_bin_get_by_name(GST_BIN(_playerPipeline), "playerSink");
        if (_playerSink == nullptr)
        {
            SetState(GstPlayerState::Error, L"_playerSink is nullptr!!!", 0);
            //return;
        }

        GstBus* bus = gst_element_get_bus(_playerPipeline);
        if (bus != nullptr)
        {
            gst_bus_add_signal_watch(bus);
            g_signal_connect(bus, "message::state-changed", G_CALLBACK(OnPlayerPipelineStateChanged), this);
            gst_object_unref(bus);
        }

        // set d3d device
        gst_element_set_context(GST_ELEMENT(_playerPipeline), gstCtx);

        GstElement* convert = gst_bin_get_by_name(GST_BIN(_playerPipeline), "d3d11convert0");
        if (convert) {
            g_object_set(convert, "adapter", _adapterIndex, nullptr);
            gst_object_unref(convert);
        }

        GstElement* upload = gst_bin_get_by_name(GST_BIN(_playerPipeline), "d3d11upload0");
        if (upload) {
            g_object_set(upload, "adapter", _adapterIndex, nullptr);
            gst_object_unref(upload);
        }

        gst_context_unref(gstCtx);

        gst_app_sink_set_emit_signals(GST_APP_SINK(_playerSink), TRUE);
        g_signal_connect(_playerSink, "new-sample", G_CALLBACK(OnNewSample), this);

        SetState(GstPlayerState::PipelineCreated, L"", 0);
    }

    GstFlowReturn GstWinRTPlayer::OnNewSample(GstAppSink* sink, gpointer user_data)
    {
        GstSample* sample = gst_app_sink_pull_sample(sink);
        auto self = static_cast<GstWinRTPlayer*>(user_data);

        static gint64 framecnt = 0;
        static gint64 next_deadline_ns = 0;
        static gint64 last_ns = 0;
        static GstClockTime last_pts = GST_CLOCK_TIME_NONE;

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

        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstClockTime pts = GST_CLOCK_TIME_NONE;
        if (buffer) {
            pts = GST_BUFFER_PTS(buffer);

            if (pts != GST_CLOCK_STIME_NONE)
            {
                self->_latestPositionNs.store(pts, std::memory_order_relaxed);
            }

            if (last_pts != GST_CLOCK_TIME_NONE && pts != GST_CLOCK_TIME_NONE) {
                double diff_ms = (pts - last_pts) / 1e6; // ns ¡æ ms
                printf("PTS: %" GST_TIME_FORMAT "   diff: %.3f ms\n", GST_TIME_ARGS(pts), diff_ms);
            }
            else if (pts != GST_CLOCK_TIME_NONE) {
                printf("PTS: %" GST_TIME_FORMAT "   (first sample)\n", GST_TIME_ARGS(pts));
            }
            last_pts = pts;
        }

        if (self->_playerRenderer)
        {
            self->_playerRenderer->AddFrame(sample);
        }

        gst_sample_unref(sample);

        return GST_FLOW_OK;
    }

    GstFlowReturn GstWinRTPlayer::OnPlayerPipelineStateChanged(GstBus* bus, GstMessage* msg, gpointer userData)
    {
        GstState oldS, newS, pending;
        gst_message_parse_state_changed(msg, &oldS, &newS, &pending);
        auto self = static_cast<GstWinRTPlayer*>(userData);

        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->_playerPipeline)) {
            switch (newS) {
            case GstState::GST_STATE_NULL:
                self->SetLogMessage(L"Player GstState::GST_STATE_NULL", 3);
                self->SetState(GstPlayerState::Stopped, L"", 0);
                break;
            case GstState::GST_STATE_READY:
                self->SetState(GstPlayerState::Ready, L"", 0);
                break;
            case GstState::GST_STATE_PLAYING:
                self->SetState(GstPlayerState::Playing, L"", 0);
                break;
            case GstState::GST_STATE_PAUSED:
                self->SetState(GstPlayerState::Paused, L"", 0);
                break;
            case GstState::GST_STATE_VOID_PENDING:
                break;
            default:
                break;
            }

            if (oldS == GST_STATE_PAUSED && newS == GST_STATE_PLAYING)
            {
                double total_duration = self->GetTotalDurationSeconds();

                GstPlayerEventArgs args;
                args.EventType = GstPlayerEventType::DurationChanged;
                args.OldState = GstPlayerState::Uninitialized;
                args.NewState = GstPlayerState::Uninitialized;
                args.Message = L"";
                args.Duration = total_duration;
                args.Code = 0;

                if (self->_uiDispatcherQueue) {
                    self->_uiDispatcherQueue.TryEnqueue([args, self]
                        {
                           self->RaisePlayerEvent(args);
                        }
                    );
                }
            }
        }

        return GST_FLOW_OK;
    }

    void GstWinRTPlayer::Play(winrt::hstring const& path)
    {
        CreateAndLaunchRenderPipeline();
        StartGstMainLoop();

        std::wstring wpath = path.c_str();
        int    size_needed = WideCharToMultiByte(CP_UTF8, 0, wpath.data(), (int)wpath.size(), nullptr, 0, nullptr, nullptr);
        std::string utf8(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wpath.data(), (int)wpath.size(), utf8.data(), size_needed, nullptr, nullptr);

        std::replace(utf8.begin(), utf8.end(), '\\', '/');

        g_object_set(G_OBJECT(_filesrc), "location", utf8.c_str(), nullptr);

        gst_element_set_state(_playerPipeline, GST_STATE_PLAYING);
    }

    void GstWinRTPlayer::Play()
    {
        _playerRenderer->ResetTiming();
        gst_element_set_state(_playerPipeline, GST_STATE_PLAYING);
    }

    void GstWinRTPlayer::Pause()
    {
        gst_element_set_state(_playerPipeline, GST_STATE_PAUSED);
    }

    void GstWinRTPlayer::SeekTo(double seconds)
    {
        if (!_playerPipeline)
            return;

        gint64 target_ns = static_cast<gint64>(seconds * GST_SECOND);
        gst_element_seek_simple(
            _playerPipeline,
            GST_FORMAT_TIME,
            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
            target_ns
        );

        if (_playerRenderer)
        {
            _playerRenderer->ResetTiming();
        }
    }

    void GstWinRTPlayer::Stop()
    {
        if (_playerRenderer)
        {
            _playerRenderer->Stop();
        }

        if (_playerPipeline)
        {
            gst_element_set_state(_playerPipeline, GST_STATE_NULL);
        }

        if (_playerRenderer)
        {
            _playerRenderer.reset();
        }
    }

    void GstWinRTPlayer::Close()
    {
        Stop();
        StopGstMainLoop();

        {
            // main render pipeline
            if (_filesrc)
            {
                gst_object_unref(_filesrc);
                _filesrc = nullptr;
            }

            if (_decoder)
            {
                gst_object_unref(_decoder);
                _decoder = nullptr;
            }

            if (_playerSink)
            {
                gst_object_unref(_playerSink);
                _playerSink = nullptr;
            }

            if (_playerPipeline)
            {
                gst_object_unref(_playerPipeline);
                _playerPipeline = nullptr;
            }
        }

        SetState(GstPlayerState::Uninitialized, L"", 0);
    }

    void GstWinRTPlayer::SetLeftHalfCrop(bool isLeftHalfCrop)
    {
        _playerRenderer->SetLeftHalfCrop(isLeftHalfCrop);
        if (_playerState == GstPlayerState::Paused)
        {
            _playerRenderer->RenderLastFrame();
        }
    }

    bool GstWinRTPlayer::IsLeftHalfCrop()
    {
        return _playerRenderer->IsLeftHalfCrop();
    }

    double GstWinRTPlayer::GetTotalDurationSeconds()
    {
        if (!_playerPipeline)
            return 0.0;

        GstFormat fmt = GST_FORMAT_TIME;
        gint64 duration_ns = 0;

        if (!gst_element_query_duration(_playerPipeline, fmt, &duration_ns))
        {
            return 0.0;
        }

        return static_cast<double>(duration_ns) / GST_SECOND;
    }

    double GstWinRTPlayer::GetPositionSeconds()
    {
        gint64 ns = _latestPositionNs.load(std::memory_order_relaxed);
        return static_cast<double>(ns) / GST_SECOND;
    }

    void GstWinRTPlayer::StartGstMainLoop()
    {
        auto context = g_main_context_default();
        _loop = g_main_loop_new(context, FALSE);
        _loopThread = std::thread([this]
            {
                g_main_loop_run(_loop);
            }
        );
    }

    void GstWinRTPlayer::StopGstMainLoop()
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

    void GstWinRTPlayer::SetLogMessage(winrt::hstring const& message, int32_t logCode)
    {
        GstPlayerEventArgs args;
        args.EventType = GstPlayerEventType::MessageLogged;
        args.OldState = GstPlayerState::Uninitialized;
        args.NewState = GstPlayerState::Uninitialized;
        args.Message = message;
        args.Code = logCode;

        if (_uiDispatcherQueue) {
            _uiDispatcherQueue.TryEnqueue([args, this]
                {
                    RaisePlayerEvent(args);
                }
            );
        }
        else
        {
            RaisePlayerEvent(args);
        }
    }

    void GstWinRTPlayer::SetState(GstPlayerState state, winrt::hstring const& message, int32_t code)
    {
        GstPlayerEventArgs args;
        args.EventType = GstPlayerEventType::StateChanged;
        args.OldState = _playerState;
        args.NewState = state;
        args.Message = message;
        args.Code = code;

        _playerState = state;

        if (_uiDispatcherQueue)
        {
            _uiDispatcherQueue.TryEnqueue([args, this]
                {
                    RaisePlayerEvent(args);
                }
            );
        }
    }

    winrt::event_token GstWinRTPlayer::GstPlayerEvent(winrt::MediaFramework::GstPlayerEventHandler const& handler)
    {
        return m_playerEventRaised.add(handler);
    }

    void GstWinRTPlayer::GstPlayerEvent(winrt::event_token const& token) noexcept
    {
        m_playerEventRaised.remove(token);
    }

    void GstWinRTPlayer::RaisePlayerEvent(MediaFramework::GstPlayerEventArgs args)
    {
        m_playerEventRaised(*this, args);
    }
}