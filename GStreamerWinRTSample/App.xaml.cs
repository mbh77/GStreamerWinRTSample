using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using MvpWuiMvvm;
using MvpWuiMvvm.Modularity;
using System;
using System.Collections.Generic;
using Presentation;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace GStreamerWinRTSample
{
    /// <summary>
    /// Provides application-specific behavior to supplement the default Application class.
    /// </summary>
    public partial class App : WuiMvvmApp
    {
        private Window? _window;

        /// <summary>
        /// Initializes the singleton application object.  This is the first line of authored code
        /// executed, and as such is the logical equivalent of main() or WinMain().
        /// </summary>
        public App() : base(new ServiceCollection())
        {
            InitializeComponent();
        }

        /// <summary>
        /// Invoked when the application is launched.
        /// </summary>
        /// <param name="args">Details about the launch request and process.</param>
        protected override void OnLaunched(Microsoft.UI.Xaml.LaunchActivatedEventArgs args)
        {
            base.OnLaunched(args);
        }

        protected override void ConfigureModuleCatalog(List<IModule> moduleCatalog)
        {
            moduleCatalog.Add(new PresentationModule());

            base.ConfigureModuleCatalog(moduleCatalog);
        }

        protected override void RegisterTypes(IServiceCollection services)
        {
            base.RegisterTypes(services);
        }

        protected override void OnInitialized(IServiceProvider serviceProvider)
        {
            _window = new MainWindow();
            _window.Activate();

            base.OnInitialized(serviceProvider);
        }
    }
}
