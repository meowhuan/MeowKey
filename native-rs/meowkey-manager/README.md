# MeowKey Manager

## English

MeowKey Manager is the current Linux-facing native manager shell and maintenance workbench for the firmware, built with `Rust + egui/eframe`.

### Current Features

- connect to a real Debug HID device
- fall back to a preview backend
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

This manager depends on firmware Debug HID.

If the firmware is built without Debug HID:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -DisableDebugHid
```

the manager cannot connect. That is expected behavior.

### Scope

The application still leans on Debug HID and remains a maintenance-oriented surface for personal labs and open-source firmware bring-up. It is not a complete end-user manager yet.

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

- 连接真实 Debug HID 设备
- 回退到预览后端
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

这个管理器依赖固件暴露的 Debug HID。

如果固件关闭了 Debug HID：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -DisableDebugHid
```

那么管理器就无法连接设备，这属于预期行为。

### 当前定位

这个应用目前仍然偏向依赖 Debug HID 的维护 / bring-up 工作台，还不是完整的普通终端用户管理器。

当前仍然缺少：

- 标准化的设备内凭据管理
- 多设备选择与持久化
- 带权限范围的管理通道
- 面向普通用户的恢复与支持流程

补充阅读：

- [../../docs/debug-interface.md](../../docs/debug-interface.md)
- [../../docs/security.md](../../docs/security.md)
