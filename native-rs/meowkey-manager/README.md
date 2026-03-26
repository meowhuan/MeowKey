# MeowKey Manager

MeowKey 的跨平台原生调试管理器，当前基于 `Rust + egui/eframe`。

## 当前能力

- 连接真实 Debug HID
- 回退到预览后端
- `CTAPHID_INIT`
- `authenticatorGetInfo`
- `authenticatorMakeCredential`
- `authenticatorGetAssertion`
- 拉取诊断日志
- 列出固件凭据摘要
- 清空诊断日志
- 清空固件凭据存储
- 已预留 UP 配置读写 transport，后续 UI 可直接接 `DIAG 5/6/7/8`
- 原始 HID / CTAP 日志
- 会话内凭据缓存

## 运行

```powershell
cd .\native-rs\meowkey-manager
cargo run
```

## 重要前提

这个管理器依赖固件暴露的 Debug HID。

如果固件使用了硬化构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -DisableDebugHid
```

那么原生管理器将无法连接设备。这是设计使然，不是故障。

## 当前定位

它现在是“带真实传输的开发调试管理器”，不是量产级终端用户管理器。

还没做到的事情包括：

- 标准化的设备内凭据管理
- 多设备选择与持久化
- 受权限保护的生产管理通道
- 面向最终用户的 UI / 恢复流程

调试层边界请配合阅读：

- [../../docs/debug-interface.md](../../docs/debug-interface.md)
- [../../docs/security.md](../../docs/security.md)
