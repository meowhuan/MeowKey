# MeowKey Manager WinUSB Driver

## English

This package binds the MeowKey management interface (`USB\\VID_CAFE&PID_4005&MI_01`) to `WinUSB`.

Files:

- `meowkey-manager-winusb.inf`
- `meowkey-manager-winusb.cat` (signed)
- `meowkey-manager-winusb.cer` (optional, signer certificate)
- `install-manager-driver.ps1`
- `install-manager-driver.cmd`
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
pwsh .\sign-manager-driver.ps1 -PfxPath .\driver-signing.pfx -PfxPassword "<password>" -TimestampUrl "http://timestamp.digicert.com"
```

If your environment does not include `Inf2Cat.exe`, you can sign the existing catalog without regeneration:

```powershell
pwsh .\sign-manager-driver.ps1 -PfxPath .\driver-signing.pfx -PfxPassword "<password>" -TimestampUrl "http://timestamp.digicert.com" -SkipCatalogGeneration
```

To avoid INF/CAT hash mismatch risks when `Inf2Cat.exe` is unavailable, you can ask the script to generate a fallback catalog via `MakeCat.exe`:

```powershell
pwsh .\sign-manager-driver.ps1 -PfxPath .\driver-signing.pfx -PfxPassword "<password>" -TimestampUrl "http://timestamp.digicert.com" -UseMakeCatFallback
```

For downloaded ZIP packages, prefer:

```cmd
install-manager-driver.cmd
```

If PowerShell reports that `install-manager-driver.ps1` is not digitally signed, remove Mark-of-the-Web metadata first:

```powershell
Get-ChildItem -Recurse -File | Unblock-File
```

`install-manager-driver.ps1` must run as Administrator. If `meowkey-manager-winusb.cer` is present, the script imports it into `LocalMachine\Root` and `LocalMachine\TrustedPublisher` before `pnputil`.

Important:

- Right-click `INF` install only works when `meowkey-manager-winusb.cat` is properly signed and trusted on the current machine.
- If catalog signing or trust is missing, Windows may show a hash/catalog mismatch style error during install.

The desktop manager enumerates the interface GUID:

- `{9DE6E7C7-0F1E-4E9B-BD9C-7F6406C8A42B}`

## 中文

这个驱动包会把 MeowKey 正式管理接口（`USB\\VID_CAFE&PID_4005&MI_01`）绑定到 `WinUSB`。

包含文件：

- `meowkey-manager-winusb.inf`
- `meowkey-manager-winusb.cat`（已签名）
- `meowkey-manager-winusb.cer`（可选，签名证书）
- `install-manager-driver.ps1`
- `install-manager-driver.cmd`
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
pwsh .\sign-manager-driver.ps1 -PfxPath .\driver-signing.pfx -PfxPassword "<password>" -TimestampUrl "http://timestamp.digicert.com"
```

如果环境里没有 `Inf2Cat.exe`，也可以跳过重建目录文件，仅对现有目录文件签名：

```powershell
pwsh .\sign-manager-driver.ps1 -PfxPath .\driver-signing.pfx -PfxPassword "<password>" -TimestampUrl "http://timestamp.digicert.com" -SkipCatalogGeneration
```

如果环境里没有 `Inf2Cat.exe`，为避免 INF/CAT 哈希不匹配，建议用 `MakeCat.exe` 回退方案重新生成目录文件：

```powershell
pwsh .\sign-manager-driver.ps1 -PfxPath .\driver-signing.pfx -PfxPassword "<password>" -TimestampUrl "http://timestamp.digicert.com" -UseMakeCatFallback
```

对于下载得到的 ZIP 包，建议优先运行：

```cmd
install-manager-driver.cmd
```

如果 PowerShell 提示 `install-manager-driver.ps1` 未数字签名，先移除下载来源标记（Mark-of-the-Web）：

```powershell
Get-ChildItem -Recurse -File | Unblock-File
```

`install-manager-driver.ps1` 需要在管理员权限下运行。如果包内存在 `meowkey-manager-winusb.cer`，脚本会在调用 `pnputil` 前自动导入到 `LocalMachine\Root` 和 `LocalMachine\TrustedPublisher`。

注意：

- 只有当 `meowkey-manager-winusb.cat` 已正确签名且当前机器信任签名证书时，右键 `INF` 安装才会成功。
- 如果目录文件签名或信任链不满足，Windows 安装时会出现“哈希/目录文件不匹配”一类错误提示。

桌面管理器会按下面这个接口 GUID 枚举：

- `{9DE6E7C7-0F1E-4E9B-BD9C-7F6406C8A42B}`
