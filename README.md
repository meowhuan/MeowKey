# MeowKey

## English

MeowKey is an RP2350 passkey / CTAP2 firmware project for personal devices, hobby boards, and open-source redistribution. The repository focuses on understandable protocol behavior, board bring-up, practical hardening, and release packaging that other builders can reuse.

The current tree already includes:

- Standard FIDO HID
- Optional Debug HID
- `CTAPHID_INIT`, `CTAPHID_PING`, `CTAPHID_CBOR`
- `authenticatorGetInfo`
- `authenticatorMakeCredential`
- `authenticatorGetAssertion`
- `authenticatorGetNextAssertion`
- `authenticatorClientPIN`
- `hmac-secret`
- Flash-backed credential storage
- Sign-count journal persistence
- Board ID probing
- Browser WebHID debug UI
- WinUI 3 Windows manager shell
- Rust / egui Linux manager shell

### Project Focus

MeowKey is organized for:

- personal hardware tokens
- self-built or community-shared RP2350 boards
- open-source firmware redistribution with clear build variants
- incremental security hardening instead of factory provisioning assumptions

The project does support optional RP2350 secure boot inputs, OTP hash material generation, and anti-rollback metadata, but those are still opt-in building blocks. They do not turn the MCU into a secure element, and they do not replace a full provisioning or recovery story.

### Quick Start

Windows development build:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build -NoPicotool -IgnoreGitGlobalConfig
```

Windows hardened build without Debug HID:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-hardened -NoPicotool -IgnoreGitGlobalConfig -DisableDebugHid
```

Windows hardened build with manager read-auth gate and simulated secure-element software wrapping:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -BuildDir build-hardened-simse `
  -DisableDebugHid `
  -ManagerSummaryAuth on
```

Windows hardened build with signed boot and anti-rollback metadata:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -BuildDir build-hardened-secure-ready `
  -DisableDebugHid `
  -EnableSignedBoot `
  -EnableAntiRollback
```

Build the standalone probe firmware:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-probe -Probe
```

Flash a UF2:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash.ps1 -Uf2Path .\build\meowkey.uf2
```

Linux flashing:

```bash
./scripts/flash.sh ./build/meowkey.uf2
```

Notes:

- `-NoPicotool` skips `UF2` output and keeps the build offline-friendly.
- `scripts/build.ps1` now enables simulated secure-element wrapping by default; use `-EnableSimulatedSecureElement:$false` if you explicitly need to disable it.
- Keep `-IgnoreGitGlobalConfig` if your machine has a broken global proxy setup.
- Release archives also include `flash.ps1` and `flash.sh`.

### Desktop Apps and Debug Tools

Browser debug UI:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\gui.ps1
```

Then open `http://127.0.0.1:8765` in a Chromium-based browser.

Windows manager shell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-manager.ps1 -Configuration Release
```

Linux-facing Rust manager shell:

```powershell
cd .\native-rs\meowkey-manager
cargo run
```

The browser UI still depends on Debug HID. The Rust shell and the WinUI shell now both support formal-channel reads plus formal write actions (per-credential delete and user-presence writes) behind local-UP-gated short-lived authorization. CTAP bring-up and diagnostics still depend on Debug HID.

### Probe and Presets

If you have an unknown RP2350 baseboard, start with the generic firmware. If you need board identification data or a new preset draft, flash the probe image and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1
```

The probe path is designed for:

- collecting GPIO board-ID clues
- collecting I2C EEPROM / ID clues
- finding candidate user-presence buttons
- producing a `board-presets.json` draft for community reuse

Current preset naming keeps `usb-a-baseboard-v1` as the recommended known board label. The older `custom-baseboard-v1` name remains as a compatibility alias.

Reference listing for the currently adapted USB-A RP2350 baseboard:

- `https://e.tb.cn/h.i7f6mxcYOn7HoWb?tk=JVti5bdpeWA`

Treat the preset name as the real long-term identifier. The shopping link is only a convenience pointer and may expire.

### Build Variants

- `generic-debug`
  Developer-friendly firmware with Debug HID enabled.
- `generic-hardened`
  Smaller attack surface for personal or community redistribution.
- `generic-hardened-secure-boot-ready`
  Hardened firmware plus signed-boot and anti-rollback metadata for users who want to continue with their own OTP workflow.
- `preset-...`
  Preconfigured release packages for known boards.
- `probe-board-id`
  Standalone serial probe firmware for board discovery and preset drafting.

### Documentation

- [docs/building.md](docs/building.md): build flags, outputs, flashing, and probe usage
- [docs/architecture.md](docs/architecture.md): firmware, desktop tools, and storage layout
- [docs/debug-interface.md](docs/debug-interface.md): Debug HID behavior and risk boundaries
- [docs/known-gaps.md](docs/known-gaps.md): current protocol and security gaps
- [docs/release.md](docs/release.md): CI, packaging, and release workflow
- [docs/security.md](docs/security.md): practical security boundaries and deployment guidance
- [docs/security-model.md](docs/security-model.md): code-level security behavior
- [CONTRIBUTING.md](CONTRIBUTING.md): contribution workflow
- [SECURITY.md](SECURITY.md): reporting policy
- [keys/README.md](keys/README.md): local secure-boot key directory
- [native-rs/meowkey-manager/README.md](native-rs/meowkey-manager/README.md): Rust Linux manager shell and maintenance workbench

### Repository Layout

- `src/`: firmware implementation
- `boards/`: board definitions
- `scripts/`: build, flash, GUI, probe, and local checks
- `docs/`: architecture, build, release, and security docs
- `gui/`: browser WebHID debug UI
- `windows/gui/MeowKey.Manager/`: WinUI 3 Windows manager shell
- `native-rs/meowkey-manager/`: Rust / egui Linux manager shell
- `third_party/pico-sdk/`: bundled Pico SDK

### Current Practical Facts

- Debug builds expose Debug HID and can read logs, inspect state, and modify user-presence behavior.
- Hardened builds disable Debug HID and are the better default for redistribution.
- The Windows desktop manager now lives in `windows/gui/MeowKey.Manager/` and is organized around management sections instead of protocol test panels.
- The Linux desktop surface currently stays on Rust + egui/eframe so the existing cross-platform backend can keep moving without a second native toolkit migration.
- The Linux manager now mirrors the same section navigation rhythm (`Overview / Devices / Credentials / Security / Maintenance / About`) for cross-surface consistency.
- Both manager shells now include release-update checks against GitHub Releases with separate manager/firmware stable-preview enrollment tracks.
- Hardened builds now default to requiring short-lived manager authorization for credential-summary reads; debug builds keep read-path friction lower for bring-up.
- The optional simulated secure-element mode adds software-only secret wrapping semantics for credential secrets; it does **not** create a hardware secure-element boundary.
- `clientPIN` currently implements the older `getPinToken` flow, not permission-scoped tokens.
- Runtime `pinUvAuthToken` is session-only and short-lived.
- The default `meowkey_rp2350_usb` user-presence source is `BOOTSEL` with a double-tap gesture.
- User-presence configuration persists in flash, so reflashing alone does not erase prior settings.
- Sign-count persistence is journal-based now, which is better than whole-store rewrites but not the end state.

Read [docs/security.md](docs/security.md) and [docs/release.md](docs/release.md) before publishing firmware archives for other users.

## 中文

MeowKey 是一个面向 `RP2350` 的 passkey / CTAP2 固件项目，文档和发布方式现在以个人设备、自制底板、社区复用和开源分发为中心来组织。这个仓库的目标不是把“工厂侧流程”包装成既成事实，而是把协议语义、板级适配、调试工具、构建变体和真实安全边界讲清楚，让使用者可以基于同一套代码继续构建、验证和分发。

当前仓库已经具备的核心能力包括：

- 标准 FIDO HID 接口
- 可选 Debug HID 接口
- `CTAPHID_INIT`、`CTAPHID_PING`、`CTAPHID_CBOR`
- `authenticatorGetInfo`
- `authenticatorMakeCredential`
- `authenticatorGetAssertion`
- `authenticatorGetNextAssertion`
- `authenticatorClientPIN`
- `hmac-secret`
- 基于 Flash 的凭据持久化
- 单独的签名计数 journal
- 板卡 ID 探测
- 浏览器 WebHID 调试界面
- WinUI 3 Windows 管理器壳层
- Rust / egui Linux 管理器壳层

### 项目主旨

MeowKey 当前围绕下面几件事展开：

- 个人自用认证器设备
- 自制或社区共享的 RP2350 底板
- 可复用、可重新打包的开源固件发布物
- 在公开代码前提下逐步收紧安全边界，而不是假设已经具备工厂 provisioning 能力

仓库现在已经支持可选的 RP2350 secure boot、OTP 公钥哈希材料输出和 anti-rollback metadata，但这些仍然只是可选构建块。它们不会自动替你完成 OTP 烧录，也不等于设备已经拥有安全元件级别的密钥边界。

### 快速开始

Windows 默认开发构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build -NoPicotool -IgnoreGitGlobalConfig
```

关闭 Debug HID 的硬化构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-hardened -NoPicotool -IgnoreGitGlobalConfig -DisableDebugHid
```

启用管理读授权门控与模拟安全元件软件封装的硬化构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -BuildDir build-hardened-simse `
  -DisableDebugHid `
  -ManagerSummaryAuth on
```

启用 signed boot 与 anti-rollback metadata 的硬化构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -BuildDir build-hardened-secure-ready `
  -DisableDebugHid `
  -EnableSignedBoot `
  -EnableAntiRollback
```

构建独立 probe 固件：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-probe -Probe
```

刷写 UF2：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash.ps1 -Uf2Path .\build\meowkey.uf2
```

Linux 刷写：

```bash
./scripts/flash.sh ./build/meowkey.uf2
```

补充说明：

- `-NoPicotool` 会跳过 `UF2` 生成，更适合离线开发检查。
- `scripts/build.ps1` 现在默认启用“模拟安全元件”软件封装；若要显式关闭，可传 `-EnableSimulatedSecureElement:$false`。
- 如果你的全局 Git 代理配置有问题，继续保留 `-IgnoreGitGlobalConfig`。
- release 压缩包里同样会附带 `flash.ps1` 与 `flash.sh`。

### 桌面应用与调试工具

浏览器调试台：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\gui.ps1
```

然后用 Chromium 内核浏览器打开 `http://127.0.0.1:8765`。

Windows 管理器壳层：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-manager.ps1 -Configuration Release
```

Linux 侧 Rust 管理器壳层：

```powershell
cd .\native-rs\meowkey-manager
cargo run
```

浏览器调试台仍然依赖 Debug HID。Rust 壳层和 WinUI 壳层现在都支持“正式通道读取 + 正式通道受控写入”（单条凭据删除与 user presence 写入），并且都走本地在场确认 + 短时授权。CTAP bring-up 与诊断仍然依赖 Debug HID。

### Probe 与预设

如果你手上的 RP2350 底板还没有现成 preset，建议先从通用固件开始；如果需要进一步拿到底板识别信息或生成新的 preset 草案，再刷入 probe 固件并执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1
```

这条链路主要用于：

- 收集 GPIO 侧的板卡 ID 线索
- 收集 I2C EEPROM / ID 芯片线索
- 给出候选的 user presence 按键
- 生成可以继续补充进 `board-presets.json` 的草案

当前预设命名里，`usb-a-baseboard-v1` 是推荐的已知底板名称；旧名字 `custom-baseboard-v1` 仍保留为兼容别名。

当前已适配 USB-A RP2350 底板的参考商品链接：

- `https://e.tb.cn/h.i7f6mxcYOn7HoWb?tk=JVti5bdpeWA`

长期应以 preset 名称作为识别依据，商品链接只作为方便对照的入口，本身可能失效。

### 构建变体

- `generic-debug`
  开发联调用固件，保留 Debug HID。
- `generic-hardened`
  更适合个人分发和社区分发的较小攻击面基线。
- `generic-hardened-secure-boot-ready`
  在 `generic-hardened` 基础上增加 signed boot 与 anti-rollback metadata，方便用户继续走自己的 OTP 流程。
- `preset-...`
  已知底板的预设发行包。
- `probe-board-id`
  独立串口探测固件，用来识别未知底板并生成 preset 草案。

### 文档索引

- [docs/building.md](docs/building.md)：构建参数、产物、刷写和 probe 用法
- [docs/architecture.md](docs/architecture.md)：固件、桌面工具与存储结构
- [docs/debug-interface.md](docs/debug-interface.md)：Debug HID 行为和风险边界
- [docs/known-gaps.md](docs/known-gaps.md)：当前协议与安全缺口
- [docs/release.md](docs/release.md)：CI、打包和发布流程
- [docs/security.md](docs/security.md)：实际安全边界与使用建议
- [docs/security-model.md](docs/security-model.md)：代码级安全模型
- [CONTRIBUTING.md](CONTRIBUTING.md)：协作与提交前检查
- [SECURITY.md](SECURITY.md)：安全问题报告方式
- [keys/README.md](keys/README.md)：本地 secure boot 密钥目录
- [native-rs/meowkey-manager/README.md](native-rs/meowkey-manager/README.md)：Rust Linux 管理器壳层与维护工作台说明

### 仓库结构

- `src/`：固件实现
- `boards/`：板卡定义
- `scripts/`：构建、刷写、GUI、probe 和本地检查脚本
- `docs/`：架构、构建、发布、安全文档
- `gui/`：浏览器 WebHID 调试界面
- `windows/gui/MeowKey.Manager/`：WinUI 3 Windows 管理器壳层
- `native-rs/meowkey-manager/`：Rust / egui Linux 管理器壳层
- `third_party/pico-sdk/`：内置 Pico SDK

### 当前最重要的事实

- `debug` 构建会暴露 Debug HID，主机可以读取日志、观察状态并改写 user presence 行为。
- `hardened` 构建会关闭 Debug HID，更适合作为分发基线。
- Windows 桌面管理器现在位于 `windows/gui/MeowKey.Manager/`，信息架构已经按真正管理器而不是协议测试面板来组织。
- Linux 桌面面暂时继续使用 Rust + egui/eframe，这样可以复用现有跨平台后端并避免再引入一套新的原生桌面栈。
- `hardened` 构建现在默认要求短时管理授权后才能读取正式凭据摘要；`debug` 构建仍保留更低摩擦的 bring-up 读取路径。
- 可选的“模拟安全元件”模式会增加软件级别的 secret wrapping 语义，但这**不等于**硬件安全元件边界。
- `clientPIN` 当前只实现旧式 `getPinToken`，没有权限范围 token。
- 运行时 `pinUvAuthToken` 只存在于当前会话，且生命周期较短。
- `meowkey_rp2350_usb` 当前默认的 user presence 来源是双击 `BOOTSEL`。
- UP 配置会持久化到 Flash，所以单纯重刷固件不会自动清掉旧配置。
- `signCount` 现在通过独立 journal 持久化，比整区重写更合理，但还不是最终形态。

如果你准备把固件打包分享给其他人，请先读完 [docs/security.md](docs/security.md) 和 [docs/release.md](docs/release.md)。
