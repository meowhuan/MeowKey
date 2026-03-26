# MeowKey

MeowKey 是一个面向 `RP2350` 的自研 passkey / CTAP2 固件实验仓库。当前目标不是直接做“量产认证器”，而是先把 USB 传输、CTAP2 语义、凭据存储、桌面调试工具和后续安全策略拆开，逐层验证清楚。

当前仓库已经不是最小脚手架，而是一套可以联调的开发型认证器：

- 标准 FIDO HID 接口
- 可选的 Debug HID 接口
- `CTAPHID_INIT`
- `CTAPHID_PING`
- `CTAPHID_CBOR`
- `authenticatorGetInfo`
- `authenticatorMakeCredential`
- `authenticatorGetAssertion`
- `authenticatorClientPIN`
- `hmac-secret` 扩展
- Flash 持久化凭据存储
- 基础签名计数
- 板卡 ID 诊断
- 浏览器 WebHID 调试台
- Rust 原生调试管理器

## 当前定位

MeowKey 现在是“可构建、可联调、可验证协议行为”的开发固件，不是量产级安全产品。

量产前仍然缺少的关键能力包括：

- 量产级用户在场（UP）硬件路径
- 用户验证（UV）硬件路径
- 板载安全元件或真正不可导出的密钥边界
- 原子化 / 磨损均衡的存储层
- 完整的凭据管理命令
- 多断言 / 凭据枚举流程
- 更严格的 CTAP2 兼容性验证

这些缺口已经整理在 [docs/known-gaps.md](docs/known-gaps.md) 和 [docs/security.md](docs/security.md)。

当前仓库已经支持可选的 RP2350 secure boot、OTP 公钥哈希材料生成和 anti-rollback 镜像元数据，但默认不会替用户自动烧录 OTP。
如果你信任本项目并希望提升设备内数据对恶意固件的防护，应主动启用 secure boot。
这仍然不等于“RP2350 在模拟安全元件”；当前设备唯一材料 wrapping 只是比明文落盘更强的 MCU 侧保护，不是不可导出私钥边界。

## 底板与预设

如果你购买的是仓库当前已经适配的 RP2350 自定义底板配件，请直接使用现有预设：

- 推荐预设名：`usb-a-baseboard-v1`
- 兼容旧别名：`custom-baseboard-v1`
- 参考商品链接：`https://e.tb.cn/h.i7f6mxcYOn7HoWb?tk=JVti5bdpeWA`

说明：

- 这里保留商品链接只是为了帮助你快速对照板型，链接本身可能失效。
- 更重要的是“是否为仓库已适配的这块 RP2350 自定义底板”，而不是链接是否长期可访问。

如果你手上的不是这块底板，按下面顺序处理：

1. 先尝试通用固件，看基础 USB/CTAP 是否能正常工作。
2. 如果还需要板级识别，再刷 `probe` 固件并运行探测脚本。
3. 如果探测结果仍不足以唯一确定预设，请提供 GPIO 编码电阻/拔码信息，或 I2C EEPROM/ID 芯片信息，再补新的 preset。

## 快速开始

### Windows 本地构建

默认开发构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build -NoPicotool -IgnoreGitGlobalConfig
```

关闭 Debug HID 的硬化构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-hardened -NoPicotool -IgnoreGitGlobalConfig -DisableDebugHid
```

启用 secure boot 与 anti-rollback 的硬化构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -BuildDir build-hardened-secure-ready `
  -DisableDebugHid `
  -EnableSignedBoot `
  -EnableAntiRollback
```

指定版本号：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -BuildDir build `
  -VersionMajor 0 -VersionMinor 2 -VersionPatch 0 -VersionLabel rc1
```

构建单独的板卡探测固件：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-probe -Probe
```

刷写 UF2：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash.ps1 -Uf2Path .\build\meowkey.uf2
```

Linux：

```bash
./scripts/flash.sh ./build/meowkey.uf2
```

说明：

- `-NoPicotool` 会跳过 `UF2` 生成，只产出 `elf/bin/hex/map/dis`。
- 要生成 `UF2`，需要允许 CMake 拉取或使用已安装的 `picotool`。
- 如果本机有错误的 Git 全局代理，继续保留 `-IgnoreGitGlobalConfig`。
- 发布包内自带的 `flash.ps1` / `flash.sh` 也支持“脚本同目录仅有一个 UF2 文件”时直接无参刷写。

### 浏览器调试台

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\gui.ps1
```

然后在 Chromium 内核浏览器中打开 `http://127.0.0.1:8765`。

### Rust 原生管理器

```powershell
cd .\native-rs\meowkey-manager
cargo run
```

Rust 管理器依赖 Debug HID。若固件使用 `-DisableDebugHid` 构建，它将无法连接设备。

### Probe 固件与预设草案

刷入 `probe` 固件后，可以用脚本抓取板卡识别信息并生成 preset 草案：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1
```

新版 probe 报告除了板卡 ID 候选，也会额外给出：

- 可手工复核的 GPIO `UP` 按键候选
- 可手工复核的 `UV` 外设候选（当前主要是启发式 I2C 候选，不会自动启用）
- 可手工复核的 `secureElement` 候选与已支持器件的只读识别摘要

如果自动探测不到正确串口，可以手工指定：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -Port COM7
```

如果希望把终端里这份可读输出保存成文本文件：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -TextOutputPath .\probe-report.txt
```

如果还想同时保存完整 JSON bundle，继续使用：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -OutputPath .\probe-bundle.json
```

脚本会输出：

- 建议的 `board-presets.json` 片段
- 备注说明
- 原始 probe 报告

这条链路优先用于“未知底板 -> 收集板卡 ID 信息 -> 生成新 preset 草案”。

## 发行版变体

仓库现在区分三类固件用途：

- `generic-debug`
  保留 Debug HID，便于 WebUI / Rust 管理器联调；不适合直接分发给终端用户。
- `generic-hardened`
  关闭 Debug HID，仅保留标准 FIDO HID；这是更接近可发布状态的构建基线。
- `generic-hardened-secure-boot-ready`
  在 `generic-hardened` 基础上启用签名镜像与 anti-rollback 元数据，并附带 OTP 公钥哈希材料；是否真正烧录 OTP 仍由用户决定。
- `preset-usb-a-baseboard-v1-debug` / `preset-usb-a-baseboard-v1-hardened`
  面向当前这块已适配 USB-A 底板配件的预设固件，包名会直接带上预设用途。
- `preset-...-hardened-secure-boot-ready`
  对应预设板卡的 secure-boot-ready 硬化发行包。
- `probe-board-id`
  独立 USB 串口探测固件，用于读取 GPIO 编码电阻/拔码和 I2C EEPROM/ID 候选信息，并生成 preset 草案。

GitHub Actions 的 release 工作流会同时产出 generic、preset 和 probe 包；每个 zip 内都会带 Windows / Linux 刷写脚本。

## 文档索引

- [docs/building.md](docs/building.md)
  本地构建、参数、产物与刷写方式。
- [docs/architecture.md](docs/architecture.md)
  固件、桌面工具和存储布局的真实结构。
- [docs/debug-interface.md](docs/debug-interface.md)
  双 HID 接口、调试命令和边界说明。
- [docs/known-gaps.md](docs/known-gaps.md)
  现有固件逻辑遗漏与后续优先级。
- [docs/release.md](docs/release.md)
  CI、tag 发行、产物组织和发布约定。
- [docs/security.md](docs/security.md)
  当前安全边界、已修补项和剩余风险。
- [docs/security-model.md](docs/security-model.md)
  代码级别的当前安全模型与真实行为。
- [CONTRIBUTING.md](CONTRIBUTING.md)
  本地提交前检查和协作约定。
- [SECURITY.md](SECURITY.md)
  安全披露入口和受支持构建说明。
- [keys/README.md](keys/README.md)
  安全启动签名密钥目录说明。
- [native-rs/meowkey-manager/README.md](native-rs/meowkey-manager/README.md)
  Rust 原生管理器说明。

## 仓库结构

- `src/`
  固件核心实现。
- `boards/`
  本地板卡定义。
- `scripts/`
  构建、刷写、GUI 启动与本地检查脚本。
- `docs/`
  架构、发布、安全和缺口说明。
- `gui/`
  浏览器 WebHID 调试台。
- `native-rs/meowkey-manager/`
  Rust / egui 原生调试管理器。
- `native/`
  早期 WPF 原型。
- `third_party/pico-sdk/`
  内置 Pico SDK。

## 当前最重要的事实

- 默认开发构建会暴露 Debug HID，可读取诊断、列出凭据并清空凭据存储。
- 硬化构建已经支持关闭 Debug HID。
- 现在已经有单独的 `probe` 固件与 `scripts/probe-board.ps1`，用于生成 preset 草案。
- `clientPIN` 现在只保留不带权限范围的旧式 `getPinToken` 路径；`getPinTokenWithPermissions` 仍未实现。
- 运行时 `pinUvAuthToken` 不会持久化到 Flash，并且现在是短生命周期令牌。
- 当前 `meowkey_rp2350_usb` 板卡的默认 UP 源已经切到 `BOOTSEL`，现状是双击 `BOOTSEL` 可满足运行时 UP。
- UP 配置会持久化到 Flash；重新刷固件不会自动抹掉既有配置，所以“新编译怎么还是这个行为”通常是设备沿用了已保存的 UP 配置。
- 如果后续底板接入了专用按键，建议把 UP 源切到独立 GPIO；如果需要临时关闭物理确认，也可以把 UP 源手动切回 `none`，相关固件配置接口已经预留，后续 UI 可直接对接。
- `signCount` 已经从主凭据区整区重写路径中拆出，改为单独 journal 持久化；但这还不是最终的掉电安全存储方案。

如果你要做真正的发行，请先看完 [docs/security.md](docs/security.md) 和 [docs/release.md](docs/release.md)。
