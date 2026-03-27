using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using MeowKey.Manager.Models;
using MeowKey.Manager.Services;

namespace MeowKey.Manager.Pages;

public sealed partial class DevicesPage : Page
{
    private readonly LocalizationService _localizer = LocalizationService.Current;

    public DevicesPage()
    {
        InitializeComponent();
        ApplyLocalization();
    }

    public ManagerSnapshot Snapshot => ((App)Application.Current).Repository.Snapshot;

    private ManagerRepository Repository => ((App)Application.Current).Repository;

    private void OnRefreshInventory(object sender, RoutedEventArgs e)
    {
        Repository.Refresh();
        Repository.RecordAction("Activity.Category.devices", "Action.Devices.Refresh");
        Frame.Navigate(typeof(DevicesPage));
    }

    private void OnQueueProbePass(object sender, RoutedEventArgs e)
    {
        Repository.RecordAction("Activity.Category.devices", "Action.Devices.Probe");
    }

    private void ApplyLocalization()
    {
        PageTitleText.Text = _localizer["Page.Devices.Title"];
        PageDescriptionText.Text = _localizer["Page.Devices.Description"];
        RefreshInventoryButton.Content = _localizer["Page.Devices.Action.Refresh"];
        QueueProbePassButton.Content = _localizer["Page.Devices.Action.Probe"];
        PoliciesTitleText.Text = _localizer["Page.Devices.PoliciesTitle"];
    }
}
