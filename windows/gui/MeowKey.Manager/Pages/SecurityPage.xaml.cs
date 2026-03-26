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

    private void OnPromoteSecureReady(object sender, RoutedEventArgs e)
    {
        Repository.RecordAction("Activity.Category.security", "Action.Security.PromoteSecureReady");
    }

    private void OnKeepDebugLimited(object sender, RoutedEventArgs e)
    {
        Repository.RecordAction("Activity.Category.security", "Action.Security.KeepDebugLimited");
    }

    private void ApplyLocalization()
    {
        PageTitleText.Text = _localizer["Page.Security.Title"];
        PageDescriptionText.Text = _localizer["Page.Security.Description"];
        PromoteSecureReadyButton.Content = _localizer["Page.Security.Action.PromoteSecureReady"];
        KeepDebugLimitedButton.Content = _localizer["Page.Security.Action.KeepDebugLimited"];
        RecommendationTitleText.Text = _localizer["Page.Security.RecommendationTitle"];
        RecommendationDescriptionText.Text = _localizer["Page.Security.RecommendationDescription"];
    }
}
