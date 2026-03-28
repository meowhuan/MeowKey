using System.Collections.ObjectModel;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using MeowKey.Manager.Models;
using MeowKey.Manager.Services;

namespace MeowKey.Manager.Pages;

public sealed partial class CredentialsPage : Page
{
    private readonly LocalizationService _localizer = LocalizationService.Current;
    private readonly DispatcherTimer _authorizationTimer = new() { Interval = TimeSpan.FromSeconds(1) };

    public CredentialsPage()
    {
        InitializeComponent();
        ApplyLocalization();
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
        _authorizationTimer.Tick += (_, _) => UpdateAuthorizationCountdown();
    }

    public ManagerSnapshot Snapshot => ((App)Application.Current).Repository.Snapshot;

    public ObservableCollection<CredentialCatalogItem> FilteredCatalog { get; } = [];

    private ManagerRepository Repository => ((App)Application.Current).Repository;

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        TypeFilterCombo.SelectedIndex = 0;
        HealthFilterCombo.SelectedIndex = 0;
        SortCombo.SelectedIndex = 0;
        _authorizationTimer.Start();
        RefreshCatalogView();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _authorizationTimer.Stop();
    }

    private void OnRefreshCatalog(object sender, RoutedEventArgs e)
    {
        Repository.Refresh();
        Repository.RecordAction("Activity.Category.credentials", "Action.Credentials.RefreshCatalog");
        Frame.Navigate(typeof(CredentialsPage));
    }

    private async void OnRequestAuthorization(object sender, RoutedEventArgs e)
    {
        if (Repository.RequestCredentialCatalogAuthorization(out var error))
        {
            Repository.RecordAction("Activity.Category.credentials", "Action.Credentials.RequestAuthorization");
            Frame.Navigate(typeof(CredentialsPage));
            return;
        }

        await ShowMessageAsync(
            _localizer["Page.Credentials.Authorize.FailedTitle"],
            string.IsNullOrWhiteSpace(error) ? _localizer["Page.Credentials.Authorize.FailedMessage"] : error!);
    }

    private async void OnDeleteBySlot(object sender, RoutedEventArgs e)
    {
        var slotTextBox = new TextBox
        {
            PlaceholderText = _localizer["Page.Credentials.DeleteBySlot.Placeholder"]
        };

        var dialog = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = _localizer["Page.Credentials.DeleteBySlot.Title"],
            PrimaryButtonText = _localizer["Page.Credentials.Action.Delete"],
            CloseButtonText = _localizer["Dialog.Close"],
            DefaultButton = ContentDialogButton.Primary,
            Content = slotTextBox
        };

        var result = await dialog.ShowAsync();
        if (result != ContentDialogResult.Primary)
        {
            return;
        }

        if (!int.TryParse(slotTextBox.Text?.Trim(), out var slot))
        {
            await ShowMessageAsync(_localizer["Page.Credentials.Delete.FailedTitle"], _localizer["Page.Credentials.DeleteBySlot.Invalid"]);
            return;
        }

        await DeleteCredentialAsync(slot);
    }

    private void OnSearchTextChanged(object sender, TextChangedEventArgs e)
    {
        RefreshCatalogView();
    }

    private void OnCatalogFilterChanged(object sender, SelectionChangedEventArgs e)
    {
        RefreshCatalogView();
    }

    private async void OnShowCredentialDetails(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { DataContext: CredentialCatalogItem item })
        {
            return;
        }

        var detailStack = new StackPanel
        {
            Spacing = 10
        };
        detailStack.Children.Add(BuildDetailRow(_localizer["Credentials.Catalog.Slot"], item.Slot.ToString()));
        detailStack.Children.Add(BuildDetailRow(_localizer["Credentials.Catalog.SignCount"], item.SignCount.ToString()));
        detailStack.Children.Add(BuildDetailRow(_localizer["Credentials.Catalog.IdPrefix"], item.CredentialIdPrefix));
        detailStack.Children.Add(BuildDetailRow(_localizer["Page.Credentials.Detail.CredentialLength"], item.CredentialIdLength.ToString()));
        detailStack.Children.Add(BuildDetailRow(_localizer["Page.Credentials.Detail.RpPreview"], FormatPreview(item.RpIdPreview, item.RpIdLength)));
        detailStack.Children.Add(BuildDetailRow(_localizer["Page.Credentials.Detail.UserPreview"], FormatPreview(item.UserNamePreview, item.UserNameLength)));
        detailStack.Children.Add(BuildDetailRow(_localizer["Page.Credentials.Detail.DisplayPreview"], FormatPreview(item.DisplayNamePreview, item.DisplayNameLength)));
        detailStack.Children.Add(BuildDetailRow(_localizer["Page.Credentials.Detail.Discoverable"], item.Discoverable ? _localizer["Credentials.Catalog.Discoverable"] : _localizer["Credentials.Catalog.ServerSide"]));
        detailStack.Children.Add(BuildDetailRow(_localizer["Page.Credentials.Detail.CredRandom"], item.CredRandomReady ? _localizer["Credentials.Catalog.CredRandomReady"] : _localizer["Credentials.Catalog.CredRandomMissing"]));

        var dialog = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = $"{item.Title} · {item.Subtitle}",
            CloseButtonText = _localizer["Dialog.Close"],
            DefaultButton = ContentDialogButton.Close,
            Content = new ScrollViewer
            {
                MaxHeight = 420,
                Content = detailStack
            }
        };

        await dialog.ShowAsync();
    }

    private async void OnDeleteCredential(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { DataContext: CredentialCatalogItem item })
        {
            return;
        }

        var confirmDialog = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = _localizer["Page.Credentials.Delete.ConfirmTitle"],
            Content = string.Format(_localizer["Page.Credentials.Delete.ConfirmBody"], item.Slot, item.Title, item.Subtitle),
            PrimaryButtonText = _localizer["Page.Credentials.Action.Delete"],
            CloseButtonText = _localizer["Dialog.Close"],
            DefaultButton = ContentDialogButton.Close
        };

        if (await confirmDialog.ShowAsync() != ContentDialogResult.Primary)
        {
            return;
        }

        await DeleteCredentialAsync(item.Slot);
    }

    private FrameworkElement BuildDetailRow(string label, string value)
    {
        var panel = new StackPanel
        {
            Spacing = 4
        };
        panel.Children.Add(new TextBlock
        {
            Text = label,
            Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["AppMutedForegroundBrush"]
        });
        panel.Children.Add(new TextBlock
        {
            Text = string.IsNullOrWhiteSpace(value) ? _localizer["Security.Value.Unavailable"] : value,
            TextWrapping = TextWrapping.WrapWholeWords
        });
        return panel;
    }

    private string FormatPreview(string preview, int declaredLength)
    {
        var value = string.IsNullOrWhiteSpace(preview) ? _localizer["Security.Value.Unavailable"] : preview;
        return $"{value} ({declaredLength})";
    }

    private async Task DeleteCredentialAsync(int slot)
    {
        if (Repository.DeleteCredentialFromSelectedDevice(slot, out var error))
        {
            Repository.RecordAction("Activity.Category.credentials", "Action.Credentials.Delete");
            Frame.Navigate(typeof(CredentialsPage));
            return;
        }

        await ShowMessageAsync(_localizer["Page.Credentials.Delete.FailedTitle"],
                               string.IsNullOrWhiteSpace(error) ? _localizer["Page.Credentials.Delete.FailedMessage"] : error!);
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

    private void RefreshCatalogView()
    {
        var selectedDevice = Snapshot.SelectedDevice;
        var totalCount = Snapshot.CredentialCatalog.Count;
        var query = Snapshot.CredentialCatalog.AsEnumerable();
        var search = SearchTextBox.Text?.Trim() ?? string.Empty;

        if (!string.IsNullOrWhiteSpace(search))
        {
            query = query.Where(item =>
                $"{item.Title} {item.Subtitle} {item.Footer} {item.CredentialIdPrefix} {item.RpIdPreview} {item.UserNamePreview} {item.DisplayNamePreview}"
                    .Contains(search, StringComparison.CurrentCultureIgnoreCase));
        }

        query = TypeFilterCombo.SelectedIndex switch
        {
            1 => query.Where(item => item.Discoverable),
            2 => query.Where(item => !item.Discoverable),
            _ => query
        };

        query = HealthFilterCombo.SelectedIndex switch
        {
            1 => query.Where(item => item.CredRandomReady),
            2 => query.Where(item => !item.CredRandomReady),
            _ => query
        };

        query = SortCombo.SelectedIndex switch
        {
            1 => query.OrderByDescending(item => item.SignCount).ThenBy(item => item.Slot),
            2 => query.OrderBy(item => item.Title, StringComparer.CurrentCultureIgnoreCase).ThenBy(item => item.Slot),
            3 => query.OrderBy(item => item.Subtitle, StringComparer.CurrentCultureIgnoreCase).ThenBy(item => item.Slot),
            _ => query.OrderBy(item => item.Slot)
        };

        FilteredCatalog.Clear();
        foreach (var item in query)
        {
            FilteredCatalog.Add(item);
        }

        UpdateCatalogLayout(selectedDevice, totalCount, FilteredCatalog.Count);
        UpdateAuthorizationCountdown();
    }

    private void UpdateCatalogLayout(ConnectedDeviceInfo? selectedDevice, int totalCount, int filteredCount)
    {
        CatalogGuidanceCard.Visibility = Visibility.Collapsed;
        CatalogFiltersCard.Visibility = Visibility.Collapsed;
        CatalogListCard.Visibility = Visibility.Collapsed;
        CatalogEmptyCard.Visibility = Visibility.Collapsed;

        if (selectedDevice is null)
        {
            ShowGuidance(
                _localizer["Page.Credentials.Guidance.NoDeviceTitle"],
                _localizer["Page.Credentials.Guidance.NoDeviceDescription"],
                _localizer["Page.Credentials.Guidance.NoDeviceLine1"],
                _localizer["Page.Credentials.Guidance.NoDeviceLine2"],
                _localizer["Page.Credentials.Guidance.NoDeviceLine3"]);
            return;
        }

        if (!selectedDevice.CredentialCatalogAvailable)
        {
            if (selectedDevice.CredentialSummariesRequireAuth)
            {
                ShowGuidance(
                    _localizer["Page.Credentials.Guidance.AuthorizationTitle"],
                    _localizer["Page.Credentials.Guidance.AuthorizationDescription"],
                    _localizer["Page.Credentials.Guidance.AuthorizationLine1"],
                    _localizer["Page.Credentials.Guidance.AuthorizationLine2"],
                    _localizer["Page.Credentials.Guidance.AuthorizationLine3"]);
                return;
            }

            ShowGuidance(
                _localizer["Page.Credentials.Guidance.UnsupportedTitle"],
                _localizer["Page.Credentials.Guidance.UnsupportedDescription"],
                _localizer["Page.Credentials.Guidance.UnsupportedLine1"],
                _localizer["Page.Credentials.Guidance.UnsupportedLine2"],
                _localizer["Page.Credentials.Guidance.UnsupportedLine3"]);
            return;
        }

        CatalogFiltersCard.Visibility = totalCount > 0 ? Visibility.Visible : Visibility.Collapsed;

        if (totalCount == 0)
        {
            ShowEmptyState(
                _localizer["Page.Credentials.EmptyCatalogTitle"],
                _localizer["Page.Credentials.EmptyCatalogDescription"]);
            return;
        }

        if (filteredCount == 0)
        {
            ShowEmptyState(
                _localizer["Page.Credentials.NoMatchTitle"],
                _localizer["Page.Credentials.NoMatchDescription"]);
            return;
        }

        CatalogListCard.Visibility = Visibility.Visible;
    }

    private void ShowGuidance(string title, string description, string line1, string line2, string line3)
    {
        CatalogGuidanceTitleText.Text = title;
        CatalogGuidanceDescriptionText.Text = description;
        CatalogGuidanceLine1Text.Text = line1;
        CatalogGuidanceLine2Text.Text = line2;
        CatalogGuidanceLine3Text.Text = line3;
        CatalogGuidanceCard.Visibility = Visibility.Visible;
    }

    private void ShowEmptyState(string title, string description)
    {
        CatalogEmptyTitleText.Text = title;
        CatalogEmptyDescriptionText.Text = description;
        CatalogEmptyCard.Visibility = Visibility.Visible;
    }

    private void UpdateAuthorizationCountdown()
    {
        var device = Snapshot.SelectedDevice;
        if (device is null)
        {
            AuthorizationCountdownText.Text = _localizer["Page.Credentials.AuthCountdown.NoDevice"];
            return;
        }

        var remainingMs = device.ManagerAuthorizationExpiresAtUnixMs - DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        if (!device.ManagerAuthorizationActive || remainingMs <= 0)
        {
            AuthorizationCountdownText.Text = _localizer["Page.Credentials.AuthCountdown.Inactive"];
            return;
        }

        var remainingSeconds = (remainingMs + 999L) / 1000L;
        AuthorizationCountdownText.Text = string.Format(
            _localizer["Page.Credentials.AuthCountdown.Active"],
            remainingSeconds,
            device.ManagerAuthorizationPermissions);
    }

    private void ApplyLocalization()
    {
        PageTitleText.Text = _localizer["Page.Credentials.Title"];
        PageDescriptionText.Text = _localizer["Page.Credentials.Description"];
        AuthorizationCountdownText.Text = _localizer["Page.Credentials.AuthCountdown.Inactive"];
        RequestAuthorizationButton.Content = _localizer["Page.Credentials.Action.RequestAuthorization"];
        RefreshCatalogButton.Content = _localizer["Page.Credentials.Action.Refresh"];
        DeleteBySlotButton.Content = _localizer["Page.Credentials.Action.DeleteBySlot"];
        CatalogTitleText.Text = _localizer["Page.Credentials.CatalogTitle"];
        CatalogDescriptionText.Text = _localizer["Page.Credentials.CatalogDescription"];
        SearchLabelText.Text = _localizer["Page.Credentials.Filter.Search"];
        TypeFilterLabelText.Text = _localizer["Page.Credentials.Filter.Type"];
        HealthFilterLabelText.Text = _localizer["Page.Credentials.Filter.Health"];
        SortLabelText.Text = _localizer["Page.Credentials.Filter.Sort"];
        SearchTextBox.PlaceholderText = _localizer["Page.Credentials.Filter.SearchPlaceholder"];
        TypeFilterAllItem.Content = _localizer["Page.Credentials.Filter.TypeAll"];
        TypeFilterDiscoverableItem.Content = _localizer["Page.Credentials.Filter.TypeDiscoverable"];
        TypeFilterServerSideItem.Content = _localizer["Page.Credentials.Filter.TypeServerSide"];
        HealthFilterAllItem.Content = _localizer["Page.Credentials.Filter.HealthAll"];
        HealthFilterReadyItem.Content = _localizer["Page.Credentials.Filter.HealthReady"];
        HealthFilterMissingItem.Content = _localizer["Page.Credentials.Filter.HealthMissing"];
        SortSlotItem.Content = _localizer["Page.Credentials.Filter.SortSlot"];
        SortSignCountItem.Content = _localizer["Page.Credentials.Filter.SortSignCount"];
        SortUserItem.Content = _localizer["Page.Credentials.Filter.SortUser"];
        SortRpItem.Content = _localizer["Page.Credentials.Filter.SortRp"];
        CapabilityTitleText.Text = _localizer["Page.Credentials.CapabilityTitle"];
        EmptyStateTitleText.Text = _localizer["Page.Credentials.EmptyStateTitle"];
        EmptyStateDescriptionText.Text = _localizer["Page.Credentials.EmptyStateDescription"];
        EmptyStateNowTitleText.Text = _localizer["Page.Credentials.EmptyStateNowTitle"];
        EmptyStateLine1Text.Text = _localizer["Page.Credentials.EmptyStateLine1"];
        EmptyStateLine2Text.Text = _localizer["Page.Credentials.EmptyStateLine2"];
        EmptyStateLine3Text.Text = _localizer["Page.Credentials.EmptyStateLine3"];
    }
}
