using System.Collections.ObjectModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using MeowKey.Manager.Models;
using MeowKey.Manager.Services;

namespace MeowKey.Manager.Pages;

public sealed partial class MaintenancePage : Page
{
    private readonly LocalizationService _localizer = LocalizationService.Current;

    public MaintenancePage()
    {
        InitializeComponent();
        ApplyLocalization();
    }

    public ManagerSnapshot Snapshot => ((App)Application.Current).Repository.Snapshot;

    public ObservableCollection<ActivityEntry> ActivityEntries => ((App)Application.Current).Repository.ActivityEntries;

    private ManagerRepository Repository => ((App)Application.Current).Repository;

    private void OnPrepareReleaseCheck(object sender, RoutedEventArgs e)
    {
        Repository.Refresh();
        Repository.RecordAction("Activity.Category.debug", "Action.Debug.RefreshChannel");
        Frame.Navigate(typeof(MaintenancePage));
    }

    private void OnLogProbeReminder(object sender, RoutedEventArgs e)
    {
        Repository.RecordAction("Activity.Category.debug", "Action.Debug.LogNote");
    }

    private void ApplyLocalization()
    {
        PageTitleText.Text = _localizer["Page.Maintenance.Title"];
        PageDescriptionText.Text = _localizer["Page.Maintenance.Description"];
        PrepareReleaseCheckButton.Content = _localizer["Page.Maintenance.Action.PrepareRelease"];
        LogProbeReminderButton.Content = _localizer["Page.Maintenance.Action.ProbeReminder"];
        ToneTitleText.Text = _localizer["Page.Maintenance.ToneTitle"];
        ToneDescriptionText.Text = _localizer["Page.Maintenance.ToneDescription"];
        DebugLogTitleText.Text = _localizer["Page.Maintenance.LogTitle"];
    }
}
