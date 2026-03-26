# Security Model

## English

This document describes what the current code actually does. It does not treat planned features as already finished.

### 1. Build Tiers and Trust Boundaries

- `debug`: FIDO HID plus Debug HID
- `hardened`: standard FIDO HID only
- `probe`: separate board-discovery image

The important trust statement is simple:

- `debug` is not a trustworthy authenticator boundary
- `hardened` removes the development backdoor surface, but it still does not create hardware-backed secret boundaries
- RP2350 is not treated as a secure element

### 2. Exposed Interfaces

Always present:

- standard FIDO HID

Debug-only:

- Debug HID
- `CTAPHID_DIAG (0x40)`

`DIAG 1/2/5/6/7/8` exist whenever Debug HID is enabled. `DIAG 3/4` additionally depend on `MEOWKEY_ENABLE_DANGEROUS_DEBUG_COMMANDS`.

### 3. CTAP2 / WebAuthn Semantics

`getInfo` currently reports:

- `versions=["FIDO_2_0"]`
- `extensions=["hmac-secret"]`
- `options.up=true`
- `options.uv=false`
- `options.makeCredUvNotRqd=true`
- `options.clientPin` tracking actual PIN configuration state

Attestation is fixed to `"none"`.

### 4. User Presence

Current UP configuration supports:

- `source`
- `gpioPin`
- `gpioActiveLow`
- `tapCount`
- `gestureWindowMs`
- `requestTimeoutMs`

Default board behavior for `meowkey_rp2350_usb` is:

- `source=bootsel`
- `tapCount=2`
- `gestureWindowMs=750`
- `requestTimeoutMs=8000`

Rules:

- `makeCredential` requires UP
- `getAssertion` requires UP, unless it hits the short one-shot retry reuse window
- `getNextAssertion` reuses pending assertion state and does not ask again

Debug HID can still change persisted or session-only UP state in debug builds.

### 5. Client PIN and UV Semantics

Current `clientPIN` support includes:

- `getRetries`
- `getKeyAgreement`
- `setPin`
- `changePin`
- `getPinToken`

It does not include permission-scoped tokens.

Current PIN behavior:

- only `pinUvAuthProtocol=1`
- runtime token is 32 bytes
- token is session-only
- token expires after `30000ms`
- successful `getPinToken` rotates it
- `setPin` / `changePin` clears prior runtime token state

ECDH peer public keys are validated before shared-secret derivation.

### 6. `hmac-secret`

Current behavior:

- `makeCredential` can declare `hmac-secret`
- `getAssertion` can decrypt salts and return encrypted secrets
- UV and non-UV requests derive from different `credRandom` seeds

This means one credential does not produce the same `hmac-secret` material for UV and non-UV flows.

### 7. Credential and State Persistence

The current on-flash store includes:

- private keys
- credential IDs
- RP IDs
- user metadata
- `credRandom`
- sign counts
- PIN hash
- PIN retries
- UP config

Current format is `version 6`.

What the store already does:

- transactional A/B main store
- generation and CRC checks
- sign-count journal bound to the active store image
- at-rest wrapping with device-bound material

What it still does not do:

- wear leveling
- hardware-backed non-exportable keys
- secure-element-style isolation from trusted-but-buggy firmware

### 8. Debug HID Impact

When Debug HID is enabled, the host can:

- inspect diagnostics
- read current UP state
- change persisted UP state
- change session-only UP state
- optionally list credentials
- optionally clear credentials

That is why the debug path must be treated as a developer-only trust mode.

### 9. Sensitive Intermediate Handling

Current code now scrubs several security-relevant intermediates after use, especially in:

- shared-secret derivation paths
- assertion signing paths
- `hmac-secret` handling
- make-credential response construction

That improves stack hygiene, but it is still not the same thing as a repository-wide formal sensitive-data lifecycle policy.

### 10. Practical Conclusion

Today the codebase provides:

- a working CTAP2 registration and assertion loop
- configurable UP
- basic Client PIN
- transactional flash persistence

It does not yet provide:

- a finished hardware secret boundary
- full permission-scoped management
- a production-style provisioning and recovery system

## 中文

这份文档只描述当前代码已经实现出来的安全行为，不把未来规划写成既成事实。

### 1. 构建档位与信任边界

- `debug`：同时暴露 FIDO HID 和 Debug HID
- `hardened`：只保留标准 FIDO HID
- `probe`：独立的板卡探测镜像

最重要的判断很简单：

- `debug` 不能被当作可信认证器边界
- `hardened` 只是去掉开发后门后的较小攻击面基线，但仍然没有形成硬件支持的机密边界
- RP2350 在当前项目里不被当作安全元件

### 2. 对外暴露的接口

始终存在：

- 标准 FIDO HID

仅在 debug 下存在：

- Debug HID
- `CTAPHID_DIAG (0x40)`

只要 Debug HID 开着，`DIAG 1/2/5/6/7/8` 就会存在；`DIAG 3/4` 还需要 `MEOWKEY_ENABLE_DANGEROUS_DEBUG_COMMANDS`。

### 3. CTAP2 / WebAuthn 语义

`getInfo` 当前上报：

- `versions=["FIDO_2_0"]`
- `extensions=["hmac-secret"]`
- `options.up=true`
- `options.uv=false`
- `options.makeCredUvNotRqd=true`
- `options.clientPin` 会跟随实际 PIN 状态变化

Attestation 仍然固定为 `"none"`。

### 4. User Presence

当前 UP 配置项包括：

- `source`
- `gpioPin`
- `gpioActiveLow`
- `tapCount`
- `gestureWindowMs`
- `requestTimeoutMs`

`meowkey_rp2350_usb` 的默认行为是：

- `source=bootsel`
- `tapCount=2`
- `gestureWindowMs=750`
- `requestTimeoutMs=8000`

规则上：

- `makeCredential` 需要 UP
- `getAssertion` 需要 UP，除非命中很短的一次性重试复用窗口
- `getNextAssertion` 复用 pending assertion 状态，不再重复要求新的 UP

在 debug 构建里，Debug HID 仍然可以改写持久化或会话态的 UP 状态。

### 5. Client PIN 与 UV 语义

当前 `clientPIN` 支持：

- `getRetries`
- `getKeyAgreement`
- `setPin`
- `changePin`
- `getPinToken`

但还不支持权限范围 token。

当前 PIN 行为包括：

- 只支持 `pinUvAuthProtocol=1`
- 运行时 token 长度为 32 字节
- token 只存在于当前会话
- token 在 `30000ms` 后过期
- 每次成功执行 `getPinToken` 都会轮换
- `setPin` / `changePin` 会清掉旧的运行时 token 状态

ECDH 对端公钥在共享密钥推导前会先做显式校验。

### 6. `hmac-secret`

当前行为是：

- `makeCredential` 可以声明 `hmac-secret`
- `getAssertion` 可以解密 salt 并返回重新加密后的 secret
- UV 和非 UV 路径会使用不同的 `credRandom` 种子

这意味着同一凭据不会在 UV 和非 UV 流程里产出同一份 `hmac-secret` 派生结果。

### 7. 凭据与状态持久化

当前会落盘的内容包括：

- 私钥
- credential ID
- RP ID
- 用户元数据
- `credRandom`
- sign count
- PIN 哈希
- PIN 重试计数
- UP 配置

当前格式版本为 `version 6`。

存储层已经做到：

- 主区 A/B 事务提交
- generation 与 CRC 校验
- 与主区绑定的 sign-count journal
- 基于设备唯一材料的 at-rest wrapping

还没有做到：

- 磨损均衡
- 硬件支持的不可导出密钥
- 像安全元件那样把秘密与“能跑在 MCU 上的固件”隔离开

### 8. Debug HID 的影响

只要 Debug HID 开着，主机就能：

- 看诊断日志
- 读当前 UP 状态
- 改持久化 UP 状态
- 改会话态 UP 状态
- 在危险命令开启时列出凭据
- 在危险命令开启时清空凭据

这也是为什么 debug 路径必须被当作开发者模式，而不是普通认证器模式。

### 9. 敏感中间态处理

当前代码已经会在几个关键路径上清理安全相关中间缓冲区，尤其包括：

- 共享密钥推导路径
- 断言签名路径
- `hmac-secret` 路径
- make-credential 响应构造路径

这能改善栈上数据卫生，但仍然不等于仓库已经建立了统一、形式化的敏感数据生命周期策略。

### 10. 实际结论

今天的代码已经具备：

- 可工作的 CTAP2 注册 / 断言闭环
- 可配置的 UP
- 基础版 Client PIN
- 事务化 Flash 持久化

但它还不具备：

- 完整的硬件机密边界
- 权限范围完整的管理模型
- 类似量产系统那样完整的 provisioning 与恢复流程
