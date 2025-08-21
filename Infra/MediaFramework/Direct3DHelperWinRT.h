#pragma once
#include "Direct3DHelperWinRT.g.h"

using Microsoft::WRL::ComPtr;

namespace winrt::MediaFramework::implementation
{
    struct Direct3DHelperWinRT : Direct3DHelperWinRTT<Direct3DHelperWinRT>
    {
        Direct3DHelperWinRT(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& uiDispatcherQueue);
        void Clear(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double red, double green, double blue, double alpha);
        void Dispose();

    private:
        ComPtr<ID3D11Device> _device;
        ComPtr<ID3D11DeviceContext> _context;
        ComPtr<IDXGIFactory6> _factory;
    };
}
namespace winrt::MediaFramework::factory_implementation
{
    struct Direct3DHelperWinRT : Direct3DHelperWinRTT<Direct3DHelperWinRT, implementation::Direct3DHelperWinRT>
    {
    };
}