namespace MeowKey.Manager.Models;

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
}
