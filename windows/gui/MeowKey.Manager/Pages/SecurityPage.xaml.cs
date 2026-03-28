using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System.Threading.Tasks;
using MeowKey.Manager.Models;
using MeowKey.Manager.Services;

namespace MeowKey.Manager.Pages;

public sealed partial class SecurityPage : Page
{
    private readonly LocalizationService _localizer = LocalizationService.Current;

    public SecurityPage()
    {
        InitializeComponent();
        ApplyLocalization();
        Loaded += OnLoaded;
    }

    public ManagerSnapshot Snapshot => ((App)Application.Current).Repository.Snapshot;

    private ManagerRepository Repository => ((App)Application.Current).Repository;

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        PopulateUserPresenceEditor();
    }

    private void OnRefreshSecurity(object sender, RoutedEventArgs e)
    {
        Repository.Refresh();
        Repository.RecordAction("Activity.Category.security", "Action.Security.RefreshState");
        Frame.Navigate(typeof(SecurityPage));
    }

    private void OnKeepDebugLimited(object sender, RoutedEventArgs e)
    {
        Repository.RecordAction("Activity.Category.security", "Action.Security.KeepDebugLimited");
    }

    private async void OnApplyPersistedUserPresence(object sender, RoutedEventArgs e)
    {
        if (!TryReadUserPresenceForm(out var config, out var error))
        {
            await ShowMessageAsync(_localizer["Page.Security.UpApply.InvalidTitle"], error);
            return;
        }

        if (Repository.ApplyUserPresenceConfig(config, persisted: true, out var applyError))
        {
            Repository.RecordAction("Activity.Category.security", "Action.Security.ApplyUpPersisted");
            Frame.Navigate(typeof(SecurityPage));
            return;
        }

        await ShowMessageAsync(_localizer["Page.Security.UpApply.FailedTitle"],
                               string.IsNullOrWhiteSpace(applyError) ? _localizer["Page.Security.UpApply.FailedMessage"] : applyError!);
    }

    private async void OnApplySessionUserPresence(object sender, RoutedEventArgs e)
    {
        if (!TryReadUserPresenceForm(out var config, out var error))
        {
            await ShowMessageAsync(_localizer["Page.Security.UpApply.InvalidTitle"], error);
            return;
        }

        if (Repository.ApplyUserPresenceConfig(config, persisted: false, out var applyError))
        {
            Repository.RecordAction("Activity.Category.security", "Action.Security.ApplyUpSession");
            Frame.Navigate(typeof(SecurityPage));
            return;
        }

        await ShowMessageAsync(_localizer["Page.Security.UpApply.FailedTitle"],
                               string.IsNullOrWhiteSpace(applyError) ? _localizer["Page.Security.UpApply.FailedMessage"] : applyError!);
    }

    private async void OnClearSessionOverride(object sender, RoutedEventArgs e)
    {
        if (Repository.ClearUserPresenceSessionOverride(out var clearError))
        {
            Repository.RecordAction("Activity.Category.security", "Action.Security.ClearUpSession");
            Frame.Navigate(typeof(SecurityPage));
            return;
        }

        await ShowMessageAsync(_localizer["Page.Security.UpApply.FailedTitle"],
                               string.IsNullOrWhiteSpace(clearError) ? _localizer["Page.Security.UpApply.FailedMessage"] : clearError!);
    }

    private void PopulateUserPresenceEditor()
    {
        var config = Snapshot.SelectedDevice?.SecurityState?.PersistedUserPresence
            ?? Snapshot.SelectedDevice?.SecurityState?.EffectiveUserPresence
            ?? new UserPresenceConfigInfo
            {
                Enabled = true,
                Source = "bootsel",
                GpioPin = -1,
                GpioActiveLow = true,
                TapCount = 2,
                GestureWindowMs = 750,
                RequestTimeoutMs = 8000
            };

        UpSourceCombo.SelectedIndex = config.Source.ToLowerInvariant() switch
        {
            "none" => 0,
            "gpio" => 2,
            _ => 1
        };
        UpGpioPinTextBox.Text = config.GpioPin.ToString();
        UpTapCountTextBox.Text = config.TapCount.ToString();
        UpGestureWindowTextBox.Text = config.GestureWindowMs.ToString();
        UpRequestTimeoutTextBox.Text = config.RequestTimeoutMs.ToString();
        UpActiveLowCheckBox.IsChecked = config.GpioActiveLow;
    }

    private bool TryReadUserPresenceForm(out UserPresenceConfigInfo config, out string error)
    {
        config = new UserPresenceConfigInfo();
        error = _localizer["Page.Security.UpApply.InvalidMessage"];

        var source = UpSourceCombo.SelectedIndex switch
        {
            0 => "none",
            2 => "gpio",
            _ => "bootsel"
        };

        if (!int.TryParse(UpGpioPinTextBox.Text?.Trim(), out var gpioPin))
        {
            error = _localizer["Page.Security.UpApply.InvalidGpio"];
            return false;
        }

        if (!int.TryParse(UpTapCountTextBox.Text?.Trim(), out var tapCount))
        {
            error = _localizer["Page.Security.UpApply.InvalidTap"];
            return false;
        }

        if (!int.TryParse(UpGestureWindowTextBox.Text?.Trim(), out var gestureWindowMs))
        {
            error = _localizer["Page.Security.UpApply.InvalidGesture"];
            return false;
        }

        if (!int.TryParse(UpRequestTimeoutTextBox.Text?.Trim(), out var requestTimeoutMs))
        {
            error = _localizer["Page.Security.UpApply.InvalidTimeout"];
            return false;
        }

        config = new UserPresenceConfigInfo
        {
            Enabled = source != "none",
            Source = source,
            GpioPin = gpioPin,
            GpioActiveLow = UpActiveLowCheckBox.IsChecked == true,
            TapCount = tapCount,
            GestureWindowMs = gestureWindowMs,
            RequestTimeoutMs = requestTimeoutMs
        };

        return true;
    }

    private async Task ShowMessageAsync(string title, string message)
    {
        var dialog = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = title,
            Content = message,
            CloseButtonText = _localizer["Dialog.Close"]
        };

        await dialog.ShowAsync();
    }

    private void ApplyLocalization()
    {
        PageTitleText.Text = _localizer["Page.Security.Title"];
        PageDescriptionText.Text = _localizer["Page.Security.Description"];
        RefreshSecurityButton.Content = _localizer["Page.Security.Action.Refresh"];
        KeepDebugLimitedButton.Content = _localizer["Page.Security.Action.KeepDebugLimited"];
        UserPresenceTitleText.Text = _localizer["Page.Security.UserPresenceTitle"];
        UserPresenceDescriptionText.Text = _localizer["Page.Security.UserPresenceDescription"];
        ManageUpTitleText.Text = _localizer["Page.Security.ManageUpTitle"];
        ManageUpDescriptionText.Text = _localizer["Page.Security.ManageUpDescription"];
        UpSourceLabelText.Text = _localizer["Page.Security.UpField.Source"];
        UpGpioPinLabelText.Text = _localizer["Page.Security.UpField.GpioPin"];
        UpTapCountLabelText.Text = _localizer["Page.Security.UpField.TapCount"];
        UpGestureWindowLabelText.Text = _localizer["Page.Security.UpField.GestureWindow"];
        UpRequestTimeoutLabelText.Text = _localizer["Page.Security.UpField.RequestTimeout"];
        UpActiveLowLabelText.Text = _localizer["Page.Security.UpField.ActiveLow"];
        UpSourceNoneItem.Content = _localizer["Page.Security.UpSource.None"];
        UpSourceBootselItem.Content = _localizer["Page.Security.UpSource.Bootsel"];
        UpSourceGpioItem.Content = _localizer["Page.Security.UpSource.Gpio"];
        ApplyPersistedUpButton.Content = _localizer["Page.Security.Action.ApplyUpPersisted"];
        ApplySessionUpButton.Content = _localizer["Page.Security.Action.ApplyUpSession"];
        ClearSessionOverrideButton.Content = _localizer["Page.Security.Action.ClearUpSession"];
        UpActiveLowCheckBox.Content = _localizer["Page.Security.UpField.ActiveLowCheckbox"];
        RecommendationTitleText.Text = _localizer["Page.Security.RecommendationTitle"];
        RecommendationDescriptionText.Text = _localizer["Page.Security.RecommendationDescription"];
    }
}
