#include "pch.h"
#include "Direct3DHelperWinRT.h"
#include "Direct3DHelperWinRT.g.cpp"

namespace winrt::MediaFramework::implementation
{
    Direct3DHelperWinRT::Direct3DHelperWinRT(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& uiDispatcherQueue)
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

        HRESULT hr = D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            levels, _countof(levels),
            D3D11_SDK_VERSION,
            _device.ReleaseAndGetAddressOf(), nullptr, _context.ReleaseAndGetAddressOf()
        );

        ComPtr<ID3D11Multithread> mt;
        if (SUCCEEDED(_context.As(&mt))) {
            mt->SetMultithreadProtected(TRUE);
        }

        if (FAILED(hr))
        {
            throw hresult_error(hr, L"Direct3DHelperWinRT: D3D11CreateDevice fail");
        }
    }

    void Direct3DHelperWinRT::Clear(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double red, double green, double blue, double alpha)
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        _device.As(&dxgiDevice);

        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        scDesc.Width = (UINT)panel.Width();
        scDesc.Height = (UINT)panel.Height();
        scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scDesc.Stereo = FALSE;
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = 2;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

        ComPtr<IDXGISwapChain1> swapChain;
        _factory->CreateSwapChainForComposition(dxgiDevice.Get(), &scDesc, nullptr, swapChain.ReleaseAndGetAddressOf());

        auto panelUnknown = reinterpret_cast<IUnknown*>(winrt::get_abi(panel));
        ComPtr<ISwapChainPanelNative> panelNative;
        HRESULT hr = panelUnknown->QueryInterface(__uuidof(ISwapChainPanelNative), (void**)panelNative.GetAddressOf());

        panelNative->SetSwapChain(nullptr);
        panelNative->SetSwapChain(swapChain.Get());

        ComPtr<ID3D11Texture2D> backBuffer;
        hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            throw hresult_error(hr, L"Clear: GetBuffer fail");
            return;
        }

        ComPtr<ID3D11RenderTargetView> rtv;
        hr = _device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtv.GetAddressOf());
        if (FAILED(hr) || !rtv)
        {
            throw hresult_error(hr, L"Clear: CreateRenderTargetView fail");
            return;
        }

        float clearColor[4] = {
            static_cast<float>(red),
            static_cast<float>(green),
            static_cast<float>(blue),
            static_cast<float>(alpha)
        };

        _context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        _context->ClearRenderTargetView(rtv.Get(), clearColor);

        hr = swapChain->Present(1, 0);
        if (FAILED(hr))
        {
            throw hresult_error(hr, L"Clear: Present fail");
        }
    }

    void Direct3DHelperWinRT::Dispose()
    {
        _context.Reset();
        _device.Reset();
        _factory.Reset();
    }
}