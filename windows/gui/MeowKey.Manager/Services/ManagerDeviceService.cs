using System.Text.Json;
using System.Text.Json.Serialization;
using MeowKey.Manager.Models;

namespace MeowKey.Manager.Services;

public sealed class ManagerDeviceService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    public IReadOnlyList<ConnectedDeviceInfo> EnumerateDevices()
    {
        var devices = new List<ConnectedDeviceInfo>();

        foreach (var devicePath in WinUsbManagerTransport.EnumerateDevicePaths())
        {
            try
            {
                var json = WinUsbManagerTransport.SendCommand(devicePath, 0x01, Array.Empty<byte>());
                var snapshot = JsonSerializer.Deserialize<ManagerSnapshotResponse>(json, JsonOptions)
                    ?? throw new InvalidOperationException("Manager snapshot payload could not be parsed.");

                devices.Add(new ConnectedDeviceInfo
                {
                    DevicePath = devicePath,
                    DeviceName = snapshot.DeviceName ?? "MeowKey",
                    ProductName = snapshot.ProductName ?? "MeowKey",
                    Serial = snapshot.Serial ?? string.Empty,
                    UsbIdentity = $"{snapshot.Usb?.Vid ?? "n/a"}:{snapshot.Usb?.Pid ?? "n/a"}",
                    BuildFlavor = snapshot.Build?.Flavor ?? "unknown",
                    FirmwareVersion = snapshot.Build?.Version ?? "unknown",
                    DebugHidEnabled = snapshot.Build?.DebugHidEnabled ?? false,
                    SignedBootEnabled = snapshot.Build?.SignedBootEnabled ?? false,
                    AntiRollbackEnabled = snapshot.Build?.AntiRollbackEnabled ?? false,
                    BoardDetected = snapshot.Board?.Detected ?? false,
                    BoardCode = snapshot.Board?.Code ?? "0x00000000",
                    BoardSummary = snapshot.Board?.Summary ?? "board-id unavailable",
                    CredentialCount = snapshot.Credentials?.Count ?? 0,
                    CredentialCapacity = snapshot.Credentials?.Capacity ?? 0,
                    StoreFormatVersion = snapshot.Credentials?.StoreFormatVersion ?? 0,
                    PinConfigured = snapshot.Pin?.Configured ?? false,
                    PinRetries = snapshot.Pin?.Retries ?? 0,
                    UserPresenceEnabled = snapshot.UserPresence?.Enabled ?? false,
                    UserPresenceSource = snapshot.UserPresence?.Source ?? "unknown",
                    UserPresenceGpioPin = snapshot.UserPresence?.GpioPin ?? -1,
                    UserPresenceGpioActiveLow = snapshot.UserPresence?.GpioActiveLow ?? false,
                    UserPresenceTapCount = snapshot.UserPresence?.TapCount ?? 0,
                    UserPresenceGestureWindowMs = snapshot.UserPresence?.GestureWindowMs ?? 0,
                    UserPresenceRequestTimeoutMs = snapshot.UserPresence?.RequestTimeoutMs ?? 0,
                    UserPresenceSessionOverride = snapshot.UserPresence?.SessionOverride ?? false,
                    FidoHidAvailable = snapshot.Interfaces?.FidoHid ?? false,
                    ManagementAvailable = snapshot.Interfaces?.Management ?? false,
                    CtapConfigured = snapshot.Ctap?.Configured ?? false,
                    Transport = snapshot.Transport ?? "winusb-bulk-v1"
                });
            }
            catch
            {
                // Ignore devices that enumerate but fail the manager handshake.
            }
        }

        return devices;
    }

    private sealed class ManagerSnapshotResponse
    {
        [JsonPropertyName("deviceName")]
        public string? DeviceName { get; set; }

        [JsonPropertyName("productName")]
        public string? ProductName { get; set; }

        [JsonPropertyName("serial")]
        public string? Serial { get; set; }

        [JsonPropertyName("transport")]
        public string? Transport { get; set; }

        [JsonPropertyName("usb")]
        public UsbInfo? Usb { get; set; }

        [JsonPropertyName("build")]
        public BuildInfo? Build { get; set; }

        [JsonPropertyName("board")]
        public BoardInfo? Board { get; set; }

        [JsonPropertyName("credentials")]
        public CredentialInfo? Credentials { get; set; }

        [JsonPropertyName("pin")]
        public PinInfo? Pin { get; set; }

        [JsonPropertyName("userPresence")]
        public UserPresenceInfo? UserPresence { get; set; }

        [JsonPropertyName("interfaces")]
        public InterfaceInfo? Interfaces { get; set; }

        [JsonPropertyName("ctap")]
        public CtapInfo? Ctap { get; set; }
    }

    private sealed class UsbInfo
    {
        [JsonPropertyName("vid")]
        public string? Vid { get; set; }

        [JsonPropertyName("pid")]
        public string? Pid { get; set; }
    }

    private sealed class BuildInfo
    {
        [JsonPropertyName("flavor")]
        public string? Flavor { get; set; }

        [JsonPropertyName("version")]
        public string? Version { get; set; }

        [JsonPropertyName("debugHidEnabled")]
        public bool DebugHidEnabled { get; set; }

        [JsonPropertyName("signedBootEnabled")]
        public bool SignedBootEnabled { get; set; }

        [JsonPropertyName("antiRollbackEnabled")]
        public bool AntiRollbackEnabled { get; set; }
    }

    private sealed class BoardInfo
    {
        [JsonPropertyName("detected")]
        public bool Detected { get; set; }

        [JsonPropertyName("code")]
        public string? Code { get; set; }

        [JsonPropertyName("summary")]
        public string? Summary { get; set; }
    }

    private sealed class CredentialInfo
    {
        [JsonPropertyName("count")]
        public int Count { get; set; }

        [JsonPropertyName("capacity")]
        public int Capacity { get; set; }

        [JsonPropertyName("storeFormatVersion")]
        public int StoreFormatVersion { get; set; }
    }

    private sealed class PinInfo
    {
        [JsonPropertyName("configured")]
        public bool Configured { get; set; }

        [JsonPropertyName("retries")]
        public int Retries { get; set; }
    }

    private sealed class UserPresenceInfo
    {
        [JsonPropertyName("enabled")]
        public bool Enabled { get; set; }

        [JsonPropertyName("source")]
        public string? Source { get; set; }

        [JsonPropertyName("gpioPin")]
        public int GpioPin { get; set; }

        [JsonPropertyName("gpioActiveLow")]
        public bool GpioActiveLow { get; set; }

        [JsonPropertyName("tapCount")]
        public int TapCount { get; set; }

        [JsonPropertyName("gestureWindowMs")]
        public int GestureWindowMs { get; set; }

        [JsonPropertyName("requestTimeoutMs")]
        public int RequestTimeoutMs { get; set; }

        [JsonPropertyName("sessionOverride")]
        public bool SessionOverride { get; set; }
    }

    private sealed class InterfaceInfo
    {
        [JsonPropertyName("fidoHid")]
        public bool FidoHid { get; set; }

        [JsonPropertyName("management")]
        public bool Management { get; set; }
    }

    private sealed class CtapInfo
    {
        [JsonPropertyName("configured")]
        public bool Configured { get; set; }
    }
}
