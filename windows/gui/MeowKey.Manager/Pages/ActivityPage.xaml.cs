using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using MeowKey.Manager.Models;
using MeowKey.Manager.Services;
using System.Diagnostics;

namespace MeowKey.Manager.Pages;

public sealed partial class ActivityPage : Page
{
    private readonly LocalizationService _localizer = LocalizationService.Current;
    private readonly UpdatePreferencesStore _updatePreferencesStore = new();
    private readonly ReleaseUpdateService _releaseUpdateService = new();
    private UpdatePreferences _updatePreferences = new();
    private bool _initializingUpdateControls;
    private string? _managerDownloadUrl;
    private string? _managerNotesUrl;
    private string? _firmwareDownloadUrl;
    private string? _firmwareNotesUrl;

    public ActivityPage()
    {
        InitializeComponent();
        _updatePreferences = _updatePreferencesStore.Load();
        ApplyLocalization();
        InitializeUpdateSection();
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

    private void OnSaveManifestUrl(object sender, RoutedEventArgs e)
    {
        SaveUpdatePreferences();
        Repository.RecordAction("Activity.Category.activity", "Action.Activity.SaveManifestUrl");
        Frame.Navigate(typeof(ActivityPage));
    }

    private async void OnCheckUpdates(object sender, RoutedEventArgs e)
    {
        SaveUpdatePreferences();
        SetUpdateButtonsEnabled(false);

        try
        {
            var request = new UpdateCheckRequest
            {
                ManifestUrl = _updatePreferences.ManifestUrl,
                AppTrack = _updatePreferences.AppTrack,
                FirmwareTrack = _updatePreferences.FirmwareTrack,
                CurrentAppVersion = Snapshot.VersionLabel,
                CurrentFirmwareVersion = Snapshot.SelectedDevice?.FirmwareVersion ?? string.Empty
            };
            var result = await _releaseUpdateService.CheckAsync(request);
            BindUpdateResult(result);
            Repository.RecordAction("Activity.Category.activity", "Action.Activity.CheckUpdates");
        }
        catch (Exception ex)
        {
            var errorState = _localizer["Page.Activity.Update.State.Error"];
            ManagerUpdateStateText.Text = $"{errorState}: {ex.Message}";
            FirmwareUpdateStateText.Text = $"{errorState}: {ex.Message}";
            LastCheckedText.Text = string.Format(
                _localizer["Page.Activity.Update.LastChecked.Format"],
                DateTime.Now.ToString("g"));
        }
        finally
        {
            SetUpdateButtonsEnabled(true);
        }
    }

    private void OnAppPreviewToggled(object sender, RoutedEventArgs e)
    {
        if (_initializingUpdateControls)
        {
            return;
        }

        _updatePreferences.AppTrack = AppPreviewToggle.IsOn ? "preview" : "stable";
        SaveUpdatePreferences();
        Repository.RecordAction("Activity.Category.activity", "Action.Activity.ToggleAppPreview");
        UpdateTrackSwitchLabels();
    }

    private void OnFirmwarePreviewToggled(object sender, RoutedEventArgs e)
    {
        if (_initializingUpdateControls)
        {
            return;
        }

        _updatePreferences.FirmwareTrack = FirmwarePreviewToggle.IsOn ? "preview" : "stable";
        SaveUpdatePreferences();
        Repository.RecordAction("Activity.Category.activity", "Action.Activity.ToggleFirmwarePreview");
        UpdateTrackSwitchLabels();
    }

    private void OnOpenManagerDownload(object sender, RoutedEventArgs e)
    {
        OpenUrl(_managerDownloadUrl);
    }

    private void OnOpenManagerNotes(object sender, RoutedEventArgs e)
    {
        OpenUrl(_managerNotesUrl);
    }

    private void OnOpenFirmwareDownload(object sender, RoutedEventArgs e)
    {
        OpenUrl(_firmwareDownloadUrl);
    }

    private void OnOpenFirmwareNotes(object sender, RoutedEventArgs e)
    {
        OpenUrl(_firmwareNotesUrl);
    }

    private void InitializeUpdateSection()
    {
        _initializingUpdateControls = true;
        ManifestUrlTextBox.Text = _updatePreferences.ManifestUrl;
        AppPreviewToggle.IsOn = string.Equals(_updatePreferences.AppTrack, "preview", StringComparison.OrdinalIgnoreCase);
        FirmwarePreviewToggle.IsOn = string.Equals(_updatePreferences.FirmwareTrack, "preview", StringComparison.OrdinalIgnoreCase);
        _initializingUpdateControls = false;
        UpdateTrackSwitchLabels();

        var unknown = _localizer["Page.Activity.Update.State.Unknown"];
        ManagerUpdateStateText.Text = unknown;
        FirmwareUpdateStateText.Text = unknown;
        ManagerUpdateCurrentText.Text = string.Format(_localizer["Page.Activity.Update.Current"], Snapshot.VersionLabel);
        FirmwareUpdateCurrentText.Text = string.Format(
            _localizer["Page.Activity.Update.Current"],
            Snapshot.SelectedDevice?.FirmwareVersion ?? _localizer["Summary.Value.Unknown"]);
        ManagerUpdateLatestText.Text = string.Format(_localizer["Page.Activity.Update.Latest"], "-");
        FirmwareUpdateLatestText.Text = string.Format(_localizer["Page.Activity.Update.Latest"], "-");
        LastCheckedText.Text = _localizer["Page.Activity.Update.LastChecked.Unknown"];
        ManifestSourceText.Text = string.Format(_localizer["Page.Activity.Update.Source"], _updatePreferences.ManifestUrl);
    }

    private void SaveUpdatePreferences()
    {
        _updatePreferences.ManifestUrl = ManifestUrlTextBox.Text.Trim();
        _updatePreferencesStore.Save(_updatePreferences);
        ManifestSourceText.Text = string.Format(_localizer["Page.Activity.Update.Source"], _updatePreferences.ManifestUrl);
    }

    private void UpdateTrackSwitchLabels()
    {
        var appTrack = string.Equals(_updatePreferences.AppTrack, "preview", StringComparison.OrdinalIgnoreCase)
            ? _localizer["Page.Activity.Update.TrackPreview"]
            : _localizer["Page.Activity.Update.TrackStable"];
        var firmwareTrack = string.Equals(_updatePreferences.FirmwareTrack, "preview", StringComparison.OrdinalIgnoreCase)
            ? _localizer["Page.Activity.Update.TrackPreview"]
            : _localizer["Page.Activity.Update.TrackStable"];

        AppPreviewToggle.Header = _localizer["Page.Activity.Update.AppPreview"];
        AppPreviewToggle.OffContent = appTrack;
        AppPreviewToggle.OnContent = appTrack;
        FirmwarePreviewToggle.Header = _localizer["Page.Activity.Update.FirmwarePreview"];
        FirmwarePreviewToggle.OffContent = firmwareTrack;
        FirmwarePreviewToggle.OnContent = firmwareTrack;
    }

    private void BindUpdateResult(UpdateCheckResult result)
    {
        BindTarget(result.ManagerWinui, isManager: true);
        BindTarget(result.Firmware, isManager: false);

        LastCheckedText.Text = string.Format(
            _localizer["Page.Activity.Update.LastChecked.Format"],
            DateTime.Now.ToString("g"));
        ManifestSourceText.Text = string.Format(_localizer["Page.Activity.Update.Source"], result.ManifestUrl);
    }

    private void BindTarget(UpdateTargetResult target, bool isManager)
    {
        var status = target.State switch
        {
            UpdateState.UpToDate => _localizer["Page.Activity.Update.State.UpToDate"],
            UpdateState.UpdateAvailable => _localizer["Page.Activity.Update.State.UpdateAvailable"],
            UpdateState.NotConfigured => _localizer["Page.Activity.Update.State.NotConfigured"],
            UpdateState.NoDevice => _localizer["Page.Activity.Update.State.NoDevice"],
            _ => _localizer["Page.Activity.Update.State.Unknown"]
        };

        var current = string.IsNullOrWhiteSpace(target.CurrentVersion)
            ? _localizer["Summary.Value.Unknown"]
            : target.CurrentVersion;
        var latest = string.IsNullOrWhiteSpace(target.LatestVersion)
            ? "-"
            : target.LatestVersion;

        if (isManager)
        {
            ManagerUpdateStateText.Text = status;
            ManagerUpdateCurrentText.Text = string.Format(_localizer["Page.Activity.Update.Current"], current);
            ManagerUpdateLatestText.Text = string.Format(_localizer["Page.Activity.Update.Latest"], latest);
            _managerDownloadUrl = target.DownloadUrl;
            _managerNotesUrl = target.NotesUrl;
            OpenManagerDownloadButton.IsEnabled = !string.IsNullOrWhiteSpace(_managerDownloadUrl);
            OpenManagerNotesButton.IsEnabled = !string.IsNullOrWhiteSpace(_managerNotesUrl);
            return;
        }

        FirmwareUpdateStateText.Text = status;
        FirmwareUpdateCurrentText.Text = string.Format(_localizer["Page.Activity.Update.Current"], current);
        FirmwareUpdateLatestText.Text = string.Format(_localizer["Page.Activity.Update.Latest"], latest);
        _firmwareDownloadUrl = target.DownloadUrl;
        _firmwareNotesUrl = target.NotesUrl;
        OpenFirmwareDownloadButton.IsEnabled = !string.IsNullOrWhiteSpace(_firmwareDownloadUrl);
        OpenFirmwareNotesButton.IsEnabled = !string.IsNullOrWhiteSpace(_firmwareNotesUrl);
    }

    private void SetUpdateButtonsEnabled(bool enabled)
    {
        SaveManifestUrlButton.IsEnabled = enabled;
        CheckUpdatesButton.IsEnabled = enabled;
        AppPreviewToggle.IsEnabled = enabled;
        FirmwarePreviewToggle.IsEnabled = enabled;
    }

    private void OpenUrl(string? url)
    {
        if (string.IsNullOrWhiteSpace(url))
        {
            return;
        }

        try
        {
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
        }
        catch
        {
            // Ignore shell-open failures.
        }
    }

    private void ApplyLocalization()
    {
        PageTitleText.Text = _localizer["Page.Activity.Title"];
        PageDescriptionText.Text = _localizer["Page.Activity.Description"];
        RecordSnapshotButton.Content = _localizer["Page.Activity.Action.RecordSnapshot"];
        PinLinuxNoteButton.Content = _localizer["Page.Activity.Action.PinLinux"];
        AboutDetailsTitleText.Text = _localizer["Page.Activity.DetailsTitle"];
        UpdateTitleText.Text = _localizer["Page.Activity.UpdateTitle"];
        UpdateDescriptionText.Text = _localizer["Page.Activity.UpdateDescription"];
        ManifestUrlLabelText.Text = _localizer["Page.Activity.Update.ManifestUrl"];
        SaveManifestUrlButton.Content = _localizer["Page.Activity.Update.Action.SaveManifest"];
        CheckUpdatesButton.Content = _localizer["Page.Activity.Update.Action.Check"];
        ManagerUpdateTitleText.Text = _localizer["Page.Activity.Update.ManagerTitle"];
        FirmwareUpdateTitleText.Text = _localizer["Page.Activity.Update.FirmwareTitle"];
        OpenManagerDownloadButton.Content = _localizer["Page.Activity.Update.OpenDownload"];
        OpenManagerNotesButton.Content = _localizer["Page.Activity.Update.OpenNotes"];
        OpenFirmwareDownloadButton.Content = _localizer["Page.Activity.Update.OpenDownload"];
        OpenFirmwareNotesButton.Content = _localizer["Page.Activity.Update.OpenNotes"];
    }
}
