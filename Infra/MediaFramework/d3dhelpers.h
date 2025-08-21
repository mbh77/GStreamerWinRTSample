#pragma once
#include <d3d11.h>
#include <wrl/client.h>

inline HRESULT
CreateD3DDevice(
    D3D_DRIVER_TYPE const type,
    Microsoft::WRL::ComPtr<ID3D11Device>& device)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    return D3D11CreateDevice(
        nullptr,
        type,
        nullptr,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        device.ReleaseAndGetAddressOf(),
        nullptr,
        nullptr);
}

inline Microsoft::WRL::ComPtr<ID3D11Device>
CreateD3DDevice()
{
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    HRESULT hr = CreateD3DDevice(D3D_DRIVER_TYPE_HARDWARE, device);

    if (DXGI_ERROR_UNSUPPORTED == hr)
    {
        hr = CreateD3DDevice(D3D_DRIVER_TYPE_WARP, device);
    }

    return device;
}