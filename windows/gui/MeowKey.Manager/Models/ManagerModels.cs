using System.Globalization;

namespace MeowKey.Manager.Models;

public sealed class SummaryCard
{
    public SummaryCard(string label, string value, string detail)
    {
        Label = label;
        Value = value;
        Detail = detail;
    }

    public string Label { get; }
    public string Value { get; }
    public string Detail { get; }
}

public sealed class ReadinessItem
{
    public ReadinessItem(string status, string title, string detail)
    {
        Status = status;
        Title = title;
        Detail = detail;
    }

    public string Status { get; }
    public string Title { get; }
    public string Detail { get; }
}

public sealed class PlatformChoice
{
    public PlatformChoice(string platform, string toolkit, string detail)
    {
        Platform = platform;
        Toolkit = toolkit;
        Detail = detail;
    }

    public string Platform { get; }
    public string Toolkit { get; }
    public string Detail { get; }
}

public sealed class DeviceEntry
{
    public DeviceEntry(
        string devicePath,
        string name,
        string role,
        string firmwareLabel,
        string firmware,
        string boardLabel,
        string board,
        string transportLabel,
        string transport,
        string state,
        string detail,
        bool isSelected,
        string selectionLabel)
    {
        DevicePath = devicePath;
        Name = name;
        Role = role;
        FirmwareLabel = firmwareLabel;
        Firmware = firmware;
        BoardLabel = boardLabel;
        Board = board;
        TransportLabel = transportLabel;
        Transport = transport;
        State = state;
        Detail = detail;
        IsSelected = isSelected;
        SelectionLabel = selectionLabel;
    }

    public string DevicePath { get; }
    public string Name { get; }
    public string Role { get; }
    public string FirmwareLabel { get; }
    public string Firmware { get; }
    public string BoardLabel { get; }
    public string Board { get; }
    public string TransportLabel { get; }
    public string Transport { get; }
    public string State { get; }
    public string Detail { get; }
    public bool IsSelected { get; }
    public string SelectionLabel { get; }
}

public sealed class CapabilityItem
{
    public CapabilityItem(string name, string status, string detail)
    {
        Name = name;
        Status = status;
        Detail = detail;
    }

    public string Name { get; }
    public string Status { get; }
    public string Detail { get; }
}

public sealed class CredentialCatalogItem
{
    public CredentialCatalogItem(string title,
                                 string subtitle,
                                 string detail,
                                 string footer,
                                 int slot,
                                 int signCount,
                                 bool discoverable,
                                 bool credRandomReady,
                                 string credentialIdPrefix,
                                 int credentialIdLength,
                                 string rpIdPreview,
                                 int rpIdLength,
                                 string userNamePreview,
                                 int userNameLength,
                                 string displayNamePreview,
                                 int displayNameLength,
                                 string detailsLabel)
    {
        Title = title;
        Subtitle = subtitle;
        Detail = detail;
        Footer = footer;
        Slot = slot;
        SignCount = signCount;
        Discoverable = discoverable;
        CredRandomReady = credRandomReady;
        CredentialIdPrefix = credentialIdPrefix;
        CredentialIdLength = credentialIdLength;
        RpIdPreview = rpIdPreview;
        RpIdLength = rpIdLength;
        UserNamePreview = userNamePreview;
        UserNameLength = userNameLength;
        DisplayNamePreview = displayNamePreview;
        DisplayNameLength = displayNameLength;
        DetailsLabel = detailsLabel;
    }

    public string Title { get; }
    public string Subtitle { get; }
    public string Detail { get; }
    public string Footer { get; }
    public int Slot { get; }
    public int SignCount { get; }
    public bool Discoverable { get; }
    public bool CredRandomReady { get; }
    public string CredentialIdPrefix { get; }
    public int CredentialIdLength { get; }
    public string RpIdPreview { get; }
    public int RpIdLength { get; }
    public string UserNamePreview { get; }
    public int UserNameLength { get; }
    public string DisplayNamePreview { get; }
    public int DisplayNameLength { get; }
    public string DetailsLabel { get; }
}

public sealed class UserPresenceSection
{
    public UserPresenceSection(string title, string status, string detail, IReadOnlyList<InfoItem> rows)
    {
        Title = title;
        Status = status;
        Detail = detail;
        Rows = rows;
    }

    public string Title { get; }
    public string Status { get; }
    public string Detail { get; }
    public IReadOnlyList<InfoItem> Rows { get; }
}

public sealed class PolicyItem
{
    public PolicyItem(string title, string value, string detail)
    {
        Title = title;
        Value = value;
        Detail = detail;
    }

    public string Title { get; }
    public string Value { get; }
    public string Detail { get; }
}

public sealed class InfoItem
{
    public InfoItem(string label, string value, string detail = "")
    {
        Label = label;
        Value = value;
        Detail = detail;
    }

    public string Label { get; }
    public string Value { get; }
    public string Detail { get; }
}

public sealed class MaintenanceCommand
{
    public MaintenanceCommand(string label, string command, string detail)
    {
        Label = label;
        Command = command;
        Detail = detail;
    }

    public string Label { get; }
    public string Command { get; }
    public string Detail { get; }
}

public sealed class ActivityEntry
{
    public ActivityEntry(DateTime timestamp, string category, string message)
    {
        Timestamp = timestamp;
        Category = category;
        Message = message;
    }

    public DateTime Timestamp { get; }
    public string TimestampText => Timestamp.ToString("g", CultureInfo.CurrentUICulture);
    public string Category { get; }
    public string Message { get; }
}

public sealed class ManagerSnapshot
{
    public string ProductName { get; init; } = string.Empty;
    public string WindowSubtitle { get; init; } = string.Empty;
    public string VersionLabel { get; init; } = string.Empty;
    public string ChannelLabel { get; init; } = string.Empty;
    public string WindowsSurface { get; init; } = string.Empty;
    public string LinuxSurface { get; init; } = string.Empty;
    public ConnectedDeviceInfo? SelectedDevice { get; init; }
    public IReadOnlyList<SummaryCard> HeaderSummaries { get; init; } = Array.Empty<SummaryCard>();
    public IReadOnlyList<InfoItem> OverviewFacts { get; init; } = Array.Empty<InfoItem>();
    public IReadOnlyList<ReadinessItem> DashboardReadiness { get; init; } = Array.Empty<ReadinessItem>();
    public IReadOnlyList<PlatformChoice> PlatformChoices { get; init; } = Array.Empty<PlatformChoice>();
    public IReadOnlyList<DeviceEntry> Devices { get; init; } = Array.Empty<DeviceEntry>();
    public IReadOnlyList<PolicyItem> DevicePolicies { get; init; } = Array.Empty<PolicyItem>();
    public IReadOnlyList<CapabilityItem> CredentialCapabilities { get; init; } = Array.Empty<CapabilityItem>();
    public IReadOnlyList<InfoItem> CredentialCatalogFacts { get; init; } = Array.Empty<InfoItem>();
    public IReadOnlyList<CredentialCatalogItem> CredentialCatalog { get; init; } = Array.Empty<CredentialCatalogItem>();
    public IReadOnlyList<PolicyItem> SecurityPolicies { get; init; } = Array.Empty<PolicyItem>();
    public IReadOnlyList<UserPresenceSection> UserPresenceSections { get; init; } = Array.Empty<UserPresenceSection>();
    public IReadOnlyList<MaintenanceCommand> MaintenanceCommands { get; init; } = Array.Empty<MaintenanceCommand>();
    public IReadOnlyList<InfoItem> AboutItems { get; init; } = Array.Empty<InfoItem>();
}
