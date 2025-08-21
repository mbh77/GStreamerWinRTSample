#pragma once
#include <concurrent_queue.h>
#include <wrl/client.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <functional>
#include <winrt/MediaFramework.h>
#include <atomic>
#include <chrono>
#include <mutex>

#include "FrameQueue.h"

using Microsoft::WRL::ComPtr;
using concurrency::concurrent_queue;

class PlayerFrameRenderer
{
public:
    using GstEventCallback = std::function<void(winrt::MediaFramework::GstPlayerEventArgs const&)>;

    PlayerFrameRenderer(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& uiDispatcherQueue,
        ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context, ComPtr<IDXGISwapChain1> swapChain, ComPtr<IDXGIFactory6> factory,
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& videoPanel,
        GstEventCallback eventCallback);
    ~PlayerFrameRenderer();

    bool Initialize(UINT width, UINT height);
    void Stop();
    void AddFrame(GstSample* sample);
    void ResetTiming() {
        _frameQueue.Flush();
        _needResetTiming.store(true, std::memory_order_relaxed);
    }
    void SetLeftHalfCrop(bool isLeftHalfCrop);
    bool IsLeftHalfCrop();
    void RenderLastFrame();

private:
    winrt::Microsoft::UI::Dispatching::DispatcherQueue _uiDispatcherQueue{ nullptr };

    ComPtr<ID3D11Device> _device;
    ComPtr<ID3D11DeviceContext> _context;
    ComPtr<ID3D11VideoDevice> _videoDev;
    ComPtr<ID3D11VideoContext> _videoCtx;

    ComPtr<ID3D11VideoProcessorEnumerator> _procEnum;
    ComPtr<ID3D11VideoProcessor> _videoProc;
    ComPtr<IDXGIFactory6> _factory;
    ComPtr<IDXGISwapChain1> _swapChain;
    winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel _videoPanel;

    ComPtr<ID3D11Texture2D> _pBackBuffer;
    ComPtr<ID3D11RenderTargetView> _pRenderTargetView;

    uint32_t _srcWidth = 0;
    uint32_t _srcHeight = 0;
    uint32_t _dstWidth = 0;
    uint32_t _dstHeight = 0;

    float _leftCropRatio = 1.0f;
    bool _firstTime = true;
    std::atomic<bool> _needResetTiming{ false };

    std::atomic<bool> _keepRunning;
    std::thread _renderThread;
    FrameQueue _frameQueue;

    FrameItem _lastFrameItem;
    bool _hasLastFrame{ false };

    GstEventCallback _eventCallback;

    void CreateSwapChain(UINT width, UINT height);
    void RenderLoop();
    void RenderSwapChainVideoBlt(ComPtr<ID3D11Texture2D> srcTexture);
    void ClearDisplay();

    void SetLogMessage(winrt::hstring const& message, int32_t logCode);
    void LogTextureAdapter(ID3D11Texture2D* texture, const wchar_t* label);
};

