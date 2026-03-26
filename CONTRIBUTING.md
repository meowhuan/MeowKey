# 贡献约定

## 1. 提交前至少做什么

Windows 本地建议直接执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

它会检查：

- `board-presets.json` 可解析
- `gui_server.py` 语法正确
- `probe-board.ps1` 样例输入可跑通
- 默认固件可离线构建
- 关闭 Debug HID 的固件可离线构建
- `probe` 固件可离线构建
- Rust 管理器能通过 `cargo check`
- WPF 原型能通过 `dotnet build`

## 2. 修改协议或行为时

如果你改了下面任一项，必须同步更新文档：

- CTAPHID / CTAP2 命令覆盖范围
- `getInfo` 返回内容
- 调试接口能力
- 发布物命名或变体
- 安全边界

至少要检查：

- `README.md`
- `docs/architecture.md`
- `docs/known-gaps.md`
- `docs/security.md`

## 3. 关于 Debug HID

默认开发构建保留 Debug HID，便于调试；但：

- 不要把 Debug 构建当成生产固件
- 不要在 release 里混淆 debug 与 hardened 变体

如果你的改动新增了调试能力，必须同时写明：

- 谁可以访问
- 如何关闭
- 对 release 的影响

## 4. 关于安全改动

安全改动需要明确写出三件事：

1. 修的是哪类风险。
2. 没修掉的边界是什么。
3. 是否影响协议兼容性、桌面工具或存储格式。

## 5. 关于发行

正式发布使用 GitHub tag：

- `v0.1.0`
- `v0.2.0`

release workflow 会同时打包：

- `debug`
- `hardened`
- `probe`

请不要只发布 Debug 构建；如果这次 release 影响板级识别，也要同步检查 `probe` 产物。
