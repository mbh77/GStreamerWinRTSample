using Microsoft.Extensions.DependencyInjection;
using System;

namespace MvpWuiMvvm.Modularity
{
    public interface IModule
    {
        void RegisterTypes(IServiceCollection services);
        void OnInitialized(IServiceProvider serviceProvider);
    }
}
