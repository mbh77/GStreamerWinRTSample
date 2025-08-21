// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

using Microsoft.Extensions.DependencyInjection;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

using MvpWuiMvvm.Modularity;
using System;

namespace Presentation
{
    public class PresentationModule : IModule
    {
        public void RegisterTypes(IServiceCollection services)
        {
            services.AddSingleton<StreamingViewModel>();
        }

        public void OnInitialized(IServiceProvider serviceProvider)
        {

        }
    }
}
