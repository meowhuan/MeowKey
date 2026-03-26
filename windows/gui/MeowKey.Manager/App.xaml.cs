using Microsoft.UI.Xaml;
using MeowKey.Manager.Services;

namespace MeowKey.Manager;

public partial class App : Application
{
    private Window? _window;

    public ManagerRepository Repository { get; } = new();

    public App()
    {
        Environment.SetEnvironmentVariable(
            "MICROSOFT_WINDOWSAPPRUNTIME_BASE_DIRECTORY",
            AppContext.BaseDirectory);
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _window = new MainWindow();
        _window.Activate();
    }
}
