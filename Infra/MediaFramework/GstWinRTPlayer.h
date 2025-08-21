#pragma once
#include "GstWinRTPlayer.g.h"

#include "GstWinRT.h"
#include "PlayerFrameRenderer.h"
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace winrt::MediaFramework::implementation
{
    struct GstWinRTPlayer : GstWinRTPlayerT<GstWinRTPlayer>
    {
        GstWinRTPlayer(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& uiDispatcherQueue);
        void Initialize(
            winrt::Microsoft::UI::Xaml::UIElement const& host,
            winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel
        );

        void Play(winrt::hstring const& path);
        void Play();
        void Pause();
        void SeekTo(double seconds);
        void Stop();
        void Close();
        void SetLeftHalfCrop(bool isLeftHalfCrop);
        bool IsLeftHalfCrop();

        double GetTotalDurationSeconds();
        double GetPositionSeconds();

        winrt::event_token GstPlayerEvent(winrt::MediaFramework::GstPlayerEventHandler const& handler);
        void GstPlayerEvent(winrt::event_token const& token) noexcept;
    private:
        // main render pipeline
        GstElement* _playerPipeline = nullptr;
        GstElement* _filesrc = nullptr;
        GstElement* _decoder = nullptr;
        GstElement* _playerSink = nullptr;

        // d3d11
        ComPtr<ID3D11Device> _d3dDevice;
        ComPtr<ID3D11DeviceContext> _d3dContext;
        ComPtr<IDXGIFactory6> _factory;
        ComPtr<IDXGISwapChain1> _swapChain{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel _videoPanel;

        //D3D Frame Renderer
        std::unique_ptr<PlayerFrameRenderer> _playerRenderer;

        uint64_t _adapterLuid = 0;
        uint32_t _adapterIndex = 0;

        std::atomic<gint64> _latestPositionNs{ 0 };

        winrt::Microsoft::UI::Dispatching::DispatcherQueue _uiDispatcherQueue{ nullptr };

        bool CreateVideoLayer(winrt::Microsoft::UI::Xaml::UIElement const& host,
            winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& videoPanel);

        GstPlayerState _playerState = GstPlayerState::Uninitialized;
        GMainLoop* _loop{};
        std::thread _loopThread;

        void CreateAndLaunchRenderPipeline();
        static GstFlowReturn OnNewSample(GstAppSink* sink, gpointer user_data);

        void StartGstMainLoop();
        void StopGstMainLoop();

        static GstFlowReturn OnPlayerPipelineStateChanged(GstBus* bus, GstMessage* msg, gpointer userData);
        void SetLogMessage(winrt::hstring const& message, int32_t logCode);
        void SetState(GstPlayerState state, winrt::hstring const& message, int32_t code);
        winrt::event<GstPlayerEventHandler> m_playerEventRaised;
        void RaisePlayerEvent(MediaFramework::GstPlayerEventArgs args);
    };
}
namespace winrt::MediaFramework::factory_implementation
{
    struct GstWinRTPlayer : GstWinRTPlayerT<GstWinRTPlayer, implementation::GstWinRTPlayer>
    {
    };
}
