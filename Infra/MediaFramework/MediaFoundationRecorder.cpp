#include "pch.h"
#include "MediaFoundationRecorder.h"

MediaFoundationRecorder::MediaFoundationRecorder() {}

MediaFoundationRecorder::~MediaFoundationRecorder() {
    Stop();
}

HRESULT MediaFoundationRecorder::Initialize(const std::wstring& outputPath, UINT32 width, UINT32 height, UINT32 fps,UINT32 bitrate)
{
    _width = width;
    _height = height;
    _fps = fps;

    HRESULT hr = MFCreateSinkWriterFromURL(outputPath.c_str(), nullptr, nullptr, &_sinkWriter);
    if (FAILED(hr))
    {
        return hr;
    }

    ComPtr<IMFMediaType> outputType;
    MFCreateMediaType(&outputType);
    outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    outputType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

    hr = _sinkWriter->AddStream(outputType.Get(), &_streamIndex);
    if (FAILED(hr)) 
    { 
        return hr;
    }

    ComPtr<IMFMediaType> inputType;
    MFCreateMediaType(&inputType);
    inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

    hr = _sinkWriter->SetInputMediaType(_streamIndex, inputType.Get(), nullptr);
    return hr;
}

HRESULT MediaFoundationRecorder::Start() {
    if (!_sinkWriter)
    {
        return E_FAIL;
    }

    HRESULT hr = _sinkWriter->BeginWriting();
    if (SUCCEEDED(hr)) 
    {
        _isRecording = true;
        _frameIndex = 0;
        _lastFrameTime = MFGetSystemTime();
    }

    return hr;
}

void MediaFoundationRecorder::Stop() {
    if (_sinkWriter && _isRecording) {
        _sinkWriter->Finalize();
    }
    _isRecording = false;
}

GstFlowReturn MediaFoundationRecorder::OnSample(GstSample* sample) {
    if (!_isRecording || !_sinkWriter)
    {
        return GST_FLOW_OK;
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo mapInfo = {};
    if (!gst_buffer_map(buffer, &mapInfo, GST_MAP_READ))
    {
        return GST_FLOW_OK;
    }

    ComPtr<IMFMediaBuffer> mediaBuffer;
    HRESULT hr = MFCreateMemoryBuffer((DWORD)mapInfo.size, mediaBuffer.GetAddressOf());
    if (SUCCEEDED(hr))
    {
        BYTE* dest = nullptr;
        DWORD maxLen = 0;
        hr = mediaBuffer->Lock(&dest, nullptr, &maxLen);
        if (SUCCEEDED(hr))
        {
            memcpy(dest, mapInfo.data, mapInfo.size);
            mediaBuffer->Unlock();
            mediaBuffer->SetCurrentLength((DWORD)mapInfo.size);
        }
    }
    gst_buffer_unmap(buffer, &mapInfo);

    if (!mediaBuffer) 
    {
        OutputDebugStringW(L"mediaBuffer is null!\n");
    }

    ComPtr<IMFSample> mfSample;
    MFCreateSample(mfSample.GetAddressOf());
    mfSample->AddBuffer(mediaBuffer.Get());

    if (!mfSample)
    {
        OutputDebugStringW(L"mfSample is null!\n");
    }

    LONGLONG now = MFGetSystemTime();
    LONGLONG frameDuration = 10'000'000 / _fps;
    LONGLONG delta = now - _lastFrameTime;

    if (delta > 2 * frameDuration)
    {
        _frameIndex += delta / frameDuration;
    }
    else 
    {
        _frameIndex++;
    }

    _lastFrameTime = now;

    mfSample->SetSampleTime(_frameIndex * frameDuration);
    mfSample->SetSampleDuration(frameDuration);

    hr = _sinkWriter->WriteSample(_streamIndex, mfSample.Get());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"WriteSample failed\n");
    }

    return GST_FLOW_OK;
}