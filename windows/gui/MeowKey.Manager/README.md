# MeowKey Manager

## English

This directory contains the primary Windows desktop manager shell for MeowKey.

- UI stack: `WinUI 3`
- Layout direction: aligned with the `Android-Cam-Bridge` desktop shell rhythm
- Localization: `zh-CN / en-US`, with automatic selection from the system UI language and Chinese as the fallback
- Formal management channel: `WinUSB` on `USB\\VID_CAFE&PID_4005&MI_01`
- Primary sections:
  - `Overview`
  - `Devices`
  - `Credentials`
  - `Security`
  - `Debug`
  - `About`

Run from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-manager.ps1 -Configuration Release
```

Linux direction:

- keep the Linux-facing desktop shell in `native-rs/meowkey-manager`
- continue using `Rust + egui/eframe`
- mirror the same information architecture instead of introducing a second unfinished native widget stack
- Windows driver package lives in `windows/driver/manager-winusb/`

## 中文

这个目录是 MeowKey 当前的 Windows 主管理器壳层。

- UI 技术栈：`WinUI 3`
- 布局方向：参考 `Android-Cam-Bridge` 的桌面管理器节奏
- 本地化：支持 `zh-CN / en-US`，默认跟随系统 `UI language` 自动选择，未匹配时回落到中文
- 正式管理通道：`USB\\VID_CAFE&PID_4005&MI_01` 上的 `WinUSB`
- 主分区：
  - `Overview`
  - `Devices`
  - `Credentials`
  - `Security`
  - `Debug`
  - `About`

从仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-manager.ps1 -Configuration Release
```

Linux 方向：

- Linux 侧桌面壳层继续放在 `native-rs/meowkey-manager`
- 继续使用 `Rust + egui/eframe`
- 保持和 Windows 一致的信息架构，而不是再引入一套新的、尚未落地的原生控件栈
- Windows 驱动包位于 `windows/driver/manager-winusb/`
