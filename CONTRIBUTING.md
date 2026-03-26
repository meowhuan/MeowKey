# Contributing

## English

### Local Checks

Run this before you send changes:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

The script covers:

- `board-presets.json` parsing
- `gui_server.py` syntax
- `probe-board.ps1` sample input
- offline default firmware build
- offline hardened firmware build
- offline probe firmware build
- `cargo check` for the Rust manager
- `dotnet build` for the WPF prototype

### When Behavior Changes

If you change protocol behavior, release naming, Debug HID scope, build flags, or security boundaries, update the docs in the same change. At minimum review:

- `README.md`
- `docs/architecture.md`
- `docs/known-gaps.md`
- `docs/security.md`

### Debug HID Rules

Debug HID is kept for bring-up and local tooling, but it must stay clearly separated from the safer redistribution path.

If your change adds or expands debug capability, document:

- who can access it
- how to disable it
- whether it changes release packaging or safety expectations

### Security Changes

For any security-related change, explain:

1. what risk you are reducing
2. what boundary still remains
3. whether protocol compatibility, tooling, or storage format changes

### Releases

Version tags follow forms such as:

- `v0.1.0`
- `v0.2.0`
- `v0.2.0-beta.2`

Release workflows package `debug`, `hardened`, and `probe` variants. Do not publish only the debug build, and re-check probe outputs when board-identification behavior changes.

## 中文

### 本地检查

提交改动前，建议至少执行一次：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

这个脚本当前会检查：

- `board-presets.json` 是否可解析
- `gui_server.py` 语法是否正确
- `probe-board.ps1` 的样例输入是否可跑通
- 默认固件是否能离线构建
- 关闭 Debug HID 的硬化固件是否能离线构建
- `probe` 固件是否能离线构建
- Rust 管理器是否能通过 `cargo check`
- WPF 原型是否能通过 `dotnet build`

### 行为变化时必须同步文档

如果你的改动影响了协议行为、发布命名、Debug HID 能力范围、构建参数或安全边界，应该在同一个改动里同步更新文档。至少需要检查：

- `README.md`
- `docs/architecture.md`
- `docs/known-gaps.md`
- `docs/security.md`

### 关于 Debug HID

Debug HID 仍然保留给 bring-up 和本地调试工具使用，但它必须和更安全的分发路径明确区分开。

如果你的改动增加了新的调试能力，必须同时说明：

- 谁能访问这项能力
- 如何关闭它
- 它是否影响 release 打包方式或安全预期

### 关于安全改动

任何安全相关改动都应写清楚三件事：

1. 你在降低什么风险
2. 还有哪些边界没有解决
3. 是否影响协议兼容性、桌面工具或存储格式

### 关于发布

版本标签目前沿用这类形式：

- `v0.1.0`
- `v0.2.0`
- `v0.2.0-beta.2`

release 工作流会同时打包 `debug`、`hardened` 和 `probe` 变体。不要只发布 debug 构建；如果本次改动影响了板级识别逻辑，也要同步检查 probe 产物是否仍然合理。
