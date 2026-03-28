# Release Workflow

## English

### 1. What CI Checks

GitHub Actions currently runs two main groups of checks.

Firmware-side checks on Ubuntu:

- `scripts/board-presets.json` parsing
- `scripts/gui_server.py` syntax validation
- default offline firmware build
- hardened offline firmware build
- secure-boot-ready hardened build with a temporary signing key
- probe firmware build

Desktop-side checks on Windows:

- `scripts/probe-board.ps1` sample-input smoke test
- `cargo check --locked --manifest-path native-rs/meowkey-manager/Cargo.toml`
- `dotnet build windows/gui/MeowKey.Manager/MeowKey.Manager.csproj -c Release`

### 2. Why CI Does Not Use `build.ps1`

`build.ps1` is optimized for local Windows development. CI instead installs toolchain pieces directly and drives CMake explicitly, because:

- `tools/` is ignored by Git
- the workflow should not assume a pre-populated local toolchain directory
- Linux CI is better aligned with direct CMake invocation

### 3. Release Triggers

`release.yml` runs on:

- tags like `v0.2.0`
- manual dispatch with an explicit version input

The workflow is aimed at redistributable open-source packages that other users can flash or inspect, not at factory SKUs.

### 4. Release Packages

Current package families:

- `generic-debug`
- `generic-hardened`
- `generic-hardened-secure-boot-ready`
- `preset-<label>-debug`
- `preset-<label>-hardened`
- `preset-<label>-hardened-secure-boot-ready`
- `probe-board-id`
- `manager-winui`
- `manager-rust-linux`

The secure-boot-ready variants appear only when the workflow runs inside the GitHub Actions Environment `release` and that environment has access to the signing key secret.

The current GitHub Actions release workflow expects the environment secret:

- `MEOWKEY_SECUREBOOT_PEM_B64`

That value must be the base64-encoded contents of an `RSA` or `EC` private key in PEM form. The workflow decodes it into `${{ github.workspace }}/.cache/meowkey-secureboot.pem`, validates that the key is `RSA` or `EC`, and then enables the secure-boot-ready package set. Do not keep this as a repository-wide secret if you want release-environment protection rules to apply.

Example commands:

```bash
openssl ecparam -name secp256k1 -genkey -noout -out meowkey-secureboot.pem
base64 -w 0 meowkey-secureboot.pem
```

### 5. Release Contents

Firmware archives include:

- `meowkey.uf2`
- `meowkey.bin`
- `meowkey.hex`
- `meowkey.elf`
- `meowkey.elf.map`
- `meowkey_build_config.h`
- `manifest.json`
- `flash.ps1`
- `flash.sh`

Secure-boot-ready archives additionally include:

- `meowkey.otp.json`

Probe archives include:

- `meowkey_probe.uf2`
- `meowkey_probe.bin`
- `meowkey_probe.hex`
- `meowkey_probe.elf`
- `meowkey_probe.elf.map`
- `manifest.json`
- `flash.ps1`
- `flash.sh`

Manager WinUI archives include:

- published WinUI desktop binaries (`win-x64`)
- `README.md`

Manager Rust-Linux archives include:

- `meowkey-manager` Linux binary
- `README.md`

Checksums are collected in:

- `SHA256SUMS.txt`

### 6. Versioning

Recommended tags:

- `v0.1.0`
- `v0.2.0`
- `v0.2.1`
- `v0.2.0-beta.2`

The workflow derives:

- `MEOWKEY_VERSION_MAJOR`
- `MEOWKEY_VERSION_MINOR`
- `MEOWKEY_VERSION_PATCH`
- optional `MEOWKEY_VERSION_LABEL`

When anti-rollback is enabled, the default compact version encoding is `major * 64 + minor * 8 + patch`, so `minor` and `patch` must stay within `0..7`.

### 7. Local Preflight

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

CI-style CMake:

```bash
cmake -S . -B build-ci -G Ninja -DPICO_BOARD=meowkey_rp2350_usb -DPICO_NO_PICOTOOL=ON
cmake --build build-ci
cmake -S . -B build-ci-hardened -G Ninja -DPICO_BOARD=meowkey_rp2350_usb -DPICO_NO_PICOTOOL=ON -DMEOWKEY_ENABLE_DEBUG_HID=OFF
cmake --build build-ci-hardened
cmake --build build-ci --target meowkey_probe
```

### 8. Release Boundary

An automated release does not prove:

- FIDO conformance
- board-specific correctness on real hardware
- OTP programming on the target device
- a factory-grade upgrade and recovery story

It proves only that the package set can be built and organized consistently.

## 中文

### 1. CI 当前检查什么

GitHub Actions 现在主要做两类检查。

Ubuntu 上的固件检查：

- `scripts/board-presets.json` 解析校验
- `scripts/gui_server.py` 语法检查
- 默认固件离线构建
- 硬化固件离线构建
- 使用临时签名密钥的 secure-boot-ready 硬化构建
- probe 固件构建

Windows 上的桌面工具检查：

- `scripts/probe-board.ps1` 样例输入冒烟测试
- `cargo check --locked --manifest-path native-rs/meowkey-manager/Cargo.toml`
- `dotnet build windows/gui/MeowKey.Manager/MeowKey.Manager.csproj -c Release`

### 2. 为什么 CI 不直接用 `build.ps1`

`build.ps1` 更偏向本地 Windows 开发环境。CI 之所以改用显式安装工具链并直接调用 CMake，主要是因为：

- `tools/` 默认被 Git 忽略
- workflow 不应假设远端仓库天然带有本地工具链目录
- Linux CI 更适合直接跑 CMake

### 3. Release 触发方式

`release.yml` 会在下列场景触发：

- 推送形如 `v0.2.0` 的 tag
- 手动触发并显式指定版本号

这套流程面向的是可以被别人下载、检查和刷写的开源发布包，不是工厂 SKU 流程。

### 4. 发布包类型

当前的发布包系列包括：

- `generic-debug`
- `generic-hardened`
- `generic-hardened-secure-boot-ready`
- `preset-<label>-debug`
- `preset-<label>-hardened`
- `preset-<label>-hardened-secure-boot-ready`
- `probe-board-id`
- `manager-winui`
- `manager-rust-linux`

其中 secure-boot-ready 变体只有在 workflow 运行于 GitHub Actions Environment `release`，并且该环境能够读取签名密钥 secret 时才会生成。

当前 GitHub Actions release workflow 期望配置在环境里的 secret 名称是：

- `MEOWKEY_SECUREBOOT_PEM_B64`

它的值需要是 `RSA` 或 `EC` 私钥 PEM 内容的 base64 编码。workflow 会先把它解码到 `${{ github.workspace }}/.cache/meowkey-secureboot.pem`，校验算法确实属于 `RSA` 或 `EC`，然后才会启用 secure-boot-ready 这一组发布包。如果你希望 release 环境的审批或访问控制真正生效，就不应把它继续保留为仓库级 secret。

示例命令：

```bash
openssl ecparam -name secp256k1 -genkey -noout -out meowkey-secureboot.pem
base64 -w 0 meowkey-secureboot.pem
```

### 5. 发布包内容

普通固件压缩包内包含：

- `meowkey.uf2`
- `meowkey.bin`
- `meowkey.hex`
- `meowkey.elf`
- `meowkey.elf.map`
- `meowkey_build_config.h`
- `manifest.json`
- `flash.ps1`
- `flash.sh`

secure-boot-ready 压缩包会额外包含：

- `meowkey.otp.json`

probe 压缩包包含：

- `meowkey_probe.uf2`
- `meowkey_probe.bin`
- `meowkey_probe.hex`
- `meowkey_probe.elf`
- `meowkey_probe.elf.map`
- `manifest.json`
- `flash.ps1`
- `flash.sh`

WinUI 管理器压缩包包含：

- 发布后的 WinUI 桌面二进制（`win-x64`）
- `README.md`

Rust-Linux 管理器压缩包包含：

- `meowkey-manager` Linux 可执行文件
- `README.md`

统一校验清单放在：

- `SHA256SUMS.txt`

### 6. 版本约定

推荐标签形式：

- `v0.1.0`
- `v0.2.0`
- `v0.2.1`
- `v0.2.0-beta.2`

workflow 会据此派生：

- `MEOWKEY_VERSION_MAJOR`
- `MEOWKEY_VERSION_MINOR`
- `MEOWKEY_VERSION_PATCH`
- 可选的 `MEOWKEY_VERSION_LABEL`

如果启用了 anti-rollback，默认使用的紧凑版本编码为 `major * 64 + minor * 8 + patch`，因此 `minor` 和 `patch` 需要保持在 `0..7` 范围内。

### 7. 本地发布前检查

Windows：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

CI 风格 CMake：

```bash
cmake -S . -B build-ci -G Ninja -DPICO_BOARD=meowkey_rp2350_usb -DPICO_NO_PICOTOOL=ON
cmake --build build-ci
cmake -S . -B build-ci-hardened -G Ninja -DPICO_BOARD=meowkey_rp2350_usb -DPICO_NO_PICOTOOL=ON -DMEOWKEY_ENABLE_DEBUG_HID=OFF
cmake --build build-ci-hardened
cmake --build build-ci --target meowkey_probe
```

### 8. 发布边界

自动化 release 不能证明：

- 已经通过 FIDO 一致性测试
- 各个底板在真实硬件上都行为正确
- 目标设备已经真正烧录 OTP
- 已经具备工厂级升级和恢复流程

它只能证明：当前定义的发布包集合能够被一致地构建和组织出来。
