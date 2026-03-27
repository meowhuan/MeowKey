using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace MeowKey.Manager.Services;

internal static class WinUsbManagerTransport
{
    private static readonly Guid InterfaceGuid = new("9DE6E7C7-0F1E-4E9B-BD9C-7F6406C8A42B");

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
    private const int RequestHeaderSize = 10;
    private const int ResponseHeaderSize = 10;
    private const byte ProtocolVersion = 0x01;
    private static readonly byte[] Magic = "MKM1"u8.ToArray();

    public static IReadOnlyList<string> EnumerateDevicePaths()
    {
        var paths = new List<string>();
        var interfaceGuid = InterfaceGuid;
        var deviceInfoSet = SetupDiGetClassDevs(ref interfaceGuid, IntPtr.Zero, IntPtr.Zero, DigcfPresent | DigcfDeviceInterface);
        if (deviceInfoSet == IntPtr.Zero || deviceInfoSet == INVALID_HANDLE_VALUE) {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }

        try
        {
            var interfaceData = new SP_DEVICE_INTERFACE_DATA { cbSize = Marshal.SizeOf<SP_DEVICE_INTERFACE_DATA>() };
            uint index = 0;
            while (SetupDiEnumDeviceInterfaces(deviceInfoSet, IntPtr.Zero, ref interfaceGuid, index, ref interfaceData))
            {
                paths.Add(GetDevicePath(deviceInfoSet, ref interfaceData));
                index++;
                interfaceData.cbSize = Marshal.SizeOf<SP_DEVICE_INTERFACE_DATA>();
            }

            var error = Marshal.GetLastWin32Error();
            if (error != ERROR_NO_MORE_ITEMS)
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

    public static string SendCommand(string devicePath, byte command, ReadOnlySpan<byte> payload)
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
            SetPipeTimeout(winUsbHandle, BulkInPipeId, 2000);
            SetPipeTimeout(winUsbHandle, BulkOutPipeId, 2000);

            var request = BuildRequest(command, payload);
            if (!WinUsb_WritePipe(winUsbHandle, BulkOutPipeId, request, (uint)request.Length, out var written, IntPtr.Zero) || written != (uint)request.Length)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Manager request write failed.");
            }

            var response = ReadResponse(winUsbHandle);
            return ParseResponse(response, command);
        }
        finally
        {
            WinUsb_Free(winUsbHandle);
        }
    }

    private static void SetPipeTimeout(IntPtr winUsbHandle, byte pipeId, uint timeoutMs)
    {
        _ = WinUsb_SetPipePolicy(winUsbHandle, pipeId, PipeTransferTimeout, sizeof(uint), ref timeoutMs);
    }

    private static byte[] BuildRequest(byte command, ReadOnlySpan<byte> payload)
    {
        var request = new byte[RequestHeaderSize + payload.Length];
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
            payload.CopyTo(request.AsSpan((int)RequestHeaderSize));
        }

        return request;
    }

    private static byte[] ReadResponse(IntPtr winUsbHandle)
    {
        var responseBuffer = new byte[1024];
        var readBuffer = new byte[256];
        var totalRead = 0;
        var expectedTotal = ResponseHeaderSize;

        while (totalRead < expectedTotal)
        {
            if (!WinUsb_ReadPipe(winUsbHandle, BulkInPipeId, readBuffer, (uint)readBuffer.Length, out var read, IntPtr.Zero))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Manager response read failed.");
            }

            if (read == 0)
            {
                throw new InvalidOperationException("Manager response was empty.");
            }

            if (totalRead + (int)read > responseBuffer.Length)
            {
                throw new InvalidOperationException("Manager response exceeded the fixed receive buffer.");
            }

            Array.Copy(readBuffer, 0, responseBuffer, totalRead, (int)read);
            totalRead += (int)read;
            if (totalRead >= ResponseHeaderSize)
            {
                var payloadLength = (ushort)(responseBuffer[8] | (responseBuffer[9] << 8));
                expectedTotal = ResponseHeaderSize + payloadLength;
                if (expectedTotal > responseBuffer.Length)
                {
                    throw new InvalidOperationException("Manager response exceeded the fixed receive buffer.");
                }
            }
        }

        return responseBuffer.AsSpan(0, totalRead).ToArray();
    }

    private static string ParseResponse(byte[] response, byte command)
    {
        if (response.Length < ResponseHeaderSize)
        {
            throw new InvalidOperationException("Manager response header was truncated.");
        }

        if (response[0] != Magic[0] || response[1] != Magic[1] || response[2] != Magic[2] || response[3] != Magic[3])
        {
            throw new InvalidOperationException("Manager response magic did not match.");
        }

        if (response[4] != ProtocolVersion)
        {
            throw new InvalidOperationException($"Unsupported manager protocol version: {response[4]}");
        }

        var status = response[5];
        var responseCommand = response[6];
        if (responseCommand != command)
        {
            throw new InvalidOperationException($"Unexpected manager response command: {responseCommand}");
        }

        if (status != 0)
        {
            throw new InvalidOperationException($"Manager command failed with status 0x{status:x2}.");
        }

        var payloadLength = (ushort)(response[8] | (response[9] << 8));
        if (ResponseHeaderSize + payloadLength > response.Length)
        {
            throw new InvalidOperationException("Manager response payload was truncated.");
        }

        return System.Text.Encoding.UTF8.GetString(response, (int)ResponseHeaderSize, payloadLength);
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

            // The variable-length DevicePath string starts immediately after cbSize.
            // cbSize must be 8 on x64 and 6 on x86 for the API call, but the first UTF-16
            // character is still located at byte offset 4 in the returned buffer.
            const int offset = 4;
            return Marshal.PtrToStringUni(IntPtr.Add(detailBuffer, offset)) ?? string.Empty;
        }
        finally
        {
            Marshal.FreeHGlobal(detailBuffer);
        }
    }

    private static readonly IntPtr INVALID_HANDLE_VALUE = new(-1);
    private const int ERROR_NO_MORE_ITEMS = 259;

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
    private static extern bool SetupDiEnumDeviceInterfaces(IntPtr deviceInfoSet, IntPtr deviceInfoData, ref Guid interfaceClassGuid, uint memberIndex, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr deviceInfoSet, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData, IntPtr deviceInterfaceDetailData, uint deviceInterfaceDetailDataSize, out uint requiredSize, IntPtr deviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern SafeFileHandle CreateFile(string fileName, uint desiredAccess, uint shareMode, IntPtr securityAttributes, uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

    [DllImport("winusb.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WinUsb_Initialize(SafeFileHandle deviceHandle, out IntPtr interfaceHandle);

    [DllImport("winusb.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WinUsb_Free(IntPtr interfaceHandle);

    [DllImport("winusb.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WinUsb_WritePipe(IntPtr interfaceHandle, byte pipeId, byte[] buffer, uint bufferLength, out uint lengthTransferred, IntPtr overlapped);

    [DllImport("winusb.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WinUsb_ReadPipe(IntPtr interfaceHandle, byte pipeId, byte[] buffer, uint bufferLength, out uint lengthTransferred, IntPtr overlapped);

    [DllImport("winusb.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WinUsb_SetPipePolicy(IntPtr interfaceHandle, byte pipeId, uint policyType, uint valueLength, ref uint value);
}
