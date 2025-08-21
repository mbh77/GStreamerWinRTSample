#pragma once
#include "GstWinRTPipeline.g.h"

#include "GstWinRT.h"
#include "StreamingFrameRenderer.h"
#include "MediaFoundationRecorder.h"
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace winrt::MediaFramework::implementation
{
    struct GstWinRTPipeline : GstWinRTPipelineT<GstWinRTPipeline>
    {
        GstWinRTPipeline(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& uiDispatcherQueue);

        void Initialize(MediaFramework::StreamerData config, winrt::Microsoft::UI::Xaml::UIElement const& host,
            winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& videoPanel);
        void Start();
        void Pause();
        void Stop();
        void Close();
        void Capture(winrt::hstring const& path);
        void StartRecording(winrt::hstring const& path);
        void PauseRecording();
        void StopRecording();
        winrt::MediaFramework::GstWinRTState GetRenderState();
        winrt::MediaFramework::GstWinRTState GetRecordState();
        winrt::MediaFramework::GstWinRTState GetCaptureState();
        uint64_t GetRenderDelayTime();
        void SetLeftHalfCrop(bool isLeftHalfCrop);
        bool IsLeftHalfCrop();
        void SetRenderEnable(bool isEnable);

        winrt::event_token GstWinRTEvent(winrt::MediaFramework::GstWinRTEventHandler const& handler);
        void GstWinRTEvent(winrt::event_token const& token) noexcept;

    private:
        StreamerData _config;
        // main render pipeline
        GstElement* _renderPipeline = nullptr;
        GstElement* _renderSink = nullptr;
        GstElement* _captureSink = nullptr;
        GstElement* _recordAppSink = nullptr;
        GstElement* _recordAppSource = nullptr;

        gulong _renderSinkHandlerId = 0;
        gulong _captureSinkHandlerId = 0;
        gulong _recordAppSinkHandlerId = 0;

        //D3D Frame Renderer
        std::unique_ptr<StreamingFrameRenderer> _d3dRenderer;

        //MediaFoundation Record
        std::unique_ptr<MediaFoundationRecorder> _mfRecorder;
        UINT32 _srcFrameWidth = 0;
        UINT32 _srcFrameHeight = 0;
        std::atomic<bool> _recordPending{ false };

        const GstClockTime BufferEventInterval = 3 * GST_SECOND;
        GstClockTime _lastRenderSampleTime = 0;
        GstClockTime _lastBufferEventTime = 0;
        uint64_t _renderDelayTimeMsec = 0;
        GstClock* _renderClock = nullptr;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer _watchdogTimer{ nullptr };

        winrt::Microsoft::UI::Composition::Compositor _compositor{ nullptr };

        // D3D11
        ComPtr<ID3D11Device> _d3dDevice;
        ComPtr<ID3D11DeviceContext> _d3dContext;
        ComPtr<IDXGIFactory6> _factory;
        ComPtr<IDXGISwapChain1> _swapChain{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel _videoPanel;

        uint64_t _adapterLuid = 0;
        uint32_t _adapterIndex = 0;

        winrt::Microsoft::UI::Dispatching::DispatcherQueue _uiDispatcherQueue{ nullptr };
        int _videoWidth = 0;
        int _videoHeight = 0;

        GstWinRTState _renderState = GstWinRTState::Uninitialized;
        GstWinRTState _recordState = GstWinRTState::Uninitialized;
        GstWinRTState _captureState = GstWinRTState::Uninitialized;
        GMainLoop* _loop{};
        std::thread _loopThread;

        std::atomic<bool> _renderPending{ false };

        std::atomic<bool> _shouldCapture{ false };
        std::atomic<bool> _isCaptureSaving{ false };
        std::wstring _captureFilePath;

        bool CreateVideoLayer(winrt::Microsoft::UI::Xaml::UIElement const& host,
            winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& videoPanel);
        void CreateAndLaunchRenderPipeline();
        void CreateAndLaunchVideoSinkPipeline();
        void StartGstMainLoop();
        void StopGstMainLoop();
        static GstFlowReturn OnNewSample(GstAppSink* sink, gpointer user_data);
        static GstFlowReturn OnCaptureSample(GstAppSink* sink, gpointer user_data);
        static GstFlowReturn OnRecordSample(GstAppSink* sink, gpointer user_data);
        winrt::fire_and_forget CaptureFrameAsync(std::vector<uint8_t> buffer, uint32_t width, uint32_t height, const std::wstring filePath);
        winrt::Windows::Foundation::IAsyncAction SaveBGRAAsPngAsync(uint8_t const* bgra, uint32_t width, uint32_t height, const std::wstring& filePath);
        winrt::fire_and_forget StartRecordingAsync(winrt::hstring const path);
        winrt::fire_and_forget StopRecordingAsync();

        static GstFlowReturn OnRenderPipelineStateChanged(GstBus* bus, GstMessage* msg, gpointer userData);
        void SetLogMessage(winrt::hstring const& message, int32_t logCode);
        void SetRenderState(GstWinRTState state, winrt::hstring const& message, int32_t code);
        void SetRecordState(GstWinRTState state, winrt::hstring const& message, int32_t code);
        void SetCaputerState(GstWinRTState state, winrt::hstring const& message, int32_t code);
        winrt::event<GstWinRTEventHandler> m_gstEventRaised;
        void RaiseGstEvent(MediaFramework::GstEventArgs args);
    };
}
namespace winrt::MediaFramework::factory_implementation
{
    struct GstWinRTPipeline : GstWinRTPipelineT<GstWinRTPipeline, implementation::GstWinRTPipeline>
    {
    };
}
