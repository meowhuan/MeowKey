using System.Collections.ObjectModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using MeowKey.Manager.Models;
using MeowKey.Manager.Services;

namespace MeowKey.Manager.Pages;

public sealed partial class ActivityPage : Page
{
    private readonly LocalizationService _localizer = LocalizationService.Current;

    public ActivityPage()
    {
        InitializeComponent();
        ApplyLocalization();
    }

    public ObservableCollection<ActivityEntry> ActivityEntries => ((App)Application.Current).Repository.ActivityEntries;

    private ManagerRepository Repository => ((App)Application.Current).Repository;

    private void OnRecordSnapshot(object sender, RoutedEventArgs e)
    {
        Repository.RecordAction("Activity.Category.activity", "Action.Activity.RecordSnapshot");
    }

    private void OnPinLinuxNote(object sender, RoutedEventArgs e)
    {
        Repository.RecordAction("Activity.Category.platform", "Action.Activity.PinLinux");
    }

    private void ApplyLocalization()
    {
        PageTitleText.Text = _localizer["Page.Activity.Title"];
        PageDescriptionText.Text = _localizer["Page.Activity.Description"];
        RecordSnapshotButton.Content = _localizer["Page.Activity.Action.RecordSnapshot"];
        PinLinuxNoteButton.Content = _localizer["Page.Activity.Action.PinLinux"];
    }
}
