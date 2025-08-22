using MediaFramework;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Services
{
    internal class StreamingService : IStreamingService
    {
        private GstWinRT _gstWinRT;
        private GstWinRTPipeline _pipeline;
        public StreamingService()
        {
            var dispatcherQueue = DispatcherQueue.GetForCurrentThread();
            _gstWinRT = new GstWinRT(dispatcherQueue);
        }

        public void CreateSession(string address, uint port, UIElement host, SwapChainPanel videoPanel)
        {
            var config = new StreamerData
            {
                SrcVideoIP = address,
                SrcVideoPort = port,
                RecordPort = 6000,
                FramerateMode = false,
                Framerate = 60,
                PipelineMode = false,
                PipelineDescription = ""
            };

            if(_pipeline != null)
            {
                CloseSession();
            }

            if(_gstWinRT != null)
            {
                _pipeline = _gstWinRT.CreatePipeline();
                if (_pipeline != null)
                {
                    _pipeline.Initialize(config, host, videoPanel);
                }
            }
        }

        public void Start()
        {
            if (_pipeline != null)
            {
                _pipeline.Start();
            }
        }

        public void Stop()
        {
            if (_pipeline != null)
            {
                _pipeline.Stop();
            }
        }

        public void CloseSession()
        {
            if (_pipeline != null)
            {
                _pipeline.Stop();
                _pipeline.Close();
                _pipeline = null;
            }
        }
    }
}
