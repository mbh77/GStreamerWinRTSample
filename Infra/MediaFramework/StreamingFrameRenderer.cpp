#include "pch.h"

#include <gst/d3d11/gstd3d11.h>
#include <gst/d3d11/gstd3d11memory.h>
#include <gst/gstevent.h>
#include <filesystem>

#include <fstream>
#include <string>
#include <iostream>
#include <sstream>

#include "StreamingFrameRenderer.h"

StreamingFrameRenderer::StreamingFrameRenderer(
    winrt::Microsoft::UI::Dispatching::DispatcherQueue const& uiDispatcherQueue,
    ComPtr<ID3D11Device> device,
    ComPtr<ID3D11DeviceContext> context,
    ComPtr<IDXGISwapChain1> swapChain,
    ComPtr<IDXGIFactory6> factory,
    winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& videoPanel,
    GstEventCallback eventCallback)
    : _uiDispatcherQueue(uiDispatcherQueue)
    , _device(device)
    , _context(context)
    , _swapChain(swapChain)
    , _factory(factory)
    , _videoPanel(videoPanel)
    , _eventCallback(eventCallback)
    , _frameQueue(10)
    , _srcWidth(0)
    , _srcHeight(0)
    , _dstWidth(0)
    , _dstHeight(0)
{
    _device.As(&_videoDev);
    _context.As(&_videoCtx);

    _keepRunning.store(true, std::memory_order_release);
    _renderThread = std::thread([this] { RenderLoop(); });
}

StreamingFrameRenderer::~StreamingFrameRenderer()
{
    if (_keepRunning.load(std::memory_order_acquire) == true)
    {
        Stop();
    }
}

void StreamingFrameRenderer::Stop()
{
    _keepRunning.store(false, std::memory_order_release);

    if (_renderThread.joinable())
    {
        _renderThread.join();
    }

    _frameQueue.Flush();

    _pBackBuffer.Reset();
    _pRenderTargetView.Reset();

    if (_context)
    {
        _context->ClearState();
        _context->Flush();
    }

    auto panelUnknown = reinterpret_cast<IUnknown*>(winrt::get_abi(_videoPanel));
    ComPtr<ISwapChainPanelNative> panelNative;
    HRESULT hr = panelUnknown->QueryInterface(__uuidof(ISwapChainPanelNative), (void**)panelNative.GetAddressOf());
    panelNative->SetSwapChain(nullptr);

    // Release COM resources
    _videoProc.Reset();
    _procEnum.Reset();
    _videoDev.Reset();
    _videoCtx.Reset();
    _context.Reset();
    _device.Reset();

    _eventCallback = nullptr;
}

bool StreamingFrameRenderer::Initialize(UINT width, UINT height)
{
    CreateSwapChain(width, height);
    return true;
}

void StreamingFrameRenderer::SetLeftHalfCrop(bool isLeftHalfCrop)
{
    if (IsLeftHalfCrop() != isLeftHalfCrop)
    {
        if (isLeftHalfCrop)
        {
            _leftCropRatio = 0.5f;
        }
        else
        {
            _leftCropRatio = 1.0f;
        }
    }
}

bool StreamingFrameRenderer::IsLeftHalfCrop()
{
    if (_leftCropRatio == 1.0f)
    {
        return false;
    }
    else
    {
        return true;
    }
}

void StreamingFrameRenderer::SetRenderEnable(bool enable)
{
    _renderEnable = enable;
}

void StreamingFrameRenderer::CreateSwapChain(UINT width, UINT height)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    _device.As(&dxgiDevice);

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = width;
    scDesc.Height = height;
    scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.Stereo = FALSE;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    _factory->CreateSwapChainForComposition(dxgiDevice.Get(), &scDesc, nullptr, _swapChain.ReleaseAndGetAddressOf());

    auto panelUnknown = reinterpret_cast<IUnknown*>(winrt::get_abi(_videoPanel));
    ComPtr<ISwapChainPanelNative> panelNative;
    HRESULT hr = panelUnknown->QueryInterface(__uuidof(ISwapChainPanelNative), (void**)panelNative.GetAddressOf());
    panelNative->SetSwapChain(_swapChain.Get());

    hr = _swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(_pBackBuffer.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        SetLogMessage(L"_swapChain->GetBuffer fail", 3);
        return;
    }

    hr = _device->CreateRenderTargetView(_pBackBuffer.Get(), nullptr, _pRenderTargetView.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        SetLogMessage(L"_device->CreateRenderTargetView fail", 3);
        return;
    }
}

void StreamingFrameRenderer::AddFrame(GstSample* sample)
{
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    int64_t pts = GST_BUFFER_PTS(buffer);

    GstD3D11Memory* d3dmem = GST_D3D11_MEMORY_CAST(gst_buffer_peek_memory(buffer, 0));
    if (gst_is_d3d11_memory((GstMemory*)d3dmem) == false)
    {
        return;
    }

    ID3D11Resource* srcResourceHandle = gst_d3d11_memory_get_resource_handle(d3dmem);
    if (!srcResourceHandle) {
        return;
    }

    ComPtr<ID3D11Texture2D> texture;
    HRESULT hrQI = srcResourceHandle->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(texture.GetAddressOf()));
    if (FAILED(hrQI) || !texture)
    {
        return;
    }

    _frameQueue.AddFrame(texture, pts);
}

void StreamingFrameRenderer::RenderLoop()
{
    int64_t playStartPTS = -1;
    auto playStartTime = std::chrono::steady_clock::now();
    int frameCount = 0;
    bool justBeforefirstFrame = true;

    double moving_avg = (1000.0 / 60.0) * 0.7;
    bool skipNextFrame = false;

    while (_keepRunning) {
        FrameItem item;

        if (!_frameQueue.WaitAndPop(item, 100))
            continue;

        if (_needResetTiming.load(std::memory_order_relaxed)) {
            playStartPTS = -1;
            _needResetTiming.store(false, std::memory_order_relaxed);
        }

        if (playStartPTS < 0) {
            playStartPTS = item.pts;
            playStartTime = std::chrono::steady_clock::now();
        }

        int64_t ptsDiffNs = item.pts - playStartPTS;
        auto targetTime = playStartTime + std::chrono::nanoseconds(ptsDiffNs);

        auto now = std::chrono::steady_clock::now();

        auto delta = now - targetTime;
        frameCount = (frameCount + 1) % 60;
        if (frameCount == 0) {
            playStartTime += delta;
        }

        auto delayMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - targetTime).count();
        //if (delayMs > 33) {
        //    printf("frame skip !!\n");
        //    continue;
        //}

        if (now < targetTime)
        {
            _frameQueue.SleepHighPrecision(targetTime);
        }

        auto ptsmsec = std::chrono::milliseconds(item.pts / 1000000);

        if (ptsmsec > std::chrono::milliseconds(700))
        {
            if (justBeforefirstFrame)
            {
                justBeforefirstFrame = false;
                ResetTiming();
            }
            else
            {
                if (skipNextFrame == false)
                {
                    auto t0 = std::chrono::steady_clock::now();
                    RenderSwapChainVideoBlt(item.texture);
                    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

                    moving_avg = moving_avg * 0.9 + dt * 0.1;
                    if (moving_avg > 11.5 || dt > 13.0)
                    {
                        skipNextFrame = true;
                        printf("frame skip\r\n");
                    }
                }
                else
                {
                    skipNextFrame = false;
                }
            }
        }
    }
}

void StreamingFrameRenderer::RenderSwapChainPanel(ComPtr<ID3D11Texture2D> srcTexture)
{
    D3D11_TEXTURE2D_DESC srcDesc{};
    srcTexture->GetDesc(&srcDesc);

    if ((UINT32)((float)srcDesc.Width * _leftCropRatio) != _srcWidth || (UINT32)srcDesc.Height != _srcHeight)
    {
        _srcWidth = (UINT32)((float)srcDesc.Width * _leftCropRatio);
        _srcHeight = srcDesc.Height;

        _pRenderTargetView.Reset();
        _pBackBuffer.Reset();

        _swapChain->ResizeBuffers(2, _srcWidth, _srcHeight, DXGI_FORMAT_B8G8R8A8_UNORM, 0);

        _swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(_pBackBuffer.ReleaseAndGetAddressOf()));
        _device->CreateRenderTargetView(_pBackBuffer.Get(), nullptr, _pRenderTargetView.ReleaseAndGetAddressOf());
         
        _uiDispatcherQueue.TryEnqueue([this]()
            {
                _videoPanel.Width(_srcWidth);
                _videoPanel.Height(_srcHeight);
            });
    }

    _context->OMSetRenderTargets(1, _pRenderTargetView.GetAddressOf(), nullptr);
    const FLOAT clearColor[4] = { 0.f, 0.f, 0.f, 1.f };
    _context->ClearRenderTargetView(_pRenderTargetView.Get(), clearColor);

    D3D11_VIEWPORT vp;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = (FLOAT)_srcWidth;
    vp.Height = (FLOAT)_srcHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    _context->RSSetViewports(1, &vp);

    D3D11_BOX srcBox{
        0,
        0,
        0,
        _srcWidth,
        _srcHeight,
        1
    };

    _context->CopySubresourceRegion(
        _pBackBuffer.Get(),
        0,
        0,
        0,
        0,
        srcTexture.Get(),
        0,
        &srcBox
    );

    _swapChain->Present(0, 0);
}

void StreamingFrameRenderer::RenderSwapChainVideoBlt(ComPtr<ID3D11Texture2D> srcTexture)
{
    if (!_renderEnable)
    {
        return;
    }

    D3D11_TEXTURE2D_DESC srcDesc{};
    srcTexture->GetDesc(&srcDesc);

    if ((UINT32)((float)srcDesc.Width * _leftCropRatio) != _srcWidth || (UINT32)srcDesc.Height != _srcHeight)
    {
        _srcWidth = (UINT32)((float)srcDesc.Width * _leftCropRatio);
        _srcHeight = srcDesc.Height;

        _dstWidth = (UINT32)((float)srcDesc.Width * _leftCropRatio);
        _dstHeight = srcDesc.Height;
        //_dstWidth = srcDesc.Width;
        //_dstHeight = srcDesc.Height;

        _pRenderTargetView.Reset();
        _pBackBuffer.Reset();

        _swapChain->ResizeBuffers(2, _dstWidth, _dstHeight, DXGI_FORMAT_B8G8R8A8_UNORM, 0);

        _swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(_pBackBuffer.ReleaseAndGetAddressOf()));
        _device->CreateRenderTargetView(_pBackBuffer.Get(), nullptr, _pRenderTargetView.ReleaseAndGetAddressOf());

        _uiDispatcherQueue.TryEnqueue([this]()
            {
                _videoPanel.Width(_dstWidth);
                _videoPanel.Height(_dstHeight);
            });

        D3D11_TEXTURE2D_DESC bbDesc;
        _pBackBuffer->GetDesc(&bbDesc);

        if (_firstTime) {
            _firstTime = false;

            LogTextureAdapter(srcTexture.Get(), L"srcTexture");
            LogTextureAdapter(_pBackBuffer.Get(), L"dstTexture");
        }

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
        contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        contentDesc.InputWidth = _srcWidth;
        contentDesc.InputHeight = _srcHeight;
        contentDesc.OutputWidth = _dstWidth;
        contentDesc.OutputHeight = _dstHeight;

        HRESULT hr = _videoDev->CreateVideoProcessorEnumerator(
            &contentDesc,
            _procEnum.ReleaseAndGetAddressOf()
        );

        hr = _videoDev->CreateVideoProcessor(
            _procEnum.Get(),
            0,
            _videoProc.ReleaseAndGetAddressOf()
        );
    }

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivd{};
    ivd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    ivd.Texture2D.MipSlice = 0;
    ComPtr<ID3D11VideoProcessorInputView> inView;
    _videoDev->CreateVideoProcessorInputView(srcTexture.Get(), _procEnum.Get(), &ivd, inView.GetAddressOf());

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovd{};
    ovd.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ovd.Texture2D.MipSlice = 0;
    ComPtr<ID3D11VideoProcessorOutputView> outView;
    _videoDev->CreateVideoProcessorOutputView(_pBackBuffer.Get(), _procEnum.Get(), &ovd, outView.GetAddressOf());

    RECT srcRect = { 0, 0, (LONG)_srcWidth, (LONG)_srcHeight };
    RECT dstRect = { 0, 0, (LONG)_dstWidth, (LONG)_dstHeight };

    _videoCtx->VideoProcessorSetStreamSourceRect(_videoProc.Get(), 0, TRUE, &srcRect);
    _videoCtx->VideoProcessorSetStreamDestRect(_videoProc.Get(), 0, TRUE, &dstRect);
    _videoCtx->VideoProcessorSetStreamAlpha(_videoProc.Get(), 0, TRUE, 1.0f);

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.OutputIndex = 0;
    stream.InputFrameOrField = 0;
    stream.pInputSurface = inView.Get();

    ComPtr<ID3D11VideoContext> videoCtx;
    _context.As(&videoCtx);
    videoCtx->VideoProcessorBlt(_videoProc.Get(), outView.Get(), 0, 1, &stream);

    _swapChain->Present(0, 0);
}

void StreamingFrameRenderer::SetLogMessage(winrt::hstring const& message, int32_t logCode)
{
    winrt::MediaFramework::GstEventArgs args;
    args.EventType = winrt::MediaFramework::GstEventType::MessageLogged;
    args.OldState = winrt::MediaFramework::GstWinRTState::Uninitialized;
    args.NewState = winrt::MediaFramework::GstWinRTState::Uninitialized;
    args.Message = message;
    args.Code = logCode;

    if (_uiDispatcherQueue) {
        _uiDispatcherQueue.TryEnqueue([args = std::move(args), this]
            {
                if (_eventCallback)
                {
                    _eventCallback(args);
                }
            }
        );
    }
    else
    {
        if (_eventCallback)
        {
            _eventCallback(args);
        }
    }
}

void StreamingFrameRenderer::LogTextureAdapter(ID3D11Texture2D* texture, const wchar_t* label)
{
    ComPtr<IDXGIResource> dxgiRes;
    if (SUCCEEDED(texture->QueryInterface(__uuidof(IDXGIResource), &dxgiRes)))
    {
        ComPtr<IDXGIDevice> dxgiDev;
        if (SUCCEEDED(dxgiRes->GetDevice(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgiDev.GetAddressOf()))))
        {
            ComPtr<IDXGIAdapter> adapter;
            if (SUCCEEDED(dxgiDev->GetAdapter(&adapter)))
            {
                DXGI_ADAPTER_DESC desc;
                if (SUCCEEDED(adapter->GetDesc(&desc)))
                {
                    wchar_t buf[256];
                    swprintf_s(buf, L"%s is on GPU: %s", label, desc.Description);
                    SetLogMessage(buf, 3);
                    char ansibuffer[512];
                    int len = WideCharToMultiByte(CP_ACP, 0, buf, -1, ansibuffer, sizeof(buf), NULL, NULL);
                    if (len > 0) {
                        printf("%s\r\n", ansibuffer);
                    }
                }
            }
        }
    }
}