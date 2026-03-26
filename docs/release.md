# CI 与发行流程

## 1. CI 现在检查什么

GitHub Actions 的 `ci.yml` 会做两类检查。

### 1.1 固件检查

在 Ubuntu 上执行：

- `scripts/board-presets.json` JSON 解析校验
- `scripts/gui_server.py` 语法校验
- 默认开发固件离线构建
- 关闭 Debug HID 的硬化固件离线构建
- `probe` 固件离线构建

这里使用系统安装的 `arm-none-eabi-gcc`、`cmake` 和 `ninja`，不依赖本地 `tools/`。
同时会在 workflow 里显式拉取 `pico-sdk` `2.2.0` 及其子模块，不依赖仓库里是否包含 `third_party/pico-sdk`。

### 1.2 桌面工具检查

在 Windows 上执行：

- `scripts/probe-board.ps1` 样例输入冒烟检查
- `cargo check --locked --manifest-path native-rs/meowkey-manager/Cargo.toml`
- `dotnet build native/MeowKey.Manager/MeowKey.Manager.csproj -c Release`

## 2. 为什么 CI 不直接运行 `build.ps1`

因为：

- `build.ps1` 偏向本地 Windows 开发环境
- 仓库默认并不假设 `tools/` 会被提交到远端
- GitHub Actions 更适合直接安装系统交叉编译器、拉取 `pico-sdk`，再通过 `-DPICO_SDK_PATH` 跑 CMake

本地开发仍然推荐用 `build.ps1`。

## 3. Release 工作流

`release.yml` 会在以下场景触发：

- 推送形如 `v0.2.0` 的 tag
- 手动触发并指定版本号

工作流会构建三个发行变体：

- `debug`
  保留 Debug HID，主要用于开发者联调。
- `hardened`
  关闭 Debug HID，作为更适合对外发布的默认固件。
- `probe`
  独立板卡探测固件，用于生成新的 preset 草案。

## 4. Release 产物

`debug` 和 `hardened` 变体会打包：

- `meowkey.uf2`
- `meowkey.bin`
- `meowkey.hex`
- `meowkey.elf`
- `meowkey.elf.map`
- `generated/meowkey_build_config.h`
- `manifest.json`
- `meowkey-<tag>-<variant>.zip.sha256`

`probe` 变体会打包：

- `meowkey_probe.uf2`
- `meowkey_probe.bin`
- `meowkey_probe.hex`
- `meowkey_probe.elf`
- `meowkey_probe.elf.map`
- `manifest.json`
- `meowkey-<tag>-probe.zip.sha256`

GitHub Release 页面会直接附上三类 zip 包。

## 5. 版本约定

推荐使用：

- `v0.1.0`
- `v0.2.0`
- `v0.2.1`

工作流会把 tag 解析为：

- `MEOWKEY_VERSION_MAJOR`
- `MEOWKEY_VERSION_MINOR`
- `MEOWKEY_VERSION_PATCH`

正式 release 不携带 `VersionLabel`。

## 6. 本地提交前建议

### 6.1 Windows 本地

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

### 6.2 Linux / CI 风格

```bash
cmake -S . -B build-ci -G Ninja -DPICO_BOARD=meowkey_rp2350_usb -DPICO_NO_PICOTOOL=ON
cmake --build build-ci
cmake -S . -B build-ci-hardened -G Ninja -DPICO_BOARD=meowkey_rp2350_usb -DPICO_NO_PICOTOOL=ON -DMEOWKEY_ENABLE_DEBUG_HID=OFF
cmake --build build-ci-hardened
cmake --build build-ci --target meowkey_probe
```

## 7. 当前发布边界

自动发布不代表“已经适合量产”。

release workflow 只保证：

- 固件能构建
- 产物会被打包
- Debug / hardened / probe 三类变体可区分

它不保证：

- FIDO 认证通过
- 板上真实行为正确
- secure boot 已启用
- 存储层已经具备掉电安全
