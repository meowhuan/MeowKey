using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using MeowKey.Manager.Models;
using MeowKey.Manager.Pages;
using MeowKey.Manager.Services;
using Windows.Graphics;
using WinRT.Interop;

namespace MeowKey.Manager;

public sealed partial class MainWindow : Window
{
    private sealed record SectionDefinition(string Key, string Title, string Subtitle, Type PageType);

    private readonly LocalizationService _localizer = LocalizationService.Current;
    private readonly ManagerRepository _repository = ((App)Application.Current).Repository;
    private readonly IReadOnlyDictionary<string, SectionDefinition> _sections;
    private bool _windowChromeInitialized;

    public MainWindow()
    {
        _sections = new Dictionary<string, SectionDefinition>(StringComparer.OrdinalIgnoreCase)
        {
            ["overview"] = new("overview", _localizer["Section.Overview.Title"], _localizer["Section.Overview.Subtitle"], typeof(DashboardPage)),
            ["devices"] = new("devices", _localizer["Section.Devices.Title"], _localizer["Section.Devices.Subtitle"], typeof(DevicesPage)),
            ["credentials"] = new("credentials", _localizer["Section.Credentials.Title"], _localizer["Section.Credentials.Subtitle"], typeof(CredentialsPage)),
            ["security"] = new("security", _localizer["Section.Security.Title"], _localizer["Section.Security.Subtitle"], typeof(SecurityPage)),
            ["maintenance"] = new("maintenance", _localizer["Section.Maintenance.Title"], _localizer["Section.Maintenance.Subtitle"], typeof(MaintenancePage)),
            ["activity"] = new("activity", _localizer["Section.Activity.Title"], _localizer["Section.Activity.Subtitle"], typeof(ActivityPage))
        };

        InitializeComponent();
        ApplyStaticLocalization();
        TryApplyWindowBackdrop();
        Activated += OnWindowActivated;
        TryResizeWindow(1360, 900);
        ApplySnapshot(_repository.Snapshot);
        ShowSection("overview");
    }

    private void ApplySnapshot(ManagerSnapshot snapshot)
    {
        Title = _localizer["App.WindowTitle"];
        TitleBarAppNameText.Text = snapshot.ProductName;
        TitleBarSubtitleText.Text = snapshot.WindowSubtitle;
        TitleBarVersionChipText.Text = snapshot.VersionLabel;
        TitleBarChannelChipText.Text = snapshot.ChannelLabel;
        BuildVersionText.Text = snapshot.VersionLabel;
        BuildChannelText.Text = snapshot.ChannelLabel;
        WindowsSurfaceText.Text = snapshot.WindowsSurface;
        LinuxSurfaceText.Text = snapshot.LinuxSurface;

        if (snapshot.HeaderSummaries.Count >= 4)
        {
            ApplySummaryCard(snapshot.HeaderSummaries[0], SummaryCard1LabelText, SummaryCard1ValueText);
            ApplySummaryCard(snapshot.HeaderSummaries[1], SummaryCard2LabelText, SummaryCard2ValueText);
            ApplySummaryCard(snapshot.HeaderSummaries[2], SummaryCard3LabelText, SummaryCard3ValueText);
            ApplySummaryCard(snapshot.HeaderSummaries[3], SummaryCard4LabelText, SummaryCard4ValueText);
        }
    }

    private void ApplyStaticLocalization()
    {
        OverviewSectionButtonText.Text = _localizer["Nav.Overview"];
        DevicesSectionButtonText.Text = _localizer["Nav.Devices"];
        CredentialsSectionButtonText.Text = _localizer["Nav.Credentials"];
        SecuritySectionButtonText.Text = _localizer["Nav.Security"];
        MaintenanceSectionButtonText.Text = _localizer["Nav.Maintenance"];
        ActivitySectionButtonText.Text = _localizer["Nav.Activity"];
        WindowsSurfaceLabelText.Text = _localizer["App.WindowsLabel"];
        LinuxSurfaceLabelText.Text = _localizer["App.LinuxLabel"];
    }

    private static void ApplySummaryCard(SummaryCard card, TextBlock labelText, TextBlock valueText)
    {
        labelText.Text = card.Label;
        valueText.Text = card.Value;
    }

    private void OnNavigate(object sender, RoutedEventArgs e)
    {
        if (sender is Button button && button.Tag is string key)
        {
            ShowSection(key);
        }
    }

    private void ShowSection(string key)
    {
        if (!_sections.TryGetValue(key, out var section))
        {
            return;
        }

        ContentFrame.Navigate(section.PageType);
        CurrentSectionTitleText.Text = section.Title;
        CurrentSectionSubtitleText.Text = section.Subtitle;
        ApplyActiveNavState(key);
    }

    private void ApplyActiveNavState(string activeKey)
    {
        SetNavButtonState(OverviewSectionButton, activeKey == "overview");
        SetNavButtonState(DevicesSectionButton, activeKey == "devices");
        SetNavButtonState(CredentialsSectionButton, activeKey == "credentials");
        SetNavButtonState(SecuritySectionButton, activeKey == "security");
        SetNavButtonState(MaintenanceSectionButton, activeKey == "maintenance");
        SetNavButtonState(ActivitySectionButton, activeKey == "activity");
    }

    private void SetNavButtonState(Button button, bool active)
    {
        button.Background = ResolveBrush(active ? "AppNavActiveBrush" : "AppNavInactiveBrush");
        button.BorderBrush = ResolveBrush("AppCardBorderBrush");
        button.Foreground = ResolveBrush(active ? "AppStrongForegroundBrush" : "AppTextForegroundBrush");
    }

    private Brush ResolveBrush(string key)
    {
        if (Application.Current.Resources.TryGetValue(key, out var value) && value is Brush brush)
        {
            return brush;
        }

        return new SolidColorBrush(Colors.Transparent);
    }

    private void OnWindowActivated(object sender, WindowActivatedEventArgs args)
    {
        if (_windowChromeInitialized)
        {
            return;
        }

        _windowChromeInitialized = true;
        TryConfigureWindowChrome();
    }

    private void TryApplyWindowBackdrop()
    {
        try
        {
            SystemBackdrop = new MicaBackdrop();
        }
        catch
        {
            // Ignore backdrop failures on older runtimes.
        }
    }

    private void TryConfigureWindowChrome()
    {
        try
        {
            ExtendsContentIntoTitleBar = true;
            SetTitleBar(WindowTitleBar);

            var hwnd = WindowNative.GetWindowHandle(this);
            var windowId = Microsoft.UI.Win32Interop.GetWindowIdFromWindow(hwnd);
            var appWindow = AppWindow.GetFromWindowId(windowId);
            if (appWindow != null && AppWindowTitleBar.IsCustomizationSupported())
            {
                var titleBar = appWindow.TitleBar;
                titleBar.ButtonBackgroundColor = Colors.Transparent;
                titleBar.ButtonInactiveBackgroundColor = Colors.Transparent;
                titleBar.ButtonHoverBackgroundColor = ColorHelper.FromArgb(36, 0, 103, 192);
                titleBar.ButtonPressedBackgroundColor = ColorHelper.FromArgb(64, 0, 103, 192);
                titleBar.PreferredHeightOption = TitleBarHeightOption.Tall;
                UpdateTitleBarInsets(titleBar);
            }
        }
        catch
        {
            // Ignore window chrome setup failures on unsupported environments.
        }
    }

    private void UpdateTitleBarInsets(AppWindowTitleBar titleBar)
    {
        try
        {
            var scale = Content.XamlRoot?.RasterizationScale ?? 1.0;
            TitleBarLeftInsetColumn.Width = new GridLength(titleBar.LeftInset / scale);
            TitleBarRightInsetColumn.Width = new GridLength(titleBar.RightInset / scale);
        }
        catch
        {
            // Ignore inset refresh failures.
        }
    }

    private void TryResizeWindow(int width, int height)
    {
        try
        {
            var hwnd = WindowNative.GetWindowHandle(this);
            var windowId = Microsoft.UI.Win32Interop.GetWindowIdFromWindow(hwnd);
            var appWindow = AppWindow.GetFromWindowId(windowId);
            appWindow?.Resize(new SizeInt32(width, height));
        }
        catch
        {
            // Ignore resize failures on older environments.
        }
    }
}
