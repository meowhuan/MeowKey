using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
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
    }

    public ManagerSnapshot Snapshot => ((App)Application.Current).Repository.Snapshot;

    private ManagerRepository Repository => ((App)Application.Current).Repository;

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

    private void ApplyLocalization()
    {
        PageTitleText.Text = _localizer["Page.Security.Title"];
        PageDescriptionText.Text = _localizer["Page.Security.Description"];
        RefreshSecurityButton.Content = _localizer["Page.Security.Action.Refresh"];
        KeepDebugLimitedButton.Content = _localizer["Page.Security.Action.KeepDebugLimited"];
        UserPresenceTitleText.Text = _localizer["Page.Security.UserPresenceTitle"];
        UserPresenceDescriptionText.Text = _localizer["Page.Security.UserPresenceDescription"];
        RecommendationTitleText.Text = _localizer["Page.Security.RecommendationTitle"];
        RecommendationDescriptionText.Text = _localizer["Page.Security.RecommendationDescription"];
    }
}
