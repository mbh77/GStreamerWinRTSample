#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

struct FrameItem 
{
    ComPtr<ID3D11Texture2D> texture;
    int64_t pts;
};

class FrameQueue {
public:
    FrameQueue(size_t maxLength) : _maxLength(maxLength) {}

    void AddFrame(ComPtr<ID3D11Texture2D> texture, int64_t pts)
    {
        std::lock_guard<std::mutex> lock(_mutex);

        while (_queue.size() >= _maxLength) {
            _queue.pop();
        }

        _queue.push({ texture, pts });
        _cv.notify_one();
    }

    bool WaitAndPop(FrameItem& out, int timeoutMs = 100)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return !_queue.empty(); }))
        {
            out = _queue.front();
            _queue.pop();
            return true;
        }
        return false;
    }

    int Size()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return _queue.size();
    }

    void Flush()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::queue<FrameItem> empty;
        std::swap(_queue, empty);
    }

    void SleepHighPrecision(std::chrono::steady_clock::time_point targetTime)
    {
        using namespace std::chrono;
        auto now = steady_clock::now();
        auto earlyWakeTime = targetTime - milliseconds(1);

        if (now < earlyWakeTime)
        {
            HANDLE hTimer = CreateWaitableTimer(NULL, TRUE, NULL);
            if (!hTimer)
            {
                return;
            }

            auto waitNs = duration_cast<nanoseconds>(targetTime - now).count();

            LARGE_INTEGER liDueTime = {};
            liDueTime.QuadPart = -waitNs / 100;

            SetWaitableTimer(hTimer, &liDueTime, 0, NULL, NULL, FALSE);

            WaitForSingleObject(hTimer, 100);
            CloseHandle(hTimer);
        }

        while (steady_clock::now() < targetTime)
            std::this_thread::yield();
    }
private:
    std::queue<FrameItem> _queue;
    std::mutex _mutex;
    std::condition_variable _cv;
    size_t _maxLength;
};