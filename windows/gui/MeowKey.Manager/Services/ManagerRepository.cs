using System.Collections.ObjectModel;
using System.Reflection;
using MeowKey.Manager.Models;

namespace MeowKey.Manager.Services;

public sealed class ManagerRepository
{
    private readonly LocalizationService _localizer = LocalizationService.Current;
    private readonly ManagerDeviceService _deviceService = new();

    public ManagerRepository()
    {
        ActivityEntries = new ObservableCollection<ActivityEntry>
        {
            CreateActivityEntry(DateTime.Now.AddMinutes(-18), "Activity.Category.shell", "Activity.Startup.Shell"),
            CreateActivityEntry(DateTime.Now.AddMinutes(-14), "Activity.Category.platform", "Activity.Startup.PlatformWin"),
            CreateActivityEntry(DateTime.Now.AddMinutes(-11), "Activity.Category.platform", "Activity.Startup.PlatformLinux"),
            CreateActivityEntry(DateTime.Now.AddMinutes(-8), "Activity.Category.cleanup", "Activity.Startup.Cleanup")
        };

        Refresh();
    }

    public ManagerSnapshot Snapshot { get; private set; } = new();

    public ObservableCollection<ActivityEntry> ActivityEntries { get; }

    public event EventHandler? SnapshotChanged;

    public void RecordAction(string categoryKey, string messageKey)
    {
        ActivityEntries.Insert(0, CreateActivityEntry(DateTime.Now, categoryKey, messageKey));
    }

    public void Refresh()
    {
        IReadOnlyList<ConnectedDeviceInfo> devices;

        try
        {
            devices = _deviceService.EnumerateDevices();
        }
        catch (Exception ex)
        {
            devices = Array.Empty<ConnectedDeviceInfo>();
            ActivityEntries.Insert(0, new ActivityEntry(DateTime.Now, _localizer["Activity.Category.debug"], ex.Message));
        }

        Snapshot = BuildSnapshot(devices);
        SnapshotChanged?.Invoke(this, EventArgs.Empty);
    }

    private ActivityEntry CreateActivityEntry(DateTime timestamp, string categoryKey, string messageKey)
    {
        return new ActivityEntry(timestamp, _localizer[categoryKey], _localizer[messageKey]);
    }

    private ManagerSnapshot BuildSnapshot(IReadOnlyList<ConnectedDeviceInfo> devices)
    {
        var t = _localizer;
        var currentDevice = devices.FirstOrDefault();
        var appVersion = GetApplicationVersion();
        var headerSummaries = BuildHeaderSummaries(currentDevice, t);
        var overviewFacts = BuildOverviewFacts(currentDevice, t);
        var deviceEntries = BuildDeviceEntries(devices, t);
        var credentialCatalogFacts = BuildCredentialCatalogFacts(currentDevice, t);
        var credentialCatalog = BuildCredentialCatalog(currentDevice, t);
        var securityPolicies = BuildSecurityPolicies(currentDevice, t);
        var credentialCapabilities = BuildCredentialCapabilities(currentDevice, t);
        var aboutItems = BuildAboutItems(currentDevice, appVersion, t);

        return new ManagerSnapshot
        {
            ProductName = t["App.ProductName"],
            WindowSubtitle = t["App.WindowSubtitle"],
            VersionLabel = appVersion,
            ChannelLabel = t["App.ChannelLabel"],
            WindowsSurface = t["App.WindowsSurface"],
            LinuxSurface = t["App.LinuxSurface"],
            HeaderSummaries = headerSummaries,
            OverviewFacts = overviewFacts,
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
            Devices = deviceEntries,
            DevicePolicies =
            [
                new PolicyItem(t["Devices.Policy1.Title"], t["Devices.Policy1.Value"], t["Devices.Policy1.Detail"]),
                new PolicyItem(t["Devices.Policy2.Title"], t["Devices.Policy2.Value"], t["Devices.Policy2.Detail"]),
                new PolicyItem(t["Devices.Policy3.Title"], t["Devices.Policy3.Value"], t["Devices.Policy3.Detail"])
            ],
            CredentialCapabilities = credentialCapabilities,
            CredentialCatalogFacts = credentialCatalogFacts,
            CredentialCatalog = credentialCatalog,
            SecurityPolicies = securityPolicies,
            MaintenanceCommands =
            [
                new MaintenanceCommand(t["Debug.Command.InstallDriver.Label"], "powershell -ExecutionPolicy Bypass -File .\\windows\\driver\\manager-winusb\\install-manager-driver.ps1", t["Debug.Command.InstallDriver.Detail"]),
                new MaintenanceCommand(t["Debug.Command.RunManager.Label"], "powershell -ExecutionPolicy Bypass -File .\\scripts\\run-manager.ps1 -Configuration Release", t["Debug.Command.RunManager.Detail"]),
                new MaintenanceCommand(t["Debug.Command.BuildDebug.Label"], "powershell -ExecutionPolicy Bypass -File .\\scripts\\build.ps1 -BuildDir build -NoPicotool -IgnoreGitGlobalConfig", t["Debug.Command.BuildDebug.Detail"]),
                new MaintenanceCommand(t["Debug.Command.Probe.Label"], "powershell -ExecutionPolicy Bypass -File .\\scripts\\probe-board.ps1", t["Debug.Command.Probe.Detail"])
            ],
            AboutItems = aboutItems
        };
    }

    private IReadOnlyList<SummaryCard> BuildHeaderSummaries(ConnectedDeviceInfo? device, LocalizationService t)
    {
        if (device is null)
        {
            return
            [
                new SummaryCard(t["Summary.Device.Label"], t["Summary.Device.Disconnected"], string.Empty),
                new SummaryCard(t["Summary.Firmware.Label"], t["Summary.Value.Unknown"], string.Empty),
                new SummaryCard(t["Summary.Credentials.Label"], t["Summary.Value.Unknown"], string.Empty),
                new SummaryCard(t["Summary.Channel.Label"], t["Summary.Value.Offline"], string.Empty)
            ];
        }

        return
        [
            new SummaryCard(t["Summary.Device.Label"], device.DeviceName, device.ProductName),
            new SummaryCard(t["Summary.Firmware.Label"], device.FirmwareVersion, device.BuildFlavor),
            new SummaryCard(t["Summary.Credentials.Label"], $"{device.CredentialCount}/{device.CredentialCapacity}", $"format v{device.StoreFormatVersion}"),
            new SummaryCard(t["Summary.Channel.Label"], device.Transport, device.ManagementAvailable ? t["Summary.Channel.Ready"] : t["Summary.Value.Offline"])
        ];
    }

    private IReadOnlyList<InfoItem> BuildOverviewFacts(ConnectedDeviceInfo? device, LocalizationService t)
    {
        if (device is null)
        {
            return
            [
                new InfoItem(t["Overview.Fact.Connection"], t["Summary.Device.Disconnected"]),
                new InfoItem(t["Overview.Fact.Driver"], t["Summary.Value.Unknown"]),
                new InfoItem(t["Overview.Fact.Board"], t["Summary.Value.Unknown"]),
                new InfoItem(t["Overview.Fact.Security"], t["Summary.Value.Unknown"])
            ];
        }

        return
        [
            new InfoItem(t["Overview.Fact.Connection"], device.Transport, device.DevicePath),
            new InfoItem(t["Overview.Fact.Driver"], t["Overview.Driver.WinUsb"], device.UsbIdentity),
            new InfoItem(t["Overview.Fact.Board"], device.BoardCode, device.BoardSummary),
            new InfoItem(t["Overview.Fact.Security"], device.SignedBootEnabled ? t["Overview.Security.Signed"] : t["Overview.Security.Unsigned"], device.UserPresenceSource)
        ];
    }

    private IReadOnlyList<DeviceEntry> BuildDeviceEntries(IReadOnlyList<ConnectedDeviceInfo> devices, LocalizationService t)
    {
        if (devices.Count == 0)
        {
            return
            [
                new DeviceEntry(
                    t["Devices.Entry.None.Name"],
                    t["Devices.Entry.None.Role"],
                    t["Devices.Item.FirmwareLabel"],
                    t["Summary.Value.Unknown"],
                    t["Devices.Item.BoardLabel"],
                    t["Summary.Value.Unknown"],
                    t["Devices.Item.TransportLabel"],
                    t["Summary.Value.Offline"],
                    t["Summary.Device.Disconnected"],
                    t["Devices.Entry.None.Detail"])
            ];
        }

        return devices
            .Select(device => new DeviceEntry(
                device.DeviceName,
                device.ManagementAvailable ? t["Devices.Role.Managed"] : t["Devices.Role.AuthOnly"],
                t["Devices.Item.FirmwareLabel"],
                $"{device.FirmwareVersion} ({device.BuildFlavor})",
                t["Devices.Item.BoardLabel"],
                device.BoardCode,
                t["Devices.Item.TransportLabel"],
                device.Transport,
                device.DebugHidEnabled ? t["Devices.State.DebugPresent"] : t["Devices.State.ManagedOnly"],
                device.BoardSummary))
            .ToArray();
    }

    private IReadOnlyList<InfoItem> BuildCredentialCatalogFacts(ConnectedDeviceInfo? device, LocalizationService t)
    {
        if (device is null)
        {
            return
            [
                new InfoItem(t["Credentials.Catalog.FactStatus"], t["Summary.Value.Unknown"]),
                new InfoItem(t["Credentials.Catalog.FactEntries"], t["Summary.Value.Unknown"]),
                new InfoItem(t["Credentials.Catalog.FactStore"], t["Summary.Value.Unknown"])
            ];
        }

        var status = !device.CredentialCatalogAvailable
            ? t["Credentials.Catalog.StatusUnavailable"]
            : device.CredentialCatalog.Count == 0
                ? t["Credentials.Catalog.StatusEmpty"]
                : t["Credentials.Catalog.StatusReady"];
        var detail = !device.CredentialCatalogAvailable
            ? t["Credentials.Catalog.DetailUnavailable"]
            : device.CredentialCatalog.Count == 0
                ? t["Credentials.Catalog.DetailEmpty"]
                : t["Credentials.Catalog.DetailReady"];

        return
        [
            new InfoItem(t["Credentials.Catalog.FactStatus"], status, detail),
            new InfoItem(t["Credentials.Catalog.FactEntries"], $"{device.CredentialCatalog.Count}/{device.CredentialCount}", $"{device.CredentialCount}/{device.CredentialCapacity}"),
            new InfoItem(t["Credentials.Catalog.FactStore"], $"v{device.StoreFormatVersion}", device.Transport)
        ];
    }

    private IReadOnlyList<CredentialCatalogItem> BuildCredentialCatalog(ConnectedDeviceInfo? device, LocalizationService t)
    {
        if (device is null || !device.CredentialCatalogAvailable || device.CredentialCatalog.Count == 0)
        {
            return Array.Empty<CredentialCatalogItem>();
        }

        return device.CredentialCatalog
            .Select(item =>
            {
                var userTitle = string.IsNullOrWhiteSpace(item.UserNamePreview)
                    ? t["Credentials.Catalog.NoUser"]
                    : item.UserNamePreview;
                var rpSubtitle = string.IsNullOrWhiteSpace(item.RpIdPreview)
                    ? t["Credentials.Catalog.NoRp"]
                    : item.RpIdPreview;
                var displayName = string.IsNullOrWhiteSpace(item.DisplayNamePreview)
                    ? t["Credentials.Catalog.NoDisplay"]
                    : item.DisplayNamePreview;
                var detail = string.Join(" · ",
                [
                    $"{t["Credentials.Catalog.Slot"]} {item.Slot}",
                    $"{t["Credentials.Catalog.SignCount"]} {item.SignCount}",
                    item.Discoverable ? t["Credentials.Catalog.Discoverable"] : t["Credentials.Catalog.ServerSide"],
                    item.CredRandomReady ? t["Credentials.Catalog.CredRandomReady"] : t["Credentials.Catalog.CredRandomMissing"]
                ]);
                var footer = $"{displayName} · {t["Credentials.Catalog.IdPrefix"]} {item.CredentialIdPrefix}";

                return new CredentialCatalogItem(userTitle, rpSubtitle, detail, footer);
            })
            .ToArray();
    }

    private IReadOnlyList<CapabilityItem> BuildCredentialCapabilities(ConnectedDeviceInfo? device, LocalizationService t)
    {
        var countText = device is null
            ? t["Summary.Value.Unknown"]
            : !device.CredentialCatalogAvailable
                ? t["Credentials.Catalog.StatusUnavailable"]
                : $"{device.CredentialCatalog.Count}/{device.CredentialCapacity}";

        return
        [
            new CapabilityItem(t["Credentials.Capability1.Name"], countText, t["Credentials.Capability1.Detail"]),
            new CapabilityItem(t["Credentials.Capability2.Name"], t["Credentials.Capability2.Status"], t["Credentials.Capability2.Detail"]),
            new CapabilityItem(t["Credentials.Capability3.Name"], t["Credentials.Capability3.Status"], t["Credentials.Capability3.Detail"]),
            new CapabilityItem(t["Credentials.Capability4.Name"], device?.DebugHidEnabled == true ? t["Credentials.Capability4.Status"] : t["Credentials.Capability4.StatusLimited"], t["Credentials.Capability4.Detail"])
        ];
    }

    private IReadOnlyList<PolicyItem> BuildSecurityPolicies(ConnectedDeviceInfo? device, LocalizationService t)
    {
        if (device is null)
        {
            return
            [
                new PolicyItem(t["Security.Policy1.Title"], t["Summary.Value.Unknown"], t["Security.Policy1.Detail"]),
                new PolicyItem(t["Security.Policy2.Title"], t["Summary.Value.Unknown"], t["Security.Policy2.Detail"]),
                new PolicyItem(t["Security.Policy3.Title"], t["Summary.Value.Unknown"], t["Security.Policy3.Detail"]),
                new PolicyItem(t["Security.Policy4.Title"], t["Summary.Value.Unknown"], t["Security.Policy4.Detail"]),
                new PolicyItem(t["Security.Policy5.Title"], t["Summary.Value.Unknown"], t["Security.Value.Unavailable"]),
                new PolicyItem(t["Security.Policy6.Title"], t["Summary.Value.Unknown"], t["Security.Value.Unavailable"])
            ];
        }

        var security = device.SecurityState;
        var effectiveUserPresence = security?.EffectiveUserPresence;
        var persistedUserPresence = security?.PersistedUserPresence;
        var bootFlags0 = security?.BootFlags0;
        var bootFlags1 = security?.BootFlags1;
        var upValue = BuildUserPresenceSummary(effectiveUserPresence, device.UserPresenceSource, device.UserPresenceTapCount, t);
        var upDetail = BuildUserPresenceDetail(effectiveUserPresence, persistedUserPresence, device.UserPresenceSessionOverride, t);
        var pinValue = security?.PinConfigured == true || device.PinConfigured
            ? t["Security.Pin.Configured"]
            : t["Security.Pin.NotConfigured"];
        var pinRetries = security?.PinRetries ?? device.PinRetries;
        var bootValue = (security?.SignedBootEnabled ?? device.SignedBootEnabled)
            ? t["Security.Policy3.ValueSigned"]
            : t["Security.Policy3.ValueUnsigned"];
        var antiRollbackEnabled = security?.AntiRollbackEnabled ?? device.AntiRollbackEnabled;
        var antiRollbackVersion = security?.AntiRollbackVersion ?? device.AntiRollbackVersion;
        var exposureValue = (security?.DebugHidEnabled ?? device.DebugHidEnabled)
            ? t["Security.Policy4.ValueDebug"]
            : t["Security.Policy4.ValueManaged"];
        var exposureDetail = string.Join(" · ",
        [
            $"{t["Security.Label.Management"]}: {BoolLabel(device.ManagementAvailable, t)}",
            $"{t["Security.Label.FidoHid"]}: {BoolLabel(device.FidoHidAvailable, t)}",
            $"{t["Security.Label.CtapConfigured"]}: {BoolLabel(security?.CtapConfigured ?? device.CtapConfigured, t)}"
        ]);
        var flags0Value = bootFlags0?.Available == true ? bootFlags0.Raw : t["Security.Value.Unavailable"];
        var flags0Detail = bootFlags0?.Available == true
            ? string.Join(" · ",
            [
                $"{t["Security.Label.RollbackRequired"]}: {BoolLabel(bootFlags0.RollbackRequired, t)}",
                $"{t["Security.Label.FlashBootDisabled"]}: {BoolLabel(bootFlags0.FlashBootDisabled, t)}",
                $"{t["Security.Label.PicobootDisabled"]}: {BoolLabel(bootFlags0.PicobootDisabled, t)}"
            ])
            : t["Security.Value.Unavailable"];
        var flags1Value = bootFlags1?.Available == true ? bootFlags1.Raw : t["Security.Value.Unavailable"];
        var flags1Detail = bootFlags1?.Available == true
            ? string.Join(" · ",
            [
                $"{t["Security.Label.KeyValidMask"]}: {bootFlags1.KeyValidMask}",
                $"{t["Security.Label.KeyInvalidMask"]}: {bootFlags1.KeyInvalidMask}"
            ])
            : t["Security.Value.Unavailable"];

        return
        [
            new PolicyItem(t["Security.Policy1.Title"], upValue, upDetail),
            new PolicyItem(t["Security.Policy2.Title"], pinValue, $"{t["Security.Pin.Retries"]}: {pinRetries}"),
            new PolicyItem(t["Security.Policy3.Title"], bootValue, $"{t["Security.Label.Version"]}: {antiRollbackVersion} · {(antiRollbackEnabled ? t["Security.Policy3.DetailRollbackOn"] : t["Security.Policy3.DetailRollbackOff"])}"),
            new PolicyItem(t["Security.Policy4.Title"], exposureValue, device.ManagementAvailable ? exposureDetail : t["Security.Policy4.DetailUnavailable"]),
            new PolicyItem(t["Security.Policy5.Title"], flags0Value, flags0Detail),
            new PolicyItem(t["Security.Policy6.Title"], flags1Value, flags1Detail)
        ];
    }

    private string BuildUserPresenceSummary(UserPresenceConfigInfo? config, string fallbackSource, int fallbackTapCount, LocalizationService t)
    {
        var source = config?.Source ?? fallbackSource;
        var tapCount = config?.TapCount ?? fallbackTapCount;

        return $"{source} · {tapCount}";
    }

    private string BuildUserPresenceDetail(UserPresenceConfigInfo? effective,
                                          UserPresenceConfigInfo? persisted,
                                          bool sessionOverride,
                                          LocalizationService t)
    {
        var effectiveDetail = $"{t["Security.Label.Effective"]}: {BuildPresenceShape(effective, t)}";
        var persistedDetail = $"{t["Security.Label.Persisted"]}: {BuildPresenceShape(persisted, t)}";
        var overrideDetail = sessionOverride ? t["Security.Label.OverrideOn"] : t["Security.Label.OverrideOff"];

        return string.Join(" · ", [effectiveDetail, persistedDetail, overrideDetail]);
    }

    private string BuildPresenceShape(UserPresenceConfigInfo? config, LocalizationService t)
    {
        if (config is null)
        {
            return t["Security.Value.Unavailable"];
        }

        return string.Join(" / ",
        [
            config.Source,
            $"{config.GestureWindowMs} ms",
            $"{config.RequestTimeoutMs} ms",
            $"{t["Security.Label.Gpio"]} {config.GpioPin}"
        ]);
    }

    private string BoolLabel(bool value, LocalizationService t)
    {
        return value ? t["Security.Value.Enabled"] : t["Security.Value.Disabled"];
    }

    private IReadOnlyList<InfoItem> BuildAboutItems(ConnectedDeviceInfo? device, string appVersion, LocalizationService t)
    {
        if (device is null)
        {
            return
            [
                new InfoItem(t["About.Item.AppVersion"], appVersion),
                new InfoItem(t["About.Item.DeviceVersion"], t["Summary.Device.Disconnected"]),
                new InfoItem(t["About.Item.DeviceName"], t["Summary.Device.Disconnected"]),
                new InfoItem(t["About.Item.UsbIdentity"], t["Summary.Value.Unknown"]),
                new InfoItem(t["About.Item.Board"], t["Summary.Value.Unknown"])
            ];
        }

        return
        [
            new InfoItem(t["About.Item.AppVersion"], appVersion),
            new InfoItem(t["About.Item.DeviceVersion"], device.FirmwareVersion, device.BuildFlavor),
            new InfoItem(t["About.Item.DeviceName"], device.DeviceName, device.ProductName),
            new InfoItem(t["About.Item.UsbIdentity"], device.UsbIdentity, device.Serial),
            new InfoItem(t["About.Item.Board"], device.BoardCode, device.BoardSummary)
        ];
    }

    private static string GetApplicationVersion()
    {
        var informational = Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
        if (!string.IsNullOrWhiteSpace(informational))
        {
            return informational!;
        }

        return Assembly.GetExecutingAssembly().GetName().Version?.ToString() ?? "0.0.0";
    }
}
