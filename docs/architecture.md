# 架构说明

## 1. 仓库分层

MeowKey 当前分成五层：

- 固件
  `src/` 下的 RP2350 认证器实现。
- 板卡探测固件
  单独的 `probe` 固件，用于板卡 ID 信息采集与 preset 草案生成。
- 开发调试入口
  `gui/` 的浏览器 WebHID 调试台。
- 桌面调试管理器
  `native-rs/meowkey-manager/` 的 Rust / egui 应用。
- 早期原型
  `native/` 的 WPF 原型，仅保留作参考。

## 2. 固件主路径

启动路径在 `src/main.c`：

1. 初始化板级支持、LED、诊断缓存和板卡 ID。
2. 初始化 CTAP HID 状态机。
3. 启动 TinyUSB。
4. 在主循环中轮询：
   - `tud_task()`
   - `ctap_hid_task()`
   - LED 状态同步

USB 接收到 HID 输出报文后，流程是：

1. `tud_hid_set_report_cb()`
2. `ctap_hid_handle_report()`
3. `ctap2_handle_cbor()`
4. 分发到：
   - `meowkey_webauthn_make_credential()`
   - `meowkey_webauthn_get_assertion()`
   - `meowkey_client_pin_handle()`

## 3. USB 接口

默认开发构建会暴露两个 HID 接口：

- FIDO HID
  `usagePage=0xF1D0`, `usage=0x01`
- Debug HID
  `usagePage=0xFF00`, `usage=0x01`

如果构建时关闭 `MEOWKEY_ENABLE_DEBUG_HID`，设备只保留标准 FIDO HID。

## 4. CTAPHID 层

`src/ctap_hid.c` 当前已经支持：

- 多包请求重组
- 多包响应分片
- `INIT`
- `PING`
- `CBOR`
- Debug 专用 `DIAG`

仍未完整实现的 CTAPHID 行为包括：

- `LOCK`
- `WINK`
- 有意义的 `CANCEL`
- 更严格的通道并发 / 争用处理

## 5. CTAP2 层

`src/ctap2.c` 目前处理五类命令：

- `authenticatorMakeCredential`
- `authenticatorGetAssertion`
- `authenticatorGetNextAssertion`
- `authenticatorGetInfo`
- `authenticatorClientPIN`

当前 `getInfo` 暴露的是开发期能力集合，版本字符串已经收敛到 `FIDO_2_0`，避免误报未实现的 2.1 权限 token 语义。

## 6. WebAuthn 层

`src/webauthn.c` 实现了开发期最小闭环：

- 解析注册请求
- 生成 P-256 密钥对
- 生成 discoverable credential
- 持久化私钥、RP ID、用户信息和 `credRandom`
- 构造 `authData`
- 对断言做 ES256 签名
- 支持 `hmac-secret`

当前行为有几个重要事实：

- 所有已创建凭据都会被当作 discoverable credential 存储。
- `getAssertion` 无 `allowList` 且命中多条 discoverable credential 时，会返回 `numberOfCredentials` 并允许继续走 `getNextAssertion`。
- 当前已经支持可配置的 UP 源。
- 对当前 `meowkey_rp2350_usb` 板卡，默认 UP 源已经切到 `BOOTSEL`；若后续有独立按键，仍建议再切到 GPIO。
- attestation 仍固定为 `"none"`。

## 7. Client PIN

`src/client_pin.c` 当前支持：

- `getRetries`
- `getKeyAgreement`
- `setPin`
- `changePin`
- `getPinToken`

已经明确不支持：

- `getPinTokenWithPermissions`

当前实现的几个边界：

- `pinUvAuthToken` 是运行时令牌，不会持久化到 Flash。
- `pinUvAuthToken` 现在是短生命周期令牌，超时后失效。
- `pinHash` 和重试计数仍持久化在 Flash。
- token 权限范围没有实现，因此直接拒绝权限 token 子命令。
- UP 配置已经有持久化接口，也预留了调试 HID 读写入口；桌面 UI 还没接上，但后续只需要对接现有 `DIAG 5/6`。

## 8. 凭据存储

`src/credential_store.c` 把凭据区放在 Flash 尾部，并维护了多代存储格式迁移：

- `version 1`
- `version 2`
- `version 3`
- 当前 `version 6`

当前存储内容包括：

- 凭据私钥
- `credentialId`
- RP ID
- 用户信息
- `credRandom`
- `signCount`
- PIN 哈希
- PIN 重试计数

当前存储层的现实限制：

- 仍然没有磨损均衡。
- 私钥与 PIN 哈希仍明文落在普通 Flash。
- 主区已经改成带 `generation/crc` 的 A/B 事务提交，`signCount` journal 也会和主区代际绑定。
- 这已经能扛掉电，但还不是量产级的 secure storage。

## 9. 板卡 ID

`src/board_id.c` 支持两类板卡识别：

- GPIO 采样
- I2C EEPROM 读取

诊断摘要会在启动时写入固件内部日志。

除此之外，仓库现在还有单独的 `probe` 固件：

- 通过 USB 串口输出 JSON 探测报告
- 扫描 GPIO 编码电阻/拔码候选
- 扫描常见 I2C EEPROM/ID 候选
- 由 `scripts/probe-board.ps1` 生成 preset 草案

## 10. 存储布局

`src/credential_store.c` 现在把尾部保留区拆成两部分：

- 主凭据区
- `signCount` journal 区

当前格式版本为 `version 6`。

这意味着：

- 凭据主体仍保存在 Flash 尾部
- 主区按 A/B 槽提交，并在 header 里记录 `generation` / `crc`
- `signCount` journal 会绑定当前主区版本与校验值，避免压实后错配
- journal 满了以后会把当前视图压实进新的主槽

这已经是事务化存储的第一版，但还不是最终的量产方案。

## 11. 开发工具如何接入

### 浏览器 WebUI

- 只连接 Debug HID。
- 用于快速发送 `INIT`、`PING`、`getInfo`、`makeCredential`、`getAssertion`。

### Rust 管理器

- 优先连接 Debug HID。
- 支持拉取诊断、列出凭据、清空凭据、注册和断言测试。

这两个工具都不是通用 FIDO 客户端，而是固件开发调试前端。
