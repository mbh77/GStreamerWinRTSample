// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

using Microsoft.Extensions.DependencyInjection;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

using MvpWuiMvvm.Modularity;
using System;

namespace Services
{
    public partial class ServicesModule : IModule
    {
        public void RegisterTypes(IServiceCollection services)
        {
            services.AddSingleton<IStreamingService, StreamingService>();
        }

        public void OnInitialized(IServiceProvider serviceProvider)
        {
            
        }
    }
}
