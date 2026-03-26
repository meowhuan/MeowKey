# Building

## English

### 1. Windows Builds

The repository ships with `scripts/build.ps1`, which uses the local `tools/` cross toolchain by default.

Default development build:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build -NoPicotool -IgnoreGitGlobalConfig
```

Hardened build without Debug HID:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-hardened -NoPicotool -IgnoreGitGlobalConfig -DisableDebugHid
```

Hardened build with signed boot and anti-rollback metadata:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -BuildDir build-hardened-secure-ready `
  -DisableDebugHid `
  -EnableSignedBoot `
  -EnableAntiRollback
```

Probe firmware:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-probe -NoPicotool -IgnoreGitGlobalConfig -Probe
```

Use a board preset:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset usb-a-baseboard-v1 -BuildDir build
```

`custom-baseboard-v1` is still accepted as a compatibility alias.

Reference listing for the currently adapted USB-A RP2350 baseboard:

- `https://e.tb.cn/h.i7f6mxcYOn7HoWb?tk=JVti5bdpeWA`

Use the preset name as the stable identifier. The shopping link is only a convenience pointer.

### 2. Important `build.ps1` Flags

- `-BuildDir`: output directory
- `-Preset`: load board settings from `scripts/board-presets.json`
- `-FlashSizeMB`: override flash size
- `-CredentialCapacity`: explicit credential cap; `0` means auto-size from the reserved store region
- `-CredentialStoreKB`: credential store size in `4 KB` units; minimum is currently `16 KB`
- `-DisableDebugHid`: build without Debug HID
- `-UserPresenceSource`: `none | bootsel | gpio`
- `-UserPresenceGpioPin`: GPIO pin when `-UserPresenceSource gpio`
- `-UserPresenceGpioActiveState`: `low | high`
- `-UserPresenceTapCount`: required tap count
- `-UserPresenceGestureWindowMs`: tap window
- `-UserPresenceTimeoutMs`: confirmation timeout
- `-Probe`: build the standalone `meowkey_probe` image
- `-BoardIdMode`: `none | gpio | i2c-eeprom`
- `-BoardIdGpioPins`: GPIO list for board-ID sampling
- `-BoardIdI2cPreset`: `24c02 | 24c32 | 24c64 | custom`
- `-NoPicotool`: skip `picotool`, so no `UF2` is emitted
- `-IgnoreGitGlobalConfig`: ignore the machine-level Git config during configure
- `-VersionMajor`, `-VersionMinor`, `-VersionPatch`, `-VersionLabel`: version string pieces
- `-EnableSignedBoot`: enable RP2350 signed image flow and OTP hash material output
- `-EnableAntiRollback`: add rollback metadata; requires `-EnableSignedBoot`
- `-SecureBootSigningKey`: PEM signing key path, defaulting to `keys/meowkey-secureboot.pem`
- `-SecureBootOtpOutputPath`: custom output path for the generated OTP JSON
- `-AntiRollbackRows`: override the OTP rows used by the rollback thermometer counter

### 3. Build Outputs

Normal firmware outputs:

- `meowkey.elf`
- `meowkey.bin`
- `meowkey.hex`
- `meowkey.dis`
- `meowkey.elf.map`

`meowkey.uf2` appears only when `picotool` is enabled.

Probe outputs:

- `meowkey_probe.elf`
- `meowkey_probe.bin`
- `meowkey_probe.hex`
- `meowkey_probe.elf.map`
- `meowkey_probe.uf2` when `picotool` is enabled

### 4. Direct CMake Builds

CI and non-Windows environments are usually better served by direct CMake:

```bash
cmake -S . -B build-ci -G Ninja \
  -DPICO_BOARD=meowkey_rp2350_usb \
  -DPICO_NO_PICOTOOL=ON
cmake --build build-ci
```

Hardened CMake build:

```bash
cmake -S . -B build-ci-hardened -G Ninja \
  -DPICO_BOARD=meowkey_rp2350_usb \
  -DPICO_NO_PICOTOOL=ON \
  -DMEOWKEY_ENABLE_DEBUG_HID=OFF
cmake --build build-ci-hardened
```

Do not use `-DPICO_NO_PICOTOOL=ON` when you need `UF2`, signed boot, or anti-rollback sealing.

### 5. Flashing

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash.ps1 -Uf2Path .\build\meowkey.uf2
```

Linux:

```bash
./scripts/flash.sh ./build/meowkey.uf2
```

Both scripts can also work without an explicit path when there is exactly one `*.uf2` next to the script.

### 6. Desktop Apps and Debug Tools

Browser UI:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\gui.ps1
```

Windows manager shell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-manager.ps1 -Configuration Release
```

Linux-facing Rust manager shell:

```powershell
cd .\native-rs\meowkey-manager
cargo run
```

Important notes:

- the browser UI and the Rust shell depend on Debug HID
- the WinUI shell is the main Windows manager surface and now lives in `windows/gui/MeowKey.Manager/`
- the Linux desktop surface currently stays on Rust + egui/eframe
- a hardened build cannot be used with the browser UI or the Rust maintenance shell
- the probe image does not use Debug HID; it reports through USB serial
- `DIAG 6` persists user-presence baseline settings
- `DIAG 7` only changes the current power session

### 7. Probe and Preset Drafting

After flashing the probe image, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1
```

Useful variants:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -Port COM7
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -InputPath .\probe-report.json
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -TextOutputPath .\probe-report.txt
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -OutputPath .\probe-bundle.json
```

The script emits:

- a preset snippet for `board-presets.json`
- notes about why that draft was chosen
- the raw probe report

The report can also include candidate UP buttons, candidate I2C UV peripherals, and secure-element identification hints.

### 8. Signed-Boot Notes

- signed boot is opt-in
- OTP programming is still a deliberate user step
- this improves firmware trust boundaries, not secret-extraction guarantees

### 9. FAQ

Why is there no `UF2`?

- you used `-NoPicotool`, or `picotool` was unavailable

Why does CMake stall while fetching `picotool`?

- machine-level Git proxy settings are a common cause; try `-IgnoreGitGlobalConfig`

Why does CI not rely on `tools/`?

- `tools/` is ignored by Git, so CI installs toolchain pieces explicitly instead of assuming that directory exists

## 中文

### 1. Windows 本地构建

仓库自带 `scripts/build.ps1`，默认会优先使用项目根目录下的 `tools/` 交叉工具链。

默认开发构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build -NoPicotool -IgnoreGitGlobalConfig
```

关闭 Debug HID 的硬化构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-hardened -NoPicotool -IgnoreGitGlobalConfig -DisableDebugHid
```

启用 signed boot 和 anti-rollback metadata 的硬化构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -BuildDir build-hardened-secure-ready `
  -DisableDebugHid `
  -EnableSignedBoot `
  -EnableAntiRollback
```

构建独立 probe 固件：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-probe -NoPicotool -IgnoreGitGlobalConfig -Probe
```

使用板级 preset：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset usb-a-baseboard-v1 -BuildDir build
```

旧名称 `custom-baseboard-v1` 仍可用，但现在只作为兼容别名保留。

当前已适配 USB-A RP2350 底板的参考商品链接：

- `https://e.tb.cn/h.i7f6mxcYOn7HoWb?tk=JVti5bdpeWA`

长期建议仍以 preset 名称作为稳定标识，商品链接只用于快速对照。

### 2. `build.ps1` 关键参数

- `-BuildDir`：输出目录
- `-Preset`：从 `scripts/board-presets.json` 读取板级参数
- `-FlashSizeMB`：覆盖 Flash 容量
- `-CredentialCapacity`：显式限制凭据数量；`0` 表示按存储区域自动计算
- `-CredentialStoreKB`：凭据存储大小，必须按 `4 KB` 对齐，当前最小为 `16 KB`
- `-DisableDebugHid`：关闭 Debug HID
- `-UserPresenceSource`：`none | bootsel | gpio`
- `-UserPresenceGpioPin`：当使用 GPIO 作为 UP 源时指定引脚
- `-UserPresenceGpioActiveState`：`low | high`
- `-UserPresenceTapCount`：需要的点击次数
- `-UserPresenceGestureWindowMs`：多击判定窗口
- `-UserPresenceTimeoutMs`：一次确认等待超时
- `-Probe`：构建单独的 `meowkey_probe`
- `-BoardIdMode`：`none | gpio | i2c-eeprom`
- `-BoardIdGpioPins`：GPIO 采样用的引脚列表
- `-BoardIdI2cPreset`：`24c02 | 24c32 | 24c64 | custom`
- `-NoPicotool`：跳过 `picotool`，因此不会生成 `UF2`
- `-IgnoreGitGlobalConfig`：配置阶段临时忽略本机 Git 全局配置
- `-VersionMajor`、`-VersionMinor`、`-VersionPatch`、`-VersionLabel`：固件版本字符串片段
- `-EnableSignedBoot`：启用 RP2350 signed image 构建链路并输出 OTP 哈希材料
- `-EnableAntiRollback`：启用 anti-rollback metadata，要求同时启用 `-EnableSignedBoot`
- `-SecureBootSigningKey`：签名私钥路径，默认是 `keys/meowkey-secureboot.pem`
- `-SecureBootOtpOutputPath`：自定义 OTP JSON 输出路径
- `-AntiRollbackRows`：覆盖 rollback 计数使用的 OTP 行

### 3. 构建产物

普通固件输出：

- `meowkey.elf`
- `meowkey.bin`
- `meowkey.hex`
- `meowkey.dis`
- `meowkey.elf.map`

只有在启用了 `picotool` 时，才会额外生成：

- `meowkey.uf2`

probe 固件输出：

- `meowkey_probe.elf`
- `meowkey_probe.bin`
- `meowkey_probe.hex`
- `meowkey_probe.elf.map`
- `meowkey_probe.uf2`（启用 `picotool` 时）

### 4. 直接使用 CMake

CI 或 Linux / macOS 环境通常更适合直接走 CMake：

```bash
cmake -S . -B build-ci -G Ninja \
  -DPICO_BOARD=meowkey_rp2350_usb \
  -DPICO_NO_PICOTOOL=ON
cmake --build build-ci
```

硬化 CMake 构建：

```bash
cmake -S . -B build-ci-hardened -G Ninja \
  -DPICO_BOARD=meowkey_rp2350_usb \
  -DPICO_NO_PICOTOOL=ON \
  -DMEOWKEY_ENABLE_DEBUG_HID=OFF
cmake --build build-ci-hardened
```

如果你需要 `UF2`、signed boot 或 anti-rollback seal，不要传 `-DPICO_NO_PICOTOOL=ON`。

### 5. 刷写

Windows：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash.ps1 -Uf2Path .\build\meowkey.uf2
```

Linux：

```bash
./scripts/flash.sh ./build/meowkey.uf2
```

如果脚本旁边只有一个 `*.uf2` 文件，两个脚本也都支持省略路径参数。

### 6. 桌面应用与调试工具

浏览器调试台：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\gui.ps1
```

Windows 管理器壳层：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-manager.ps1 -Configuration Release
```

Linux 侧 Rust 管理器壳层：

```powershell
cd .\native-rs\meowkey-manager
cargo run
```

需要注意：

- 浏览器调试台和 Rust 壳层都依赖 Debug HID
- WinUI 壳层是新的 Windows 主管理界面，项目路径为 `windows/gui/MeowKey.Manager/`
- Linux 桌面面当前继续使用 Rust + egui/eframe
- 浏览器调试台和 Rust 维护壳层都不能直接配合硬化构建使用
- probe 固件不走 Debug HID，而是通过 USB 串口输出报告
- `DIAG 6` 会持久化写入 user presence baseline
- `DIAG 7` 只影响当前上电会话

### 7. Probe 与 preset 草案

刷入 probe 固件后，执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1
```

常用变体：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -Port COM7
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -InputPath .\probe-report.json
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -TextOutputPath .\probe-report.txt
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -OutputPath .\probe-bundle.json
```

脚本会输出：

- 一个可复制进 `board-presets.json` 的 preset 片段
- 选择这份草案的说明
- 原始 probe 报告

新版报告还可能包含候选的 UP 按键、候选 I2C UV 外设，以及常见安全元件的只读识别提示。

### 8. Signed Boot 说明

- signed boot 默认是关闭的
- OTP 烧录仍然需要用户自己确认后执行
- 这条链路提升的是“可信固件边界”，不是“不可导出密钥边界”

### 9. 常见问题

为什么没有 `UF2`？

- 你传了 `-NoPicotool`，或者当前环境里没有可用的 `picotool`

为什么 CMake 会卡在拉取 `picotool`？

- 常见原因是本机 Git 全局代理配置有问题，可以尝试 `-IgnoreGitGlobalConfig`

为什么 CI 不依赖仓库里的 `tools/`？

- 因为 `tools/` 默认被 Git 忽略，CI 会显式安装工具链，而不是假定这个目录一定存在
