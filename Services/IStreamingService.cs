using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Services
{
    public interface IStreamingService
    {
        void CreateSession(string address, uint port, UIElement host, SwapChainPanel videoPanel);
        void Start();
        void Stop();
        void CloseSession();
    }
}
