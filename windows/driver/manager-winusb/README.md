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

CI/release signing with a PFX is also supported:

```powershell
pwsh .\sign-manager-driver.ps1 -PfxPath .\driver-signing.pfx -PfxPassword "<password>"
```

If your environment does not include `Inf2Cat.exe`, you can sign the existing catalog without regeneration:

```powershell
pwsh .\sign-manager-driver.ps1 -PfxPath .\driver-signing.pfx -PfxPassword "<password>" -SkipCatalogGeneration
```

Important:

- Right-click `INF` install only works when `meowkey-manager-winusb.cat` is properly signed and trusted on the current machine.
- If catalog signing or trust is missing, Windows may show a hash/catalog mismatch style error during install.

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

也支持在 CI/release 中使用 PFX 签名：

```powershell
pwsh .\sign-manager-driver.ps1 -PfxPath .\driver-signing.pfx -PfxPassword "<password>"
```

如果环境里没有 `Inf2Cat.exe`，也可以跳过重建目录文件，仅对现有目录文件签名：

```powershell
pwsh .\sign-manager-driver.ps1 -PfxPath .\driver-signing.pfx -PfxPassword "<password>" -SkipCatalogGeneration
```

注意：

- 只有当 `meowkey-manager-winusb.cat` 已正确签名且当前机器信任签名证书时，右键 `INF` 安装才会成功。
- 如果目录文件签名或信任链不满足，Windows 安装时会出现“哈希/目录文件不匹配”一类错误提示。

桌面管理器会按下面这个接口 GUID 枚举：

- `{9DE6E7C7-0F1E-4E9B-BD9C-7F6406C8A42B}`
