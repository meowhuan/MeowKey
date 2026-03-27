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

    public ManagerSnapshot Snapshot => ((App)Application.Current).Repository.Snapshot;

    private ManagerRepository Repository => ((App)Application.Current).Repository;

    private void OnRecordSnapshot(object sender, RoutedEventArgs e)
    {
        Repository.Refresh();
        Repository.RecordAction("Activity.Category.activity", "Action.Activity.RecordSnapshot");
        Frame.Navigate(typeof(ActivityPage));
    }

    private void OnPinLinuxNote(object sender, RoutedEventArgs e)
    {
        Repository.RecordAction("Activity.Category.activity", "Action.Activity.PinLinux");
    }

    private void ApplyLocalization()
    {
        PageTitleText.Text = _localizer["Page.Activity.Title"];
        PageDescriptionText.Text = _localizer["Page.Activity.Description"];
        RecordSnapshotButton.Content = _localizer["Page.Activity.Action.RecordSnapshot"];
        PinLinuxNoteButton.Content = _localizer["Page.Activity.Action.PinLinux"];
        AboutDetailsTitleText.Text = _localizer["Page.Activity.DetailsTitle"];
    }
}
