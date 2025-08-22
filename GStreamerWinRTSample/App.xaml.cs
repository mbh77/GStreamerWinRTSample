using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using MvpWuiMvvm;
using MvpWuiMvvm.Modularity;
using Presentation;
using Services;
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace GStreamerWinRTSample
{
    /// <summary>
    /// Provides application-specific behavior to supplement the default Application class.
    /// </summary>

    public partial class App : WuiMvvmApp
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool SetDllDirectory(string lpPathName);

        private Window? _window;

        /// <summary>
        /// Initializes the singleton application object.  This is the first line of authored code
        /// executed, and as such is the logical equivalent of main() or WinMain().
        /// </summary>
        public App() : base(new ServiceCollection())
        {
            var exeFolder = System.IO.Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location);
            if (exeFolder != null)
            {
                var gstreamerDir = System.IO.Path.Combine(exeFolder, "gstreamer\\bin");
                SetDllDirectory(gstreamerDir);

                var gstLibPath = System.IO.Path.GetFullPath(System.IO.Path.Combine(exeFolder, "gstreamer\\lib\\gstreamer-1.0"));
                Environment.SetEnvironmentVariable("GST_PLUGIN_PATH", gstLibPath, EnvironmentVariableTarget.Process);

                var path = Environment.GetEnvironmentVariable("PATH", EnvironmentVariableTarget.Process);
                var gstBinPath = System.IO.Path.GetFullPath(System.IO.Path.Combine(exeFolder, "gstreamer\\bin"));
                Environment.SetEnvironmentVariable("PATH", $"{gstBinPath};{path}", EnvironmentVariableTarget.Process);
            }

            var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            var regDir = System.IO.Path.Combine(local, "GStreamerWinRTSample", "gstreamer", "1.0");
            Directory.CreateDirectory(regDir);
            var regFile = Path.Combine(regDir, $"registry_{Environment.ProcessId}.bin");
            Environment.SetEnvironmentVariable("GST_REGISTRY", regFile, EnvironmentVariableTarget.Process);

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
            moduleCatalog.Add(new ServicesModule());

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
