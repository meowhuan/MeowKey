# MeowKey Manager WinUSB Driver

## English

This package binds the MeowKey management interface (`USB\\VID_CAFE&PID_4005&MI_01`) to `WinUSB`.

Files:

- `meowkey-manager-winusb.inf`
- `install-manager-driver.ps1`
- `new-manager-driver-cert.ps1`
- `sign-manager-driver.ps1`

Typical local test flow:

```powershell
pwsh .\new-manager-driver-cert.ps1 -InstallToTrustedRoots
pwsh .\sign-manager-driver.ps1
pwsh .\install-manager-driver.ps1
```

The desktop manager enumerates the interface GUID:

- `{9DE6E7C7-0F1E-4E9B-BD9C-7F6406C8A42B}`

## 中文

这个驱动包会把 MeowKey 正式管理接口（`USB\\VID_CAFE&PID_4005&MI_01`）绑定到 `WinUSB`。

包含文件：

- `meowkey-manager-winusb.inf`
- `install-manager-driver.ps1`
- `new-manager-driver-cert.ps1`
- `sign-manager-driver.ps1`

本地测试常用流程：

```powershell
pwsh .\new-manager-driver-cert.ps1 -InstallToTrustedRoots
pwsh .\sign-manager-driver.ps1
pwsh .\install-manager-driver.ps1
```

桌面管理器会按下面这个接口 GUID 枚举：

- `{9DE6E7C7-0F1E-4E9B-BD9C-7F6406C8A42B}`
