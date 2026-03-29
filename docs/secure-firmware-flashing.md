# Secure Firmware Flashing

## English

### 1. Scope

This guide is for flashing release firmware with a stronger trust boundary on RP2350, especially the `*-hardened-secure-boot-ready` packages.

It does not replace a factory provisioning flow.

### 2. Pre-Flash Checklist

1. Select the correct package:
- Use `generic-hardened` or `preset-<label>-hardened` for hardened unsigned deployment.
- Use `*-hardened-secure-boot-ready` when you are preparing signed-boot and anti-rollback.
2. Verify archive integrity:
- Download `SHA256SUMS.txt` from the same release tag.
- Verify the zip checksum before unpacking.
3. Inspect package metadata:
- Open `manifest.json` and confirm `variant`, `secureBootSigned`, and `antiRollbackEnabled` match your target.
- Confirm `debugHidEnabled` is `false` for production-oriented usage.
4. Confirm hardware identity:
- If board identity is unknown, run the probe workflow first (`probe-board-id` package + `scripts/probe-board.ps1`).

### 3. Flash Procedure

1. Unzip the selected firmware package.
2. Put the RP2350 board into UF2 boot mode (`BOOTSEL`).
3. Flash the UF2:

```powershell
powershell -ExecutionPolicy Bypass -File .\flash.ps1
```

or

```bash
./flash.sh
```

When there is exactly one `.uf2` beside the script, no extra path is required.

4. Reboot and reconnect the device normally.

### 4. Post-Flash Verification

1. Confirm the manager can enumerate the formal management interface (WinUSB on Windows).
2. In manager security information, check:
- signed-boot state
- anti-rollback state and version
- debug exposure state (should reflect hardened expectations)
3. Keep release zip + `SHA256SUMS.txt` + `manifest.json` together for traceability.

### 5. OTP and Anti-Rollback Notes

1. `meowkey.otp.json` is generated for secure-boot-ready artifacts to support your OTP workflow.
2. OTP operations are irreversible. Validate the full flow on a non-critical board first.
3. Enabling anti-rollback permanently constrains acceptable firmware version progression.
4. Signed-boot and anti-rollback improve firmware authenticity, but do not create a hardware secure-element key boundary.

### 6. Recovery and Risk Boundaries

1. A successful flash does not prove FIDO certification, full board compatibility, or factory-grade recovery readiness.
2. Keep at least one known-good release package and its checksum record offline.
3. Treat key handling, OTP programming, and release approval as separate controlled steps.

## 中文

### 1. 适用范围

这份指南用于在 RP2350 上刷入更强信任边界的发布固件，重点覆盖 `*-hardened-secure-boot-ready` 包。

它不等于工厂级 provisioning 流程。

### 2. 刷写前检查清单

1. 选对包：
- 只需要硬化分发基线时，用 `generic-hardened` 或 `preset-<label>-hardened`。
- 需要准备 signed boot 与 anti-rollback 时，用 `*-hardened-secure-boot-ready`。
2. 校验发布完整性：
- 从同一个 release tag 下载 `SHA256SUMS.txt`。
- 刷写前先校验 zip 摘要。
3. 检查包内元数据：
- 打开 `manifest.json`，确认 `variant`、`secureBootSigned`、`antiRollbackEnabled` 与目标一致。
- 面向分发时确认 `debugHidEnabled` 为 `false`。
4. 确认底板身份：
- 如果底板未知，先走 probe 流程（`probe-board-id` 包 + `scripts/probe-board.ps1`）。

### 3. 刷写步骤

1. 解压目标固件包。
2. 让 RP2350 进入 UF2 启动模式（`BOOTSEL`）。
3. 执行刷写：

```powershell
powershell -ExecutionPolicy Bypass -File .\flash.ps1
```

或

```bash
./flash.sh
```

如果脚本同目录下只有一个 `.uf2`，无需额外传路径。

4. 刷写完成后重启设备并正常重新连接。

### 4. 刷后验证

1. 确认管理器可以枚举正式管理接口（Windows 下为 WinUSB）。
2. 在管理器安全信息里核对：
- signed boot 状态
- anti-rollback 状态和版本
- debug 暴露状态（应符合 hardened 预期）
3. 把 release zip、`SHA256SUMS.txt`、`manifest.json` 一起留存，便于追溯。

### 5. OTP 与 Anti-Rollback 注意事项

1. secure-boot-ready 产物会带 `meowkey.otp.json`，用于你的 OTP 流程。
2. OTP 写入不可逆，先在非关键板卡上完整演练。
3. 启用 anti-rollback 后，可接受固件版本会被永久约束为单向递进。
4. signed boot / anti-rollback 提升的是固件真实性边界，不等于硬件安全元件密钥边界。

### 6. 恢复与风险边界

1. 刷写成功不代表已经通过 FIDO 一致性、覆盖全部底板、或具备工厂级恢复能力。
2. 至少离线保存一份“已验证可用”的发布包和对应校验记录。
3. 把密钥管理、OTP 烧录、发布审批当成独立受控步骤执行。
