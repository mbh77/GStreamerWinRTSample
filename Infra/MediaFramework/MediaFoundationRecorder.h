#pragma once

#include <wrl/client.h>
#include <gst/app/gstappsink.h>

using Microsoft::WRL::ComPtr;

class MediaFoundationRecorder 
{
public:
    MediaFoundationRecorder();
    ~MediaFoundationRecorder();

    HRESULT Initialize(const std::wstring& outputPath,
        UINT32 width,
        UINT32 height,
        UINT32 fps = 60,
        UINT32 bitrate = 26000000);

    HRESULT Start();
    void    Stop();

    GstFlowReturn OnSample(GstSample* sample);

private:
    ComPtr<IMFSinkWriter> _sinkWriter;
    DWORD                 _streamIndex = 0;
    LONGLONG              _frameIndex = 0;
    LONGLONG              _lastFrameTime = 0;
    bool                  _isRecording = false;

    UINT32                _width = 0;
    UINT32                _height = 0;
    UINT32                _fps = 0;
};