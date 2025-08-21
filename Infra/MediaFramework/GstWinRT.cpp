#include "pch.h"
#include "GstWinRT.h"
#include "GstWinRT.g.cpp"

#include <stdio.h>
using Microsoft::WRL::ComPtr;

namespace winrt::MediaFramework::implementation
{
    GstWinRT::GstWinRT(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& uiDispatcherQueue)
    {
        //FILE* fp;
        //freopen_s(&fp, "CONOUT$", "w", stdout);
        //freopen_s(&fp, "CONOUT$", "w", stderr);

        _uiDispatcherQueue = uiDispatcherQueue;
        gst_init(nullptr, nullptr);
        MFStartup(MF_VERSION);
    }

    GstWinRT::~GstWinRT()
    {
        OutputDebugString(L"[~GstWinRT] Called\n");
    }

    winrt::MediaFramework::GstWinRTPipeline GstWinRT::CreatePipeline()
    {
        auto factory = get_activation_factory<GstWinRTPipeline, IGstWinRTPipelineFactory>();
        winrt::MediaFramework::GstWinRTPipeline pipeline = factory.CreateInstance(_uiDispatcherQueue);
        return pipeline;
    }

    void GstWinRT::RemovePipeline(winrt::MediaFramework::GstWinRTPipeline const& pipeline)
    {
        pipeline.Close();
        return;
    }

    winrt::MediaFramework::GstWinRTPlayer GstWinRT::CreatePlayer()
    {
        auto factory = get_activation_factory<GstWinRTPlayer, IGstWinRTPlayerFactory>();
        winrt::MediaFramework::GstWinRTPlayer player = factory.CreateInstance(_uiDispatcherQueue);
        return player;
    }

    void GstWinRT::RemovePlayer(winrt::MediaFramework::GstWinRTPlayer const& player)
    {

    }
}
