using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using MeowKey.Manager.Models;
using MeowKey.Manager.Services;

namespace MeowKey.Manager.Pages;

public sealed partial class DashboardPage : Page
{
    private readonly LocalizationService _localizer = LocalizationService.Current;

    public DashboardPage()
    {
        InitializeComponent();
        ApplyLocalization();
    }

    public ManagerSnapshot Snapshot => ((App)Application.Current).Repository.Snapshot;

    private ManagerRepository Repository => ((App)Application.Current).Repository;

    private void OnPreferHardenedBaseline(object sender, RoutedEventArgs e)
    {
        Repository.Refresh();
        Repository.RecordAction("Activity.Category.overview", "Action.Activity.RecordSnapshot");
        Frame.Navigate(typeof(DashboardPage));
    }

    private void OnConfirmLinuxSurface(object sender, RoutedEventArgs e)
    {
        Repository.RecordAction("Activity.Category.overview", "Action.Overview.ViewDevices");
        Frame.Navigate(typeof(DevicesPage));
    }

    private void ApplyLocalization()
    {
        PageTitleText.Text = _localizer["Page.Dashboard.Title"];
        PageDescription1Text.Text = _localizer["Page.Dashboard.Description1"];
        PageDescription2Text.Text = _localizer["Page.Dashboard.Description2"];
        PreferHardenedBaselineButton.Content = _localizer["Page.Dashboard.Action.RefreshDevice"];
        ConfirmLinuxSurfaceButton.Content = _localizer["Page.Dashboard.Action.ViewDevices"];
        OverviewFactsTitleText.Text = _localizer["Page.Dashboard.FactsTitle"];
        OverviewFactsDescriptionText.Text = _localizer["Page.Dashboard.FactsDescription"];
        ReadinessTitleText.Text = _localizer["Page.Dashboard.ReadinessTitle"];
        ReadinessDescriptionText.Text = _localizer["Page.Dashboard.ReadinessDescription"];
        PlatformTitleText.Text = _localizer["Page.Dashboard.PlatformTitle"];
        PlatformDescriptionText.Text = _localizer["Page.Dashboard.PlatformDescription"];
        BoundaryTitleText.Text = _localizer["Page.Dashboard.BoundaryTitle"];
        BoundaryDescriptionText.Text = _localizer["Page.Dashboard.BoundaryDescription"];
        NowTitleText.Text = _localizer["Page.Dashboard.NowTitle"];
        NowLine1Text.Text = _localizer["Page.Dashboard.NowLine1"];
        NowLine2Text.Text = _localizer["Page.Dashboard.NowLine2"];
        NowLine3Text.Text = _localizer["Page.Dashboard.NowLine3"];
        NextTitleText.Text = _localizer["Page.Dashboard.NextTitle"];
        NextLine1Text.Text = _localizer["Page.Dashboard.NextLine1"];
        NextLine2Text.Text = _localizer["Page.Dashboard.NextLine2"];
        NextLine3Text.Text = _localizer["Page.Dashboard.NextLine3"];
    }
}
