using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Services;
using System;

namespace Presentation
{
    public class StreamingViewModel : ObservableObject
    {
        private IStreamingService _streamingService;
        private UIElement _videoHost = null;
        private SwapChainPanel _swapChainPanel = null;

        private string _address = "127.0.0.1";
        private string _port = "5000";

        public string Address { get => _address; set => SetProperty(ref _address, value); }
        public string Port { get => _port; set => SetProperty(ref _port, value); }

        public StreamingViewModel(IStreamingService streamingService)
        {
            _streamingService = streamingService;
        }

        public void SetVideoHostAndPanel(UIElement host, SwapChainPanel panel)
        {
            _videoHost = host;
            _swapChainPanel = panel;
        }

        public void OnStartStreamingClicked(object sender, RoutedEventArgs e)
        {
            _streamingService.CreateSession(Address, UInt32.Parse(Port), _videoHost, _swapChainPanel);
            _streamingService.Start();
        }

        public void OnStopStreamingClicked(object sender, RoutedEventArgs e)
        {
            _streamingService.CloseSession();
        }
    }
}
