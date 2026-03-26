# Architecture

## English

### 1. Repository Layers

MeowKey currently has five practical layers:

- firmware in `src/`
- standalone board probe firmware
- browser WebHID debug UI in `gui/`
- WinUI 3 Windows manager shell in `windows/gui/MeowKey.Manager/`
- Rust / egui Linux manager shell in `native-rs/meowkey-manager/`

### 2. Firmware Main Path

Boot starts in `src/main.c`:

1. initialize board support, LEDs, diagnostics, and board ID
2. initialize the CTAP HID state machine
3. start TinyUSB
4. run the main loop with `tud_task()`, `ctap_hid_task()`, and LED updates

Incoming HID output reports flow through:

1. `tud_hid_set_report_cb()`
2. `ctap_hid_handle_report()`
3. `ctap2_handle_cbor()`
4. WebAuthn or Client PIN handlers

### 3. USB Interfaces

Development builds enumerate:

- FIDO HID: `usagePage=0xF1D0`, `usage=0x01`
- Debug HID: `usagePage=0xFF00`, `usage=0x01`

When `MEOWKEY_ENABLE_DEBUG_HID` is disabled, only standard FIDO HID remains.

### 4. CTAPHID Layer

`src/ctap_hid.c` already handles:

- multi-packet request reassembly
- multi-packet response fragmentation
- `INIT`
- `PING`
- `CBOR`
- debug-only `DIAG`

Still incomplete:

- `LOCK`
- `WINK`
- meaningful `CANCEL`
- stricter channel contention handling

### 5. CTAP2 Layer

`src/ctap2.c` currently routes:

- `authenticatorMakeCredential`
- `authenticatorGetAssertion`
- `authenticatorGetNextAssertion`
- `authenticatorGetInfo`
- `authenticatorClientPIN`

`getInfo` deliberately reports `FIDO_2_0` only, so the firmware does not over-claim 2.1 permission-token behavior that is not implemented yet.

### 6. WebAuthn Layer

`src/webauthn.c` implements the current closed loop:

- parse registration requests
- generate P-256 keypairs
- create discoverable credentials
- persist private key, RP ID, user data, and `credRandom`
- build `authData`
- sign assertions with ES256
- serve `hmac-secret`

Current important behavior:

- all credentials are stored as discoverable credentials
- a multi-match discoverable assertion returns `numberOfCredentials` and enables `getNextAssertion`
- `makeCredential` always requires UP unless UP source is set to `none`
- `getAssertion` requires UP, but allows one short retry reuse window after a successful assertion
- `getNextAssertion` reuses pending state and does not ask for fresh UP again
- a valid `pinUvAuthParam` marks the operation as verified and affects both the UV bit and `hmac-secret` derivation
- attestation is fixed to `"none"`

### 7. Client PIN

`src/client_pin.c` supports:

- `getRetries`
- `getKeyAgreement`
- `setPin`
- `changePin`
- `getPinToken`

Explicitly unsupported:

- `getPinTokenWithPermissions`

Current boundaries:

- only `pinUvAuthProtocol=1`
- runtime token is session-only and expires after `30000ms`
- every successful `getPinToken` rotates the token
- PIN hash and retry count remain in flash
- peer public keys are validated before ECDH shared-secret derivation

### 8. Credential Storage

`src/credential_store.c` keeps the store at the end of flash and migrates several historical formats up to the current `version 6`.

Stored material includes:

- credential private key
- credential ID
- RP ID
- user metadata
- `credRandom`
- sign count
- PIN hash
- PIN retries
- user-presence config

Current storage reality:

- no wear leveling yet
- private key, `credRandom`, and PIN hash are wrapped at rest with device-bound material, but not protected by a secure element
- the main area uses A/B transactional commits with generation and CRCs
- sign-count persistence uses a separate journal

### 9. Board Identification and Probe

`src/board_id.c` supports:

- GPIO sampling
- I2C EEPROM reads

The standalone probe image goes further:

- USB serial JSON reports
- GPIO resistor / DIP candidate scanning
- common I2C EEPROM / ID scanning
- secure-element address probing with safe read-only identification for supported families
- preset draft generation through `scripts/probe-board.ps1`

### 10. Desktop Tools

Browser WebUI:

- connects only to Debug HID
- sends `INIT`, `PING`, `getInfo`, `makeCredential`, and `getAssertion`

WinUI manager:

- organizes the app into overview, devices, credentials, security, maintenance, and activity
- follows the Android-Cam-Bridge desktop layout rhythm on Windows
- presents current management gaps explicitly instead of centering the UI on raw protocol tests

Rust manager shell:

- keeps the Linux-facing desktop surface on `Rust + egui/eframe`
- remains the practical maintenance and bring-up workbench for Debug HID flows
- still covers diagnostics, credential summaries, and registration / assertion tests

The browser UI and Rust shell remain development and maintenance frontends, while the WinUI manager is the product-facing desktop shell. None of them are generic FIDO clients.

## 中文

### 1. 仓库分层

MeowKey 当前可以分成五个主要层次：

- `src/` 中的固件实现
- 独立的板卡探测固件
- `gui/` 下的浏览器 WebHID 调试界面
- `windows/gui/MeowKey.Manager/` 下的 WinUI 3 Windows 管理器壳层
- `native-rs/meowkey-manager/` 下的 Rust / egui Linux 管理器壳层

### 2. 固件主路径

启动入口在 `src/main.c`：

1. 初始化板级支持、LED、诊断缓存和板卡 ID
2. 初始化 CTAP HID 状态机
3. 启动 TinyUSB
4. 在主循环中持续运行 `tud_task()`、`ctap_hid_task()` 和 LED 同步

USB 收到 HID 输出报文后的处理路径为：

1. `tud_hid_set_report_cb()`
2. `ctap_hid_handle_report()`
3. `ctap2_handle_cbor()`
4. 分发到 WebAuthn 或 Client PIN 处理函数

### 3. USB 接口

开发构建默认会枚举两个 HID 接口：

- FIDO HID：`usagePage=0xF1D0`，`usage=0x01`
- Debug HID：`usagePage=0xFF00`，`usage=0x01`

如果关闭 `MEOWKEY_ENABLE_DEBUG_HID`，设备就只保留标准 FIDO HID。

### 4. CTAPHID 层

`src/ctap_hid.c` 目前已经支持：

- 多包请求重组
- 多包响应分片
- `INIT`
- `PING`
- `CBOR`
- 调试专用 `DIAG`

还没有完整实现的部分包括：

- `LOCK`
- `WINK`
- 有意义的 `CANCEL`
- 更严格的通道并发与争用控制

### 5. CTAP2 层

`src/ctap2.c` 当前负责分发：

- `authenticatorMakeCredential`
- `authenticatorGetAssertion`
- `authenticatorGetNextAssertion`
- `authenticatorGetInfo`
- `authenticatorClientPIN`

`getInfo` 现在只上报 `FIDO_2_0`，这样可以避免把尚未实现的 2.1 权限 token 语义误报为“已经支持”。

### 6. WebAuthn 层

`src/webauthn.c` 目前实现了一个可用闭环：

- 解析注册请求
- 生成 P-256 密钥对
- 创建 discoverable credential
- 持久化私钥、RP ID、用户信息和 `credRandom`
- 构造 `authData`
- 对断言做 ES256 签名
- 处理 `hmac-secret`

当前比较关键的行为是：

- 所有凭据都会按 discoverable credential 方式存储
- 如果一次断言命中多条 discoverable credential，会返回 `numberOfCredentials` 并允许继续调用 `getNextAssertion`
- `makeCredential` 默认每次都要求一次 UP，除非当前 UP 源被设置为 `none`
- `getAssertion` 也要求 UP，但会为“刚刚成功的一次断言重试”保留一个很短的一次性复用窗口
- `getNextAssertion` 依赖上一次留下的 pending state，不会再次要求新的 UP
- 只要 `pinUvAuthParam` 合法，本次请求就会被视为已验证用户，并影响 UV bit 和 `hmac-secret`
- attestation 仍固定为 `"none"`

### 7. Client PIN

`src/client_pin.c` 当前支持：

- `getRetries`
- `getKeyAgreement`
- `setPin`
- `changePin`
- `getPinToken`

明确未实现：

- `getPinTokenWithPermissions`

当前边界包括：

- 只支持 `pinUvAuthProtocol=1`
- 运行时 token 只存在于当前会话，并在 `30000ms` 后失效
- 每次成功获取 `getPinToken` 都会轮换 token
- PIN 哈希和重试次数仍然保存在 Flash 中
- 对端公钥在做 ECDH 共享密钥推导前会先做显式校验

### 8. 凭据存储

`src/credential_store.c` 把存储区放在 Flash 尾部，并负责把多代历史格式迁移到当前 `version 6`。

当前会持久化的内容包括：

- 凭据私钥
- 凭据 ID
- RP ID
- 用户元数据
- `credRandom`
- `signCount`
- PIN 哈希
- PIN 重试计数
- user presence 配置

当前存储层的真实情况是：

- 还没有磨损均衡
- 私钥、`credRandom` 和 PIN 哈希已经做了基于设备唯一材料的 wrapping，但并不等于安全元件保护
- 主存储区使用 A/B 事务提交，并带有 generation 和 CRC
- `signCount` 走独立 journal

### 9. 板卡识别与 Probe

`src/board_id.c` 当前支持：

- GPIO 采样
- I2C EEPROM 读取

独立的 probe 固件在这之上做了更多工作：

- 通过 USB 串口输出 JSON 报告
- 扫描 GPIO 编码电阻 / 拨码候选
- 扫描常见 I2C EEPROM / ID 芯片
- 扫描安全元件地址，并对已支持的器件家族做只读识别
- 通过 `scripts/probe-board.ps1` 生成 preset 草案

### 10. 桌面工具

浏览器 WebUI：

- 只连接 Debug HID
- 适合快速发 `INIT`、`PING`、`getInfo`、`makeCredential`、`getAssertion`

WinUI 管理器：

- 在 Windows 上按 `概览 / 设备 / 凭据 / 安全 / 维护 / 活动` 组织信息架构
- 布局节奏参考 Android-Cam-Bridge 的桌面管理器
- 明确展示当前管理能力缺口，而不是继续把原始协议测试放在 UI 中心

Rust 管理器壳层：

- 继续承担 Linux 侧 `Rust + egui/eframe` 桌面界面
- 仍然是 Debug HID 维护与 bring-up 的实用工作台
- 继续覆盖诊断、凭据摘要、注册 / 断言测试等能力

浏览器 WebUI 和 Rust 壳层仍然属于开发 / 维护前端，WinUI 管理器则是面向产品形态的桌面壳层。但它们都不是通用 FIDO 客户端。
