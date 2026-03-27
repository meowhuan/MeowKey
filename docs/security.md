# Security Guide

## English

### 1. Current Baseline

MeowKey is best understood in three modes:

- `debug`: for bring-up and protocol inspection; not a trustworthy security boundary
- `hardened`: smaller attack surface for personal or community distribution
- `probe`: board-discovery image, not a long-term authenticator firmware

This project is being documented for personal use and open-source redistribution. That means the guidance here focuses on practical host trust, board trust, and firmware trust, not on factory assumptions.

### 2. Hardening Already in Place

Current implemented tightening includes:

- Debug HID can be compiled out completely
- dangerous credential-list / credential-clear commands are separately gated
- runtime `pinUvAuthToken` is session-only and expires after `30000ms`
- `getPinTokenWithPermissions` is explicitly rejected instead of pretending to be partially supported
- PIN-related HMAC and hash comparisons use constant-time style checks
- ECDH peer public keys are validated before shared-secret derivation
- sensitive intermediates on the main `clientPIN`, signing, and `hmac-secret` paths are scrubbed after use
- the main credential store is transactional, and sign counts are journaled separately
- optional RP2350 signed boot, OTP hash material generation, and anti-rollback metadata are available

### 3. Real Boundaries Today

Important facts that should not be hidden:

- if Debug HID is enabled, the host can still inspect logs and modify UP behavior
- hardened startup rejects legacy or Debug HID-derived persisted UP state
- private keys, `credRandom`, and PIN hash are wrapped at rest, but a hostile firmware that already runs on the MCU is still inside the trust boundary
- current UV behavior is Client PIN semantics, not a separate trusted local verifier
- `getAssertion` still allows one short UP reuse window for retry convenience

### 4. Remaining Risks

- there is no secure element and no hardware-backed non-exportable key boundary
- there are no permission-scoped tokens or RP-scoped authorization rules yet
- signed boot and anti-rollback remain opt-in and do not by themselves define provisioning or recovery
- storage still has no wear leveling
- browser tooling still depends on Debug HID, and the Rust shell still keeps Debug-HID-only maintenance paths, so the debug path remains intentionally more powerful than the hardened path

### 5. Recommended Use

For local development:

- assume the host is trusted
- treat the device as a protocol and firmware test target
- expect Debug HID to be security-relevant

For personal or community redistribution:

1. build the hardened variant
2. keep Debug HID disabled
3. decide deliberately whether signed boot and OTP programming fit your threat model
4. test UP behavior, upgrades, and recovery on real hardware

### 6. What You Should Not Assume

The following statements are still false:

- "it registers and asserts, therefore it is already a finished secure authenticator"
- "it has PIN support, therefore UV is complete"
- "it supports signed boot inputs, therefore provisioning is complete"
- "it runs on RP2350, therefore it has a secure-element boundary"

## 中文

### 1. 当前安全基线

MeowKey 目前最好按三种模式来理解：

- `debug`：用于 bring-up 和协议联调，不能当作可信安全边界
- `hardened`：更适合个人分发和社区分发的较小攻击面基线
- `probe`：只用于底板识别，不是长期认证器固件

这个项目现在以个人使用和开源分发为中心来写文档，所以这里讨论的重点是主机信任边界、底板信任边界和固件信任边界，而不是假设已经存在工厂级控制流程。

### 2. 已经落实的收紧项

当前已经落实的安全收紧包括：

- Debug HID 可以在编译期彻底关闭
- 危险的列凭据 / 清凭据命令已经和普通调试能力分开门控
- 运行时 `pinUvAuthToken` 只存在于当前会话，并在 `30000ms` 后过期
- `getPinTokenWithPermissions` 会被明确拒绝，而不是伪装成“部分支持”
- PIN 相关 HMAC 与哈希比较路径使用常量时间风格比较
- ECDH 对端公钥在共享密钥推导前会做显式校验
- 主要 `clientPIN`、签名和 `hmac-secret` 路径上的敏感中间缓冲区会在使用后清零
- 主凭据存储区已经事务化，`signCount` 也走独立 journal
- 可选的 RP2350 signed boot、OTP 哈希材料输出和 anti-rollback metadata 已经具备

### 3. 当前真实边界

几个必须正视的事实：

- 只要 Debug HID 还开着，主机就仍然能读取日志并改写 UP 行为
- `hardened` 启动会拒绝继承 legacy 或 Debug HID 写入的持久化 UP 状态
- 私钥、`credRandom` 和 PIN 哈希虽然已经做了 at-rest wrapping，但如果恶意固件已经能在 MCU 上运行，它仍然处在信任边界内部
- 当前 UV 语义本质上还是 Client PIN，不是独立的本地可信验证器
- `getAssertion` 仍然保留一个很短的一次性 UP 复用窗口，用于重试可用性

### 4. 仍然存在的风险

- 目前没有安全元件，也没有硬件支持的不可导出私钥边界
- 还没有权限范围 token，也没有 RP 绑定的细粒度授权规则
- signed boot 和 anti-rollback 仍然是可选能力，本身不等于 provisioning / 恢复流程已经完整
- 存储层仍然没有磨损均衡
- 浏览器工具仍然依赖 Debug HID，而 Rust 壳层也还保留 Debug-HID-only 的维护路径，因此 debug 路径仍然会比 hardened 路径拥有更多能力

### 5. 使用建议

本地开发阶段：

- 默认把主机视为可信
- 把设备当作协议和固件测试目标
- 明确认识到 Debug HID 本身就是安全相关能力

个人使用或社区分发阶段：

1. 优先构建 `hardened` 变体
2. 确保 Debug HID 关闭
3. 结合自己的威胁模型，再决定是否启用 signed boot 与 OTP 烧录
4. 在真实硬件上验证 UP、升级和恢复路径

### 6. 不应该误判的事情

下面这些判断现在都还不成立：

- “能注册和断言，所以已经是完整安全认证器”
- “有 PIN，所以 UV 已经完成”
- “支持 signed boot 入口，所以 provisioning 已经完整”
- “运行在 RP2350 上，所以已经具备安全元件边界”
