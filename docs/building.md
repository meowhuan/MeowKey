# 构建说明

## 1. 本地 Windows 构建

仓库自带一套 Windows 侧 `build.ps1`，默认使用根目录下的 `tools/` 交叉工具链。

基础构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build -NoPicotool -IgnoreGitGlobalConfig
```

关闭 Debug HID：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-hardened -NoPicotool -IgnoreGitGlobalConfig -DisableDebugHid
```

构建 `probe` 固件：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-probe -NoPicotool -IgnoreGitGlobalConfig -Probe
```

使用板级预设：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset usb-a-baseboard-v1 -BuildDir build
```

旧名字 `custom-baseboard-v1` 仍可用，但现在只作为兼容别名。

## 2. `build.ps1` 的关键参数

- `-BuildDir`
  输出目录。
- `-Preset`
  从 `scripts/board-presets.json` 读取板级参数。
- `-FlashSizeMB`
  覆盖 Flash 容量。
- `-CredentialCapacity`
  显式限制最大凭据数；`0` 表示按存储区域自动计算。
- `-CredentialStoreKB`
  凭据存储区域大小，必须是 `4 KB` 的整数倍，且当前至少为 `16 KB`；扣掉 `8 KB` journal 后剩余数据区会按 A/B 事务槽均分。
- `-DisableDebugHid`
  关闭 Debug HID，仅保留标准 FIDO HID。
- `-UserPresenceSource`
  `none | bootsel | gpio`。当前 `meowkey_rp2350_usb` 默认是 `bootsel`，即双击 `BOOTSEL` 作为运行时 UP；需要关闭时可显式传 `-UserPresenceSource none`。
- `-UserPresenceGpioPin`
  当 `-UserPresenceSource gpio` 时指定按键引脚。
- `-UserPresenceGpioActiveState`
  `low | high`。
- `-UserPresenceTapCount`
  需要的点击次数。
- `-UserPresenceGestureWindowMs`
  多击判定窗口。
- `-UserPresenceTimeoutMs`
  一次 UP 请求的超时。
- `-Probe`
  构建单独的板卡探测固件 `meowkey_probe`。
- `-BoardIdMode`
  `none | gpio | i2c-eeprom`。
- `-BoardIdGpioPins`
  GPIO 方式的板卡 ID 引脚列表。
- `-BoardIdI2cPreset`
  `24c02 | 24c32 | 24c64 | custom`。
- `-NoPicotool`
  跳过 `picotool`，因此不会生成 `UF2`。
- `-IgnoreGitGlobalConfig`
  临时忽略本机 Git 全局配置，适合存在代理污染时使用。
- `-VersionMajor`
- `-VersionMinor`
- `-VersionPatch`
- `-VersionLabel`
  设置固件版本字符串；例如 `0.2.0-rc1`。

## 3. 产物

默认情况下，构建目录中会出现：

- `meowkey.elf`
- `meowkey.bin`
- `meowkey.hex`
- `meowkey.dis`
- `meowkey.elf.map`

只有在启用 `picotool` 时，才会生成：

- `meowkey.uf2`

如果传了 `-Probe`，产物名会变成：

- `meowkey_probe.elf`
- `meowkey_probe.bin`
- `meowkey_probe.hex`
- `meowkey_probe.elf.map`
- `meowkey_probe.uf2`（启用 `picotool` 时）

## 4. 直接 CMake 构建

CI 和 Linux / macOS 环境更适合直接走 CMake，而不是依赖 Windows 脚本：

```bash
cmake -S . -B build-ci -G Ninja \
  -DPICO_BOARD=meowkey_rp2350_usb \
  -DPICO_NO_PICOTOOL=ON
cmake --build build-ci
```

关闭 Debug HID：

```bash
cmake -S . -B build-ci-hardened -G Ninja \
  -DPICO_BOARD=meowkey_rp2350_usb \
  -DPICO_NO_PICOTOOL=ON \
  -DMEOWKEY_ENABLE_DEBUG_HID=OFF
cmake --build build-ci-hardened
```

如果需要发行用 `UF2`，不要传 `-DPICO_NO_PICOTOOL=ON`。

## 5. 刷写

Windows 的 `flash.ps1` 会通过 `INFO_UF2.TXT` 自动寻找 RP2350/RP2 的 UF2 盘符：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash.ps1 -Uf2Path .\build\meowkey.uf2
```

Linux 可使用配套的 `flash.sh`：

```bash
./scripts/flash.sh ./build/meowkey.uf2
```

如果脚本所在目录里只有一个 `*.uf2` 文件，`flash.ps1` 和 `flash.sh` 也都支持省略 UF2 路径参数。

## 6. 调试工具

浏览器调试台：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\gui.ps1
```

Rust 原生管理器：

```powershell
cd .\native-rs\meowkey-manager
cargo run
```

注意：

- 这两个调试工具都依赖 Debug HID。
- 若使用 `-DisableDebugHid` 构建，调试工具会失去设备访问能力。
- `probe` 固件不使用 Debug HID，而是通过 USB 串口输出 JSON 探测报告。
- UP 配置会持久化到 Flash；如果设备之前已保存过 `source=none` 或 `source=gpio`，重新刷默认固件不会自动覆盖该配置。
- 如果你要显式关闭当前板卡的 BOOTSEL UP，直接传 `-UserPresenceSource none`；后续 UI 也会复用同一条持久化配置接口。

## 7. Probe 固件与 preset 草案

刷入 `probe` 固件后，执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1
```

如果有多个串口设备，可以显式指定：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -Port COM7
```

如果你已经保存了 probe 报告，也可以直接从文件生成 preset 草案：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -InputPath .\probe-report.json
```

如果需要把这份可读输出单独保存成文本文件：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -TextOutputPath .\probe-report.txt
```

如果需要机器可读的完整 JSON bundle：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-board.ps1 -OutputPath .\probe-bundle.json
```

脚本会输出：

- 一个可复制到 `board-presets.json` 的 preset 片段
- 选择该草案的备注说明
- 原始 probe 报告

新版 probe 报告还会包含两类启发式候选：

- `userPresence.gpioButtonCandidates`
  观察窗口内检测到的额外 GPIO 按键候选，用于后续人工确认是否可绑定为真实 UP。
- `userVerification.i2cCandidates`
  非 EEPROM 的 I2C 外设候选，用于后续人工确认是否存在可接入的 UV 模块。

## 8. 常见问题

### 8.1 为什么没有 `UF2`

你传了 `-NoPicotool`，或者 `picotool` 拉取失败。开发期离线检查可以接受，发行构建不行。

### 8.2 为什么构建时会卡在拉取 `picotool`

常见原因是本机 Git 全局代理配置有问题。优先尝试：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build -IgnoreGitGlobalConfig
```

### 8.3 为什么 CI 不直接复用仓库里的 `tools/`

`tools/` 默认被 `.gitignore` 忽略，不应假定它存在于远端仓库。GitHub Actions 使用系统安装的 `arm-none-eabi-gcc` 和 `ninja`，并在 workflow 中显式拉取 `pico-sdk` 2.2.0，再通过 `-DPICO_SDK_PATH=...` 覆盖本地默认路径。
