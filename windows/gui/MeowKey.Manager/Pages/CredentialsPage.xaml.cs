using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using MeowKey.Manager.Models;
using MeowKey.Manager.Services;

namespace MeowKey.Manager.Pages;

public sealed partial class CredentialsPage : Page
{
    private readonly LocalizationService _localizer = LocalizationService.Current;

    public CredentialsPage()
    {
        InitializeComponent();
        ApplyLocalization();
    }

    public ManagerSnapshot Snapshot => ((App)Application.Current).Repository.Snapshot;

    private ManagerRepository Repository => ((App)Application.Current).Repository;

    private void OnRefreshCatalog(object sender, RoutedEventArgs e)
    {
        Repository.Refresh();
        Repository.RecordAction("Activity.Category.credentials", "Action.Credentials.RefreshCatalog");
        Frame.Navigate(typeof(CredentialsPage));
    }

    private void OnPlanSingleDelete(object sender, RoutedEventArgs e)
    {
        Repository.RecordAction("Activity.Category.credentials", "Action.Credentials.PlanDelete");
    }

    private void ApplyLocalization()
    {
        PageTitleText.Text = _localizer["Page.Credentials.Title"];
        PageDescriptionText.Text = _localizer["Page.Credentials.Description"];
        RefreshCatalogButton.Content = _localizer["Page.Credentials.Action.Refresh"];
        PlanSingleDeleteButton.Content = _localizer["Page.Credentials.Action.PlanDelete"];
        CatalogTitleText.Text = _localizer["Page.Credentials.CatalogTitle"];
        CatalogDescriptionText.Text = _localizer["Page.Credentials.CatalogDescription"];
        CapabilityTitleText.Text = _localizer["Page.Credentials.CapabilityTitle"];
        EmptyStateTitleText.Text = _localizer["Page.Credentials.EmptyStateTitle"];
        EmptyStateDescriptionText.Text = _localizer["Page.Credentials.EmptyStateDescription"];
        EmptyStateNowTitleText.Text = _localizer["Page.Credentials.EmptyStateNowTitle"];
        EmptyStateLine1Text.Text = _localizer["Page.Credentials.EmptyStateLine1"];
        EmptyStateLine2Text.Text = _localizer["Page.Credentials.EmptyStateLine2"];
        EmptyStateLine3Text.Text = _localizer["Page.Credentials.EmptyStateLine3"];
    }
}
