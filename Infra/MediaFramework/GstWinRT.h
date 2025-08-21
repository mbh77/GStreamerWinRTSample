#pragma once
#include "GstWinRT.g.h"

#include <gst/gst.h>
#include <gst/app/app.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <wrl/client.h>
#include <glib.h>

namespace winrt::MediaFramework::implementation
{
    struct GstWinRT : GstWinRTT<GstWinRT>
    {
        GstWinRT(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& uiDispatcherQueue);
        ~GstWinRT();

        winrt::MediaFramework::GstWinRTPipeline CreatePipeline();
        void RemovePipeline(winrt::MediaFramework::GstWinRTPipeline const& pipeline);
        winrt::MediaFramework::GstWinRTPlayer CreatePlayer();
        void RemovePlayer(winrt::MediaFramework::GstWinRTPlayer const& player);

    private:
        winrt::Microsoft::UI::Dispatching::DispatcherQueue _uiDispatcherQueue{ nullptr };
    };
}
namespace winrt::MediaFramework::factory_implementation
{
    struct GstWinRT : GstWinRTT<GstWinRT, implementation::GstWinRT>
    {
    };
}