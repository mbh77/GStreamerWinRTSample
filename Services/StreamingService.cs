using MediaFramework;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Services
{
    internal class StreamingService : IStreamingService
    {
        private MediaFramework.GstWinRT _gstWinRT;
        public StreamingService()
        {
            var dispatcherQueue = DispatcherQueue.GetForCurrentThread();
            _gstWinRT = new MediaFramework.GstWinRT(dispatcherQueue);
        }

        public void CreateSession(string address, uint port, UIElement host, SwapChainPanel videoPanel)
        {
            
        }

        public void Start()
        {
            
        }
    }
}
