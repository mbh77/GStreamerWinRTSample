using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using MvpWuiMvvm.Modularity;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace MvpWuiMvvm
{
    public partial class WuiMvvmApp : Application
    {
        public static IServiceProvider ServiceProvider { get; private set; }
        protected IServiceCollection Services;
        protected List<IModule> ModuleCatalog = new();
        public Window m_window;

        public WuiMvvmApp(IServiceCollection services)
        {
            Services = services;
        }

        protected override void OnLaunched(LaunchActivatedEventArgs args)
        {
            base.OnLaunched(args);

            ConfigureModuleCatalog(ModuleCatalog);

            RegisterTypes(Services);
            ServiceProvider = Services.BuildServiceProvider();
            Ioc.Default.ConfigureServices(ServiceProvider);

            OnInitialized(ServiceProvider);
        }

        protected virtual void ConfigureModuleCatalog(List<IModule> moduleCatalog)
        {
        }

        protected virtual void RegisterTypes(IServiceCollection services)
        {
            foreach (var module in ModuleCatalog)
            {
                module.RegisterTypes(services);
            }
        }

        protected virtual void OnInitialized(IServiceProvider serviceProvider)
        {
            foreach (var module in ModuleCatalog)
            {
                module.OnInitialized(serviceProvider);
            }
        }
    }
}
