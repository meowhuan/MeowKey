using System.Collections.ObjectModel;
using MeowKey.Manager.Models;

namespace MeowKey.Manager.Services;

public sealed class ManagerRepository
{
    private readonly LocalizationService _localizer = LocalizationService.Current;

    public ManagerRepository()
    {
        Snapshot = BuildSnapshot();
        ActivityEntries = new ObservableCollection<ActivityEntry>
        {
            CreateActivityEntry(DateTime.Now.AddMinutes(-18), "Activity.Category.shell", "Activity.Startup.Shell"),
            CreateActivityEntry(DateTime.Now.AddMinutes(-14), "Activity.Category.platform", "Activity.Startup.PlatformWin"),
            CreateActivityEntry(DateTime.Now.AddMinutes(-11), "Activity.Category.platform", "Activity.Startup.PlatformLinux"),
            CreateActivityEntry(DateTime.Now.AddMinutes(-8), "Activity.Category.cleanup", "Activity.Startup.Cleanup")
        };
    }

    public ManagerSnapshot Snapshot { get; }

    public ObservableCollection<ActivityEntry> ActivityEntries { get; }

    public void RecordAction(string categoryKey, string messageKey)
    {
        ActivityEntries.Insert(0, CreateActivityEntry(DateTime.Now, categoryKey, messageKey));
    }

    private ActivityEntry CreateActivityEntry(DateTime timestamp, string categoryKey, string messageKey)
    {
        return new ActivityEntry(timestamp, _localizer[categoryKey], _localizer[messageKey]);
    }

    private ManagerSnapshot BuildSnapshot()
    {
        var t = _localizer;

        return new ManagerSnapshot
        {
            ProductName = t["App.ProductName"],
            WindowSubtitle = t["App.WindowSubtitle"],
            VersionLabel = "v0.1.0-preview",
            ChannelLabel = t["App.ChannelLabel"],
            WindowsSurface = t["App.WindowsSurface"],
            LinuxSurface = t["App.LinuxSurface"],
            HeaderSummaries =
            [
                new SummaryCard(t["Summary.PrimaryDevice.Label"], t["Summary.PrimaryDevice.Value"], t["Summary.PrimaryDevice.Detail"]),
                new SummaryCard(t["Summary.ReleaseMode.Label"], t["Summary.ReleaseMode.Value"], t["Summary.ReleaseMode.Detail"]),
                new SummaryCard(t["Summary.CredentialFlow.Label"], t["Summary.CredentialFlow.Value"], t["Summary.CredentialFlow.Detail"]),
                new SummaryCard(t["Summary.LinuxUi.Label"], t["Summary.LinuxUi.Value"], t["Summary.LinuxUi.Detail"])
            ],
            DashboardReadiness =
            [
                new ReadinessItem(t["Dashboard.Readiness.Now"], t["Dashboard.Readiness.Item1.Title"], t["Dashboard.Readiness.Item1.Detail"]),
                new ReadinessItem(t["Dashboard.Readiness.Now"], t["Dashboard.Readiness.Item2.Title"], t["Dashboard.Readiness.Item2.Detail"]),
                new ReadinessItem(t["Dashboard.Readiness.Next"], t["Dashboard.Readiness.Item3.Title"], t["Dashboard.Readiness.Item3.Detail"]),
                new ReadinessItem(t["Dashboard.Readiness.Next"], t["Dashboard.Readiness.Item4.Title"], t["Dashboard.Readiness.Item4.Detail"])
            ],
            PlatformChoices =
            [
                new PlatformChoice(t["Dashboard.Platform.Windows"], t["Dashboard.Platform.Windows.Toolkit"], t["Dashboard.Platform.Windows.Detail"]),
                new PlatformChoice(t["Dashboard.Platform.Linux"], t["Dashboard.Platform.Linux.Toolkit"], t["Dashboard.Platform.Linux.Detail"]),
                new PlatformChoice(t["Dashboard.Platform.Shared"], t["Dashboard.Platform.Shared.Toolkit"], t["Dashboard.Platform.Shared.Detail"])
            ],
            Devices =
            [
                new DeviceEntry(
                    t["Devices.Entry.Primary.Name"],
                    t["Devices.Entry.Primary.Role"],
                    t["Devices.Item.FirmwareLabel"],
                    "generic-hardened",
                    t["Devices.Item.BoardLabel"],
                    "meowkey_rp2350_usb",
                    t["Devices.Item.TransportLabel"],
                    t["Devices.Entry.Primary.Transport"],
                    t["Devices.Entry.Primary.State"],
                    t["Devices.Entry.Primary.Detail"]),
                new DeviceEntry(
                    t["Devices.Entry.Bringup.Name"],
                    t["Devices.Entry.Bringup.Role"],
                    t["Devices.Item.FirmwareLabel"],
                    "generic-debug",
                    t["Devices.Item.BoardLabel"],
                    "meowkey_rp2350_usb",
                    t["Devices.Item.TransportLabel"],
                    t["Devices.Entry.Bringup.Transport"],
                    t["Devices.Entry.Bringup.State"],
                    t["Devices.Entry.Bringup.Detail"]),
                new DeviceEntry(
                    t["Devices.Entry.Probe.Name"],
                    t["Devices.Entry.Probe.Role"],
                    t["Devices.Item.FirmwareLabel"],
                    "probe-board-id",
                    t["Devices.Item.BoardLabel"],
                    "unknown-baseboard",
                    t["Devices.Item.TransportLabel"],
                    t["Devices.Entry.Probe.Transport"],
                    t["Devices.Entry.Probe.State"],
                    t["Devices.Entry.Probe.Detail"])
            ],
            DevicePolicies =
            [
                new PolicyItem(t["Devices.Policy1.Title"], t["Devices.Policy1.Value"], t["Devices.Policy1.Detail"]),
                new PolicyItem(t["Devices.Policy2.Title"], t["Devices.Policy2.Value"], t["Devices.Policy2.Detail"]),
                new PolicyItem(t["Devices.Policy3.Title"], t["Devices.Policy3.Value"], t["Devices.Policy3.Detail"])
            ],
            CredentialCapabilities =
            [
                new CapabilityItem(t["Credentials.Capability1.Name"], t["Credentials.Capability1.Status"], t["Credentials.Capability1.Detail"]),
                new CapabilityItem(t["Credentials.Capability2.Name"], t["Credentials.Capability2.Status"], t["Credentials.Capability2.Detail"]),
                new CapabilityItem(t["Credentials.Capability3.Name"], t["Credentials.Capability3.Status"], t["Credentials.Capability3.Detail"]),
                new CapabilityItem(t["Credentials.Capability4.Name"], t["Credentials.Capability4.Status"], t["Credentials.Capability4.Detail"])
            ],
            SecurityPolicies =
            [
                new PolicyItem(t["Security.Policy1.Title"], t["Security.Policy1.Value"], t["Security.Policy1.Detail"]),
                new PolicyItem(t["Security.Policy2.Title"], t["Security.Policy2.Value"], t["Security.Policy2.Detail"]),
                new PolicyItem(t["Security.Policy3.Title"], t["Security.Policy3.Value"], t["Security.Policy3.Detail"]),
                new PolicyItem(t["Security.Policy4.Title"], t["Security.Policy4.Value"], t["Security.Policy4.Detail"])
            ],
            MaintenanceCommands =
            [
                new MaintenanceCommand(t["Maintenance.Command.Check.Label"], "powershell -ExecutionPolicy Bypass -File .\\scripts\\check.ps1", t["Maintenance.Command.Check.Detail"]),
                new MaintenanceCommand(t["Maintenance.Command.Build.Label"], "powershell -ExecutionPolicy Bypass -File .\\scripts\\build.ps1 -BuildDir build -NoPicotool -IgnoreGitGlobalConfig", t["Maintenance.Command.Build.Detail"]),
                new MaintenanceCommand(t["Maintenance.Command.Hardened.Label"], "powershell -ExecutionPolicy Bypass -File .\\scripts\\build.ps1 -BuildDir build-hardened -NoPicotool -IgnoreGitGlobalConfig -DisableDebugHid", t["Maintenance.Command.Hardened.Detail"]),
                new MaintenanceCommand(t["Maintenance.Command.Probe.Label"], "powershell -ExecutionPolicy Bypass -File .\\scripts\\probe-board.ps1", t["Maintenance.Command.Probe.Detail"])
            ]
        };
    }
}
