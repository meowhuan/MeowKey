# MeowKey Manager

## English

MeowKey Manager is the current Linux-facing native manager shell and maintenance workbench for the firmware, built with `Rust + egui/eframe`.

### Current Features

- connect to the formal management bulk interface
- connect to a real Debug HID device
- fall back to a preview backend
- read formal credential summaries (`0x03`)
- read formal security state (`0x04`)
- request short-lived formal authorization (`0x05`)
- delete one credential by slot on formal channel (`0x06`)
- write user-presence config on formal channel (`0x07` / `0x08`)
- clear formal user-presence session override (`0x09`)
- check manager/firmware updates from GitHub Releases
- enroll separately into manager/firmware `stable` or `preview` tracks
- use WinUI-aligned section navigation (`Overview / Devices / Credentials / Security / Maintenance / About`)
- `CTAPHID_INIT`
- `authenticatorGetInfo`
- `authenticatorMakeCredential`
- `authenticatorGetAssertion`
- fetch diagnostics
- list credential summaries
- clear diagnostics
- clear credential storage
- raw HID / CTAP logging
- session-local credential cache

### Run

```powershell
cd .\native-rs\meowkey-manager
cargo run
```

Windows now ships a separate WinUI 3 shell in `windows/gui/MeowKey.Manager/`.

### Important Boundary

This manager now spans two transport classes:

- the formal management bulk interface for inventory/posture reads and manager-safe writes
- Debug HID for CTAP bring-up, diagnostics, and destructive maintenance flows

If the firmware is built without Debug HID:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -DisableDebugHid
```

the manager can still execute formal-channel reads and manager-safe writes, but Debug-HID-only workflows such as CTAP bring-up, diagnostics, and destructive maintenance actions will be unavailable. That is expected behavior.

### Scope

The application is still a hybrid manager and maintenance workbench. It now covers formal-channel reads and manager-safe writes, but CTAP exercise paths and destructive maintenance actions still lean on Debug HID. It is not a complete end-user manager yet.

Still missing:

- standard in-device credential management
- multi-device selection and persistence
- permission-scoped management channels
- end-user recovery and support flows

See also:

- [../../docs/debug-interface.md](../../docs/debug-interface.md)
- [../../docs/security.md](../../docs/security.md)

## 中文

MeowKey Manager 是当前固件在 Linux 侧的原生管理器壳层与维护工作台，技术栈为 `Rust + egui/eframe`。

### 当前能力

- 连接正式管理 bulk 接口
- 连接真实 Debug HID 设备
- 回退到预览后端
- 读取正式凭据摘要（`0x03`）
- 读取正式安全状态（`0x04`）
- 请求短时正式授权（`0x05`）
- 在正式通道按槽位删除单条凭据（`0x06`）
- 在正式通道写入 user presence 配置（`0x07` / `0x08`）
- 清除正式通道 user presence 会话覆盖（`0x09`）
- `CTAPHID_INIT`
- `authenticatorGetInfo`
- `authenticatorMakeCredential`
- `authenticatorGetAssertion`
- 拉取诊断日志
- 列出凭据摘要
- 清空诊断日志
- 清空凭据存储
- 原始 HID / CTAP 日志
- 会话内凭据缓存

### 运行方式

```powershell
cd .\native-rs\meowkey-manager
cargo run
```

Windows 端现在已经独立到 `windows/gui/MeowKey.Manager/` 下的 WinUI 3 壳层。

### 重要边界

这个管理器现在横跨两类传输：

- 正式管理 bulk 接口，用于 inventory / posture 读路径与管理安全写路径
- Debug HID，用于 CTAP bring-up、诊断和破坏性维护动作

如果固件关闭了 Debug HID：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -DisableDebugHid
```

那么管理器仍然可以执行正式通道读写，但依赖 Debug HID 的 CTAP 联调、诊断和破坏性维护动作会不可用，这属于预期行为。

### 当前定位

这个应用目前仍然是“正式管理读写路径 + Debug HID 维护路径”的混合工作台，还不是完整的普通终端用户管理器。

当前仍然缺少：

- 标准化的设备内凭据管理
- 多设备选择与持久化
- 带权限范围的管理通道
- 面向普通用户的恢复与支持流程

补充阅读：

- [../../docs/debug-interface.md](../../docs/debug-interface.md)
- [../../docs/security.md](../../docs/security.md)
