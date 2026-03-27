using System.Buffers.Binary;
using System.Text.Json;
using System.Text.Json.Serialization;
using MeowKey.Manager.Models;

namespace MeowKey.Manager.Services;

public sealed class ManagerDeviceService
{
    private const byte GetSnapshotCommand = 0x01;
    private const byte GetCredentialSummariesCommand = 0x03;
    private const byte GetSecurityStateCommand = 0x04;
    private const ushort CredentialPageLimit = 16;

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
                var snapshot = ReadJson<ManagerSnapshotResponse>(devicePath, GetSnapshotCommand, Array.Empty<byte>())
                    ?? throw new InvalidOperationException("Manager snapshot payload could not be parsed.");
                var credentialCatalog = TryReadCredentialCatalog(devicePath, out var credentialCatalogAvailable);
                var securityState = TryReadSecurityState(devicePath, out var securityStateAvailable);

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
                    AntiRollbackVersion = snapshot.Build?.AntiRollbackVersion ?? 0,
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
                    Transport = snapshot.Transport ?? "winusb-bulk-v1",
                    CredentialCatalogAvailable = credentialCatalogAvailable,
                    CredentialCatalog = credentialCatalog,
                    SecurityStateAvailable = securityStateAvailable,
                    SecurityState = securityState
                });
            }
            catch
            {
                // Ignore devices that enumerate but fail the manager handshake.
            }
        }

        return devices;
    }

    private static T? ReadJson<T>(string devicePath, byte command, ReadOnlySpan<byte> payload)
    {
        var json = WinUsbManagerTransport.SendCommand(devicePath, command, payload);
        return JsonSerializer.Deserialize<T>(json, JsonOptions);
    }

    private static IReadOnlyList<CredentialSummaryInfo> TryReadCredentialCatalog(string devicePath, out bool available)
    {
        var items = new List<CredentialSummaryInfo>();
        ushort cursor = 0;

        try
        {
            while (true)
            {
                var payload = BuildCredentialCatalogPayload(cursor, CredentialPageLimit);
                var page = ReadJson<CredentialCatalogResponse>(devicePath, GetCredentialSummariesCommand, payload)
                    ?? throw new InvalidOperationException("Credential catalog payload could not be parsed.");

                if (page.Items is not null)
                {
                    items.AddRange(page.Items.Select(MapCredentialSummary));
                }

                if (!page.HasMore)
                {
                    available = true;
                    return items;
                }

                if (page.NextCursor <= cursor)
                {
                    throw new InvalidOperationException("Credential catalog pagination did not advance.");
                }

                cursor = (ushort)page.NextCursor;
            }
        }
        catch
        {
            available = false;
            return Array.Empty<CredentialSummaryInfo>();
        }
    }

    private static SecurityStateInfo? TryReadSecurityState(string devicePath, out bool available)
    {
        try
        {
            var state = ReadJson<SecurityStateResponse>(devicePath, GetSecurityStateCommand, Array.Empty<byte>())
                ?? throw new InvalidOperationException("Security state payload could not be parsed.");
            available = true;
            return MapSecurityState(state);
        }
        catch
        {
            available = false;
            return null;
        }
    }

    private static byte[] BuildCredentialCatalogPayload(ushort cursor, ushort limit)
    {
        var payload = new byte[4];
        BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(0, 2), cursor);
        BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(2, 2), limit);
        return payload;
    }

    private static CredentialSummaryInfo MapCredentialSummary(CredentialCatalogEntryResponse entry)
    {
        return new CredentialSummaryInfo
        {
            Slot = entry.Slot,
            CredentialIdLength = entry.CredentialIdLength,
            CredentialIdPrefix = entry.CredentialIdPrefix ?? string.Empty,
            SignCount = entry.SignCount,
            Discoverable = entry.Discoverable,
            CredRandomReady = entry.CredRandomReady,
            RpIdPreview = entry.RpIdPreview ?? string.Empty,
            RpIdLength = entry.RpIdLength,
            UserNamePreview = entry.UserNamePreview ?? string.Empty,
            UserNameLength = entry.UserNameLength,
            DisplayNamePreview = entry.DisplayNamePreview ?? string.Empty,
            DisplayNameLength = entry.DisplayNameLength
        };
    }

    private static SecurityStateInfo MapSecurityState(SecurityStateResponse state)
    {
        return new SecurityStateInfo
        {
            BuildFlavor = state.Build?.Flavor ?? "unknown",
            BuildVersion = state.Build?.Version ?? "unknown",
            SignedBootEnabled = state.Build?.SignedBootEnabled ?? false,
            AntiRollbackEnabled = state.Build?.AntiRollbackEnabled ?? false,
            AntiRollbackVersion = state.Build?.AntiRollbackVersion ?? 0,
            BoardDetected = state.Board?.Detected ?? false,
            BoardCode = state.Board?.Code ?? "0x00000000",
            BoardSummary = state.Board?.Summary ?? "board-id unavailable",
            DebugHidEnabled = state.Interfaces?.DebugHid ?? false,
            CtapConfigured = state.Ctap?.Configured ?? false,
            PinConfigured = state.Pin?.Configured ?? false,
            PinRetries = state.Pin?.Retries ?? 0,
            UserPresenceSessionOverride = state.UserPresence?.SessionOverride ?? false,
            EffectiveUserPresence = MapUserPresence(state.UserPresence?.Effective),
            PersistedUserPresence = MapUserPresence(state.UserPresence?.Persisted),
            BootFlags0 = new OtpBootFlags0Info
            {
                Available = state.Otp?.BootFlags0?.Available ?? false,
                Raw = state.Otp?.BootFlags0?.Raw ?? "0x000000",
                RollbackRequired = state.Otp?.BootFlags0?.RollbackRequired ?? false,
                FlashBootDisabled = state.Otp?.BootFlags0?.FlashBootDisabled ?? false,
                PicobootDisabled = state.Otp?.BootFlags0?.PicobootDisabled ?? false
            },
            BootFlags1 = new OtpBootFlags1Info
            {
                Available = state.Otp?.BootFlags1?.Available ?? false,
                Raw = state.Otp?.BootFlags1?.Raw ?? "0x000000",
                KeyValidMask = state.Otp?.BootFlags1?.KeyValidMask ?? "0x0",
                KeyInvalidMask = state.Otp?.BootFlags1?.KeyInvalidMask ?? "0x0"
            }
        };
    }

    private static UserPresenceConfigInfo MapUserPresence(UserPresenceConfigResponse? config)
    {
        return new UserPresenceConfigInfo
        {
            Enabled = config?.Enabled ?? false,
            Source = config?.Source ?? "unknown",
            GpioPin = config?.GpioPin ?? -1,
            GpioActiveLow = config?.GpioActiveLow ?? false,
            TapCount = config?.TapCount ?? 0,
            GestureWindowMs = config?.GestureWindowMs ?? 0,
            RequestTimeoutMs = config?.RequestTimeoutMs ?? 0
        };
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

    private sealed class CredentialCatalogResponse
    {
        [JsonPropertyName("items")]
        public IReadOnlyList<CredentialCatalogEntryResponse>? Items { get; set; }

        [JsonPropertyName("hasMore")]
        public bool HasMore { get; set; }

        [JsonPropertyName("nextCursor")]
        public int NextCursor { get; set; }
    }

    private sealed class CredentialCatalogEntryResponse
    {
        [JsonPropertyName("slot")]
        public int Slot { get; set; }

        [JsonPropertyName("credentialIdLength")]
        public int CredentialIdLength { get; set; }

        [JsonPropertyName("credentialIdPrefix")]
        public string? CredentialIdPrefix { get; set; }

        [JsonPropertyName("signCount")]
        public int SignCount { get; set; }

        [JsonPropertyName("discoverable")]
        public bool Discoverable { get; set; }

        [JsonPropertyName("credRandomReady")]
        public bool CredRandomReady { get; set; }

        [JsonPropertyName("rpIdPreview")]
        public string? RpIdPreview { get; set; }

        [JsonPropertyName("rpIdLength")]
        public int RpIdLength { get; set; }

        [JsonPropertyName("userNamePreview")]
        public string? UserNamePreview { get; set; }

        [JsonPropertyName("userNameLength")]
        public int UserNameLength { get; set; }

        [JsonPropertyName("displayNamePreview")]
        public string? DisplayNamePreview { get; set; }

        [JsonPropertyName("displayNameLength")]
        public int DisplayNameLength { get; set; }
    }

    private sealed class SecurityStateResponse
    {
        [JsonPropertyName("build")]
        public BuildInfo? Build { get; set; }

        [JsonPropertyName("board")]
        public BoardInfo? Board { get; set; }

        [JsonPropertyName("interfaces")]
        public InterfaceInfo? Interfaces { get; set; }

        [JsonPropertyName("ctap")]
        public CtapInfo? Ctap { get; set; }

        [JsonPropertyName("pin")]
        public PinInfo? Pin { get; set; }

        [JsonPropertyName("userPresence")]
        public SecurityUserPresenceResponse? UserPresence { get; set; }

        [JsonPropertyName("otp")]
        public OtpResponse? Otp { get; set; }
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

        [JsonPropertyName("antiRollbackVersion")]
        public int AntiRollbackVersion { get; set; }
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

    private sealed class SecurityUserPresenceResponse
    {
        [JsonPropertyName("sessionOverride")]
        public bool SessionOverride { get; set; }

        [JsonPropertyName("effective")]
        public UserPresenceConfigResponse? Effective { get; set; }

        [JsonPropertyName("persisted")]
        public UserPresenceConfigResponse? Persisted { get; set; }
    }

    private sealed class UserPresenceConfigResponse
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
    }

    private sealed class InterfaceInfo
    {
        [JsonPropertyName("fidoHid")]
        public bool FidoHid { get; set; }

        [JsonPropertyName("management")]
        public bool Management { get; set; }

        [JsonPropertyName("debugHid")]
        public bool DebugHid { get; set; }
    }

    private sealed class CtapInfo
    {
        [JsonPropertyName("configured")]
        public bool Configured { get; set; }
    }

    private sealed class OtpResponse
    {
        [JsonPropertyName("bootFlags0")]
        public OtpBootFlags0Response? BootFlags0 { get; set; }

        [JsonPropertyName("bootFlags1")]
        public OtpBootFlags1Response? BootFlags1 { get; set; }
    }

    private sealed class OtpBootFlags0Response
    {
        [JsonPropertyName("available")]
        public bool Available { get; set; }

        [JsonPropertyName("raw")]
        public string? Raw { get; set; }

        [JsonPropertyName("rollbackRequired")]
        public bool RollbackRequired { get; set; }

        [JsonPropertyName("flashBootDisabled")]
        public bool FlashBootDisabled { get; set; }

        [JsonPropertyName("picobootDisabled")]
        public bool PicobootDisabled { get; set; }
    }

    private sealed class OtpBootFlags1Response
    {
        [JsonPropertyName("available")]
        public bool Available { get; set; }

        [JsonPropertyName("raw")]
        public string? Raw { get; set; }

        [JsonPropertyName("keyValidMask")]
        public string? KeyValidMask { get; set; }

        [JsonPropertyName("keyInvalidMask")]
        public string? KeyInvalidMask { get; set; }
    }
}
