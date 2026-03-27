namespace MeowKey.Manager.Models;

public sealed class CredentialSummaryInfo
{
    public int Slot { get; init; }
    public int CredentialIdLength { get; init; }
    public string CredentialIdPrefix { get; init; } = string.Empty;
    public int SignCount { get; init; }
    public bool Discoverable { get; init; }
    public bool CredRandomReady { get; init; }
    public string RpIdPreview { get; init; } = string.Empty;
    public int RpIdLength { get; init; }
    public string UserNamePreview { get; init; } = string.Empty;
    public int UserNameLength { get; init; }
    public string DisplayNamePreview { get; init; } = string.Empty;
    public int DisplayNameLength { get; init; }
}

public sealed class UserPresenceConfigInfo
{
    public bool Enabled { get; init; }
    public string Source { get; init; } = string.Empty;
    public int GpioPin { get; init; }
    public bool GpioActiveLow { get; init; }
    public int TapCount { get; init; }
    public int GestureWindowMs { get; init; }
    public int RequestTimeoutMs { get; init; }
}

public sealed class OtpBootFlags0Info
{
    public bool Available { get; init; }
    public string Raw { get; init; } = string.Empty;
    public bool RollbackRequired { get; init; }
    public bool FlashBootDisabled { get; init; }
    public bool PicobootDisabled { get; init; }
}

public sealed class OtpBootFlags1Info
{
    public bool Available { get; init; }
    public string Raw { get; init; } = string.Empty;
    public string KeyValidMask { get; init; } = string.Empty;
    public string KeyInvalidMask { get; init; } = string.Empty;
}

public sealed class SecurityStateInfo
{
    public string BuildFlavor { get; init; } = string.Empty;
    public string BuildVersion { get; init; } = string.Empty;
    public bool SignedBootEnabled { get; init; }
    public bool AntiRollbackEnabled { get; init; }
    public int AntiRollbackVersion { get; init; }
    public bool BoardDetected { get; init; }
    public string BoardCode { get; init; } = string.Empty;
    public string BoardSummary { get; init; } = string.Empty;
    public bool DebugHidEnabled { get; init; }
    public bool CtapConfigured { get; init; }
    public bool PinConfigured { get; init; }
    public int PinRetries { get; init; }
    public bool UserPresenceSessionOverride { get; init; }
    public UserPresenceConfigInfo EffectiveUserPresence { get; init; } = new();
    public UserPresenceConfigInfo PersistedUserPresence { get; init; } = new();
    public OtpBootFlags0Info BootFlags0 { get; init; } = new();
    public OtpBootFlags1Info BootFlags1 { get; init; } = new();
}

public sealed class ConnectedDeviceInfo
{
    public string DevicePath { get; init; } = string.Empty;
    public string DeviceName { get; init; } = string.Empty;
    public string ProductName { get; init; } = string.Empty;
    public string Serial { get; init; } = string.Empty;
    public string UsbIdentity { get; init; } = string.Empty;
    public string BuildFlavor { get; init; } = string.Empty;
    public string FirmwareVersion { get; init; } = string.Empty;
    public bool DebugHidEnabled { get; init; }
    public bool SignedBootEnabled { get; init; }
    public bool AntiRollbackEnabled { get; init; }
    public bool BoardDetected { get; init; }
    public string BoardCode { get; init; } = string.Empty;
    public string BoardSummary { get; init; } = string.Empty;
    public int CredentialCount { get; init; }
    public int CredentialCapacity { get; init; }
    public int StoreFormatVersion { get; init; }
    public bool PinConfigured { get; init; }
    public int PinRetries { get; init; }
    public bool UserPresenceEnabled { get; init; }
    public string UserPresenceSource { get; init; } = string.Empty;
    public int UserPresenceGpioPin { get; init; }
    public bool UserPresenceGpioActiveLow { get; init; }
    public int UserPresenceTapCount { get; init; }
    public int UserPresenceGestureWindowMs { get; init; }
    public int UserPresenceRequestTimeoutMs { get; init; }
    public bool UserPresenceSessionOverride { get; init; }
    public bool FidoHidAvailable { get; init; }
    public bool ManagementAvailable { get; init; }
    public bool CtapConfigured { get; init; }
    public string Transport { get; init; } = string.Empty;
    public int AntiRollbackVersion { get; init; }
    public bool CredentialCatalogAvailable { get; init; }
    public IReadOnlyList<CredentialSummaryInfo> CredentialCatalog { get; init; } = Array.Empty<CredentialSummaryInfo>();
    public bool SecurityStateAvailable { get; init; }
    public SecurityStateInfo? SecurityState { get; init; }
}
