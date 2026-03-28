using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;

internal static class Program
{
    private const byte SnapshotCommand = 0x01;
    private const byte PingCommand = 0x02;
    private const byte CredentialSummariesCommand = 0x03;
    private const byte SecurityStateCommand = 0x04;
    private const byte AuthorizeCommand = 0x05;
    private const byte DeleteCredentialCommand = 0x06;
    private const byte SetUserPresencePersistedCommand = 0x07;
    private const byte ClearUserPresenceSessionCommand = 0x09;
    private const byte UnsupportedCommand = 0x33;

    private const byte StatusOk = 0x00;
    private const byte StatusInvalidRequest = 0x01;
    private const byte StatusAuthRequired = 0x04;
    private const byte StatusConfirmationRequired = 0x05;
    private const ushort PermissionCredentialRead = 0x0004;
    private const ushort PermissionCredentialWrite = 0x0001;
    private const int AuthTokenBytes = 16;

    private static void Main()
    {
        Console.OutputEncoding = Encoding.UTF8;
        var paths = WinUsbRawTransport.EnumerateDevicePaths();
        Console.WriteLine($"[info] management paths: {paths.Count}");
        if (paths.Count == 0)
        {
            Console.WriteLine("[info] no WinUSB manager interface found.");
            return;
        }

        for (var i = 0; i < paths.Count; i++)
        {
            Console.WriteLine($"[path {i}] {paths[i]}");
        }

        var path = paths[0];
        Console.WriteLine($"[target] {path}");

        RunCase(path, "ping", PingCommand, Array.Empty<byte>());
        RunCase(path, "snapshot", SnapshotCommand, Array.Empty<byte>());
        RunCase(path, "security-state", SecurityStateCommand, Array.Empty<byte>());
        RunCase(path, "credential-summaries", CredentialSummariesCommand, BuildCredentialSummariesPayload(null, cursor: 0, limit: 16));
        RunCase(path, "credential-summaries-page-2", CredentialSummariesCommand, BuildCredentialSummariesPayload(null, cursor: 2, limit: 16));
        RunCase(path, "unsupported-command", UnsupportedCommand, Array.Empty<byte>());

        RunSummaryReadAuthorizationGateScenario(path);

        var fakeDeletePayload = new byte[18];
        fakeDeletePayload[16] = 0;
        fakeDeletePayload[17] = 0;
        var forgedDelete = RunCase(path, "delete-with-forged-token", DeleteCredentialCommand, fakeDeletePayload);
        ExpectStatus("delete-with-forged-token should require auth", forgedDelete.Status, StatusAuthRequired);

        var fakeUpPayload = new byte[24];
        fakeUpPayload[16] = 1; // source=bootsel
        fakeUpPayload[17] = 0xFF; // gpioPin=-1
        fakeUpPayload[18] = 1; // active low
        fakeUpPayload[19] = 2; // tap count
        fakeUpPayload[20] = 0xEE; // 750ms
        fakeUpPayload[21] = 0x02;
        fakeUpPayload[22] = 0x40; // 8000ms
        fakeUpPayload[23] = 0x1F;
        var forgedUpPersisted = RunCase(path, "set-up-persisted-with-forged-token", SetUserPresencePersistedCommand, fakeUpPayload);
        ExpectStatus("set-up-persisted-with-forged-token should require auth", forgedUpPersisted.Status, StatusAuthRequired);

        var fakeClearSessionPayload = new byte[16];
        var forgedClearSession = RunCase(path, "clear-session-with-forged-token", ClearUserPresenceSessionCommand, fakeClearSessionPayload);
        ExpectStatus("clear-session-with-forged-token should require auth", forgedClearSession.Status, StatusAuthRequired);

        RunCase(path, "authorize-perm-credential-write", AuthorizeCommand, BuildPermissionsPayload(PermissionCredentialWrite));
    }

    private static void RunSummaryReadAuthorizationGateScenario(string path)
    {
        Console.WriteLine("\n[scenario] credential-summaries auth gate");

        var unauthRead = RunCase(
            path,
            "credential-summaries-without-token-gate-check",
            CredentialSummariesCommand,
            BuildCredentialSummariesPayload(null, cursor: 0, limit: 16));
        if (unauthRead.Status == StatusOk)
        {
            Console.WriteLine("[expect-note] credential-summaries without token returned 0x00, so summary auth gate is not enabled on this firmware.");
            return;
        }

        if (unauthRead.Status != StatusAuthRequired)
        {
            Console.WriteLine($"[expect-fail] credential-summaries without token expected 0x{StatusAuthRequired:X2}, actual=0x{unauthRead.Status:X2}");
            return;
        }
        Console.WriteLine("[expect-pass] credential-summaries without token: status=0x04");

        var authorizeRead = RunCase(
            path,
            "authorize-perm-credential-read",
            AuthorizeCommand,
            BuildPermissionsPayload(PermissionCredentialRead));
        if (authorizeRead.Status == StatusConfirmationRequired)
        {
            Console.WriteLine("[note] credential-read authorization requires local confirmation; complete confirmation and rerun for the positive read path.");
            return;
        }

        if (authorizeRead.Status == StatusInvalidRequest)
        {
            Console.WriteLine("[expect-note] firmware rejected read permission authorization payload (status 0x01); this device likely does not implement credential-read authorization yet.");
            return;
        }

        if (authorizeRead.Status != StatusOk)
        {
            Console.WriteLine($"[expect-fail] authorize-perm-credential-read expected status=0x{StatusOk:X2} (or 0x{StatusConfirmationRequired:X2}), actual=0x{authorizeRead.Status:X2}");
            return;
        }

        if (!TryParseAuthorizationToken(authorizeRead.Payload, out var token))
        {
            Console.WriteLine("[expect-fail] authorize-perm-credential-read returned OK but token could not be parsed.");
            return;
        }

        var authedRead = RunCase(
            path,
            "credential-summaries-with-valid-token",
            CredentialSummariesCommand,
            BuildCredentialSummariesPayload(token, cursor: 0, limit: 16));
        ExpectStatus("credential-summaries with valid token", authedRead.Status, StatusOk);
    }

    private static byte[] BuildPermissionsPayload(ushort permissions)
    {
        return new byte[]
        {
            (byte)(permissions & 0xFF),
            (byte)(permissions >> 8)
        };
    }

    private static byte[] BuildCredentialSummariesPayload(byte[]? authToken, ushort cursor, ushort limit)
    {
        if (authToken is { Length: AuthTokenBytes })
        {
            var payload = new byte[AuthTokenBytes + 4];
            authToken.CopyTo(payload, 0);
            payload[AuthTokenBytes + 0] = (byte)(cursor & 0xFF);
            payload[AuthTokenBytes + 1] = (byte)(cursor >> 8);
            payload[AuthTokenBytes + 2] = (byte)(limit & 0xFF);
            payload[AuthTokenBytes + 3] = (byte)(limit >> 8);
            return payload;
        }

        return new byte[]
        {
            (byte)(cursor & 0xFF),
            (byte)(cursor >> 8),
            (byte)(limit & 0xFF),
            (byte)(limit >> 8)
        };
    }

    private static bool TryParseAuthorizationToken(byte[] payload, out byte[] token)
    {
        token = Array.Empty<byte>();
        var text = TryUtf8(payload);
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        try
        {
            using var json = JsonDocument.Parse(text);
            if (!json.RootElement.TryGetProperty("token", out var tokenProperty) ||
                tokenProperty.ValueKind != JsonValueKind.String)
            {
                return false;
            }

            var tokenHex = tokenProperty.GetString();
            if (string.IsNullOrWhiteSpace(tokenHex) || tokenHex.Length != (AuthTokenBytes * 2))
            {
                return false;
            }

            token = Convert.FromHexString(tokenHex);
            return token.Length == AuthTokenBytes;
        }
        catch
        {
            return false;
        }
    }

    private static void ExpectStatus(string label, byte actual, params byte[] expected)
    {
        foreach (var value in expected)
        {
            if (actual == value)
            {
                Console.WriteLine($"[expect-pass] {label}: status=0x{actual:X2}");
                return;
            }
        }

        var expectedJoined = string.Join(" or ", expected.Select(value => $"0x{value:X2}"));
        Console.WriteLine($"[expect-fail] {label}: expected {expectedJoined}, actual=0x{actual:X2}");
    }

    private static ManagerResponse RunCase(string path, string name, byte command, byte[] payload)
    {
        Console.WriteLine($"\n[test] {name} cmd=0x{command:X2} payload={payload.Length}");
        try
        {
            var response = WinUsbRawTransport.SendCommand(path, command, payload);
            Console.WriteLine($"[resp] status=0x{response.Status:X2} cmd=0x{response.Command:X2} payload={response.Payload.Length}");
            if (response.Payload.Length > 0)
            {
                var text = TryUtf8(response.Payload);
                if (!string.IsNullOrWhiteSpace(text))
                {
                    Console.WriteLine($"[resp-text] {TrimForConsole(text)}");
                }
                else
                {
                    Console.WriteLine($"[resp-hex] {Convert.ToHexString(response.Payload)}");
                }
            }

            return response;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[error] {ex.GetType().Name}: {ex.Message}");
            return new ManagerResponse(0xFF, command, Array.Empty<byte>());
        }
    }

    private static string TryUtf8(byte[] bytes)
    {
        try
        {
            return Encoding.UTF8.GetString(bytes);
        }
        catch
        {
            return string.Empty;
        }
    }

    private static string TrimForConsole(string text)
    {
        var normalized = text.Replace('\r', ' ').Replace('\n', ' ');
        return normalized.Length <= 2400 ? normalized : normalized[..2400] + "...";
    }
}

internal static class WinUsbRawTransport
{
    private static readonly Guid InterfaceGuid = new("9DE6E7C7-0F1E-4E9B-BD9C-7F6406C8A42B");
    private static readonly IntPtr InvalidHandleValue = new(-1);
    private static readonly byte[] Magic = "MKM1"u8.ToArray();

    private const uint DigcfPresent = 0x00000002;
    private const uint DigcfDeviceInterface = 0x00000010;
    private const uint GenericRead = 0x80000000;
    private const uint GenericWrite = 0x40000000;
    private const uint FileShareRead = 0x00000001;
    private const uint FileShareWrite = 0x00000002;
    private const uint OpenExisting = 3;
    private const uint FileAttributeNormal = 0x00000080;
    private const uint FileFlagOverlapped = 0x40000000;
    private const byte BulkOutPipeId = 0x03;
    private const byte BulkInPipeId = 0x83;
    private const uint PipeTransferTimeout = 0x03;
    private const byte ProtocolVersion = 0x01;
    private const int HeaderSize = 10;
    private const int ErrorNoMoreItems = 259;

    public static IReadOnlyList<string> EnumerateDevicePaths()
    {
        var paths = new List<string>();
        var guid = InterfaceGuid;
        var deviceInfoSet = SetupDiGetClassDevs(ref guid, IntPtr.Zero, IntPtr.Zero, DigcfPresent | DigcfDeviceInterface);
        if (deviceInfoSet == IntPtr.Zero || deviceInfoSet == InvalidHandleValue)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }

        try
        {
            var interfaceData = new SP_DEVICE_INTERFACE_DATA { cbSize = Marshal.SizeOf<SP_DEVICE_INTERFACE_DATA>() };
            uint index = 0;
            while (SetupDiEnumDeviceInterfaces(deviceInfoSet, IntPtr.Zero, ref guid, index, ref interfaceData))
            {
                paths.Add(GetDevicePath(deviceInfoSet, ref interfaceData));
                index++;
                interfaceData.cbSize = Marshal.SizeOf<SP_DEVICE_INTERFACE_DATA>();
            }

            var error = Marshal.GetLastWin32Error();
            if (error != ErrorNoMoreItems)
            {
                throw new Win32Exception(error);
            }
        }
        finally
        {
            _ = SetupDiDestroyDeviceInfoList(deviceInfoSet);
        }

        return paths;
    }

    public static ManagerResponse SendCommand(string devicePath, byte command, ReadOnlySpan<byte> payload)
    {
        using var deviceHandle = CreateFile(
            devicePath,
            GenericRead | GenericWrite,
            FileShareRead | FileShareWrite,
            IntPtr.Zero,
            OpenExisting,
            FileAttributeNormal | FileFlagOverlapped,
            IntPtr.Zero);
        if (deviceHandle.IsInvalid)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), $"Unable to open device path: {devicePath}");
        }

        if (!WinUsb_Initialize(deviceHandle, out var winUsbHandle) || winUsbHandle == IntPtr.Zero)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "WinUSB initialization failed.");
        }

        try
        {
            SetPipeTimeout(winUsbHandle, BulkInPipeId, 15000);
            SetPipeTimeout(winUsbHandle, BulkOutPipeId, 15000);
            _ = WinUsb_ResetPipe(winUsbHandle, BulkInPipeId);

            var request = BuildRequest(command, payload);
            if (!WinUsb_WritePipe(winUsbHandle, BulkOutPipeId, request, (uint)request.Length, out var written, IntPtr.Zero) ||
                written != (uint)request.Length)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "WritePipe failed.");
            }

            var response = ReadResponse(winUsbHandle);
            return ParseResponse(response, command);
        }
        finally
        {
            WinUsb_Free(winUsbHandle);
        }
    }

    private static void SetPipeTimeout(IntPtr handle, byte pipeId, uint timeoutMs)
    {
        _ = WinUsb_SetPipePolicy(handle, pipeId, PipeTransferTimeout, sizeof(uint), ref timeoutMs);
    }

    private static byte[] BuildRequest(byte command, ReadOnlySpan<byte> payload)
    {
        var request = new byte[HeaderSize + payload.Length];
        Array.Copy(Magic, 0, request, 0, Magic.Length);
        request[4] = ProtocolVersion;
        request[5] = command;
        request[6] = 0;
        request[7] = 0;
        var payloadLength = (ushort)payload.Length;
        request[8] = (byte)(payloadLength & 0xff);
        request[9] = (byte)(payloadLength >> 8);
        if (!payload.IsEmpty)
        {
            payload.CopyTo(request.AsSpan(HeaderSize));
        }
        return request;
    }

    private static byte[] ReadResponse(IntPtr handle)
    {
        var responseBuffer = new byte[1024];
        var readBuffer = new byte[256];
        var totalRead = 0;
        var expectedTotal = HeaderSize;

        while (totalRead < expectedTotal)
        {
            if (!WinUsb_ReadPipe(handle, BulkInPipeId, readBuffer, (uint)readBuffer.Length, out var read, IntPtr.Zero))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "ReadPipe failed.");
            }

            if (read == 0)
            {
                throw new InvalidOperationException("Empty response.");
            }

            if (totalRead + (int)read > responseBuffer.Length)
            {
                throw new InvalidOperationException("Response exceeded fixed buffer.");
            }

            Array.Copy(readBuffer, 0, responseBuffer, totalRead, (int)read);
            totalRead += (int)read;
            if (totalRead >= HeaderSize)
            {
                var payloadLength = (ushort)(responseBuffer[8] | (responseBuffer[9] << 8));
                expectedTotal = HeaderSize + payloadLength;
            }
        }

        return responseBuffer.AsSpan(0, totalRead).ToArray();
    }

    private static ManagerResponse ParseResponse(byte[] response, byte expectedCommand)
    {
        if (response.Length < HeaderSize)
        {
            throw new InvalidOperationException("Truncated response header.");
        }

        if (response[0] != Magic[0] || response[1] != Magic[1] || response[2] != Magic[2] || response[3] != Magic[3])
        {
            throw new InvalidOperationException("Response magic mismatch.");
        }

        if (response[4] != ProtocolVersion)
        {
            throw new InvalidOperationException($"Protocol mismatch: {response[4]}");
        }

        var status = response[5];
        var command = response[6];
        var payloadLength = (ushort)(response[8] | (response[9] << 8));
        if (HeaderSize + payloadLength > response.Length)
        {
            throw new InvalidOperationException("Truncated payload.");
        }
        if (command != expectedCommand)
        {
            throw new InvalidOperationException($"Unexpected command in response: 0x{command:X2}");
        }

        var payload = response.AsSpan(HeaderSize, payloadLength).ToArray();
        return new ManagerResponse(status, command, payload);
    }

    private static string GetDevicePath(IntPtr deviceInfoSet, ref SP_DEVICE_INTERFACE_DATA interfaceData)
    {
        SetupDiGetDeviceInterfaceDetail(deviceInfoSet, ref interfaceData, IntPtr.Zero, 0, out var requiredSize, IntPtr.Zero);
        var detailBuffer = Marshal.AllocHGlobal((int)requiredSize);
        try
        {
            Marshal.WriteInt32(detailBuffer, IntPtr.Size == 8 ? 8 : 6);
            if (!SetupDiGetDeviceInterfaceDetail(deviceInfoSet, ref interfaceData, detailBuffer, requiredSize, out _, IntPtr.Zero))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

            return Marshal.PtrToStringUni(IntPtr.Add(detailBuffer, 4)) ?? string.Empty;
        }
        finally
        {
            Marshal.FreeHGlobal(detailBuffer);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct SP_DEVICE_INTERFACE_DATA
    {
        public int cbSize;
        public Guid InterfaceClassGuid;
        public int Flags;
        public IntPtr Reserved;
    }

    [DllImport("setupapi.dll", SetLastError = true)]
    private static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator, IntPtr hwndParent, uint flags);

    [DllImport("setupapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiEnumDeviceInterfaces(
        IntPtr deviceInfoSet,
        IntPtr deviceInfoData,
        ref Guid interfaceClassGuid,
        uint memberIndex,
        ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiGetDeviceInterfaceDetail(
        IntPtr deviceInfoSet,
        ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData,
        IntPtr deviceInterfaceDetailData,
        uint deviceInterfaceDetailDataSize,
        out uint requiredSize,
        IntPtr deviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern SafeFileHandle CreateFile(
        string fileName,
        uint desiredAccess,
        uint shareMode,
        IntPtr securityAttributes,
        uint creationDisposition,
        uint flagsAndAttributes,
        IntPtr templateFile);

    [DllImport("winusb.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WinUsb_Initialize(SafeFileHandle deviceHandle, out IntPtr interfaceHandle);

    [DllImport("winusb.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WinUsb_Free(IntPtr interfaceHandle);

    [DllImport("winusb.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WinUsb_WritePipe(
        IntPtr interfaceHandle,
        byte pipeId,
        byte[] buffer,
        uint bufferLength,
        out uint lengthTransferred,
        IntPtr overlapped);

    [DllImport("winusb.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WinUsb_ReadPipe(
        IntPtr interfaceHandle,
        byte pipeId,
        byte[] buffer,
        uint bufferLength,
        out uint lengthTransferred,
        IntPtr overlapped);

    [DllImport("winusb.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WinUsb_SetPipePolicy(
        IntPtr interfaceHandle,
        byte pipeId,
        uint policyType,
        uint valueLength,
        ref uint value);

    [DllImport("winusb.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WinUsb_ResetPipe(IntPtr interfaceHandle, byte pipeId);
}

internal sealed record ManagerResponse(byte Status, byte Command, byte[] Payload);
