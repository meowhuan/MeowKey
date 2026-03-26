# 安全说明

## 1. 当前安全基线

MeowKey 仍然是开发期认证器，安全边界应该分三档理解：

- `debug`
  面向开发联调，默认不安全。
- `hardened`
  关闭 Debug HID，是更接近分发的较小攻击面基线，但仍不是量产设计。
- `probe`
  板卡探测镜像，不应被当作长期认证器固件。

如果你需要看代码级别的真实行为，而不是高层摘要，请直接看 [docs/security-model.md](docs/security-model.md)。

## 2. 当前已经落实的收紧项

- Debug HID 可以通过 `-DMEOWKEY_ENABLE_DEBUG_HID=OFF` 或 `-DisableDebugHid` 彻底关闭。
- 危险调试命令 `DIAG 3/4` 现在和 Debug HID 分开编译控制，不再和所有构建强绑定。
- `pinUvAuthToken` 只存在于当前上电会话，每次 `getPinToken` 重签，生命周期当前为 `30000ms`。
- `getPinTokenWithPermissions` 当前明确拒绝，不伪装成“部分支持”。
- PIN 相关 HMAC / 哈希比较路径已经使用常量时间风格比较。
- 主存储区已经是 `version 6` 的 A/B 事务提交，`signCount` 也已经拆到独立 journal。

## 3. 当前最需要注意的真实边界

- 只要 Debug HID 还开着，主机就仍然能读取日志，并且能通过 `DIAG 6` 持久化改写 UP 配置。
- 这意味着 debug 构建上的主机可以把 `source` 改成 `none`，从而让后续注册/断言跳过物理确认。
- `hardened` 构建虽然去掉了 Debug HID，但私钥、`credRandom`、PIN 哈希仍然明文放在普通 Flash。
- 当前 UV 只是 Client PIN 语义，不是更强的本地受信验证硬件。
- 当前 `getAssertion` 还包含一个一次性 `1200ms` UP 复用窗口，用于同一语义请求的快速重试；这是有边界的可用性折中，不是长期豁免。

## 4. 仍然存在的主要风险

- 私钥、`credRandom`、PIN 哈希仍明文存放在设备 Flash。
- 当前没有权限范围 token，也没有基于 RP 的细粒度授权模型。
- 当前没有量产级 secure boot、OTP 公钥哈希和 rollback policy。
- `webauthn.c` 里的部分敏感中间缓冲区仍未系统化清零。
- 当前 UP / UV 仍然只是开发期可用实现，不是量产级的人机交互与本地验证方案。

## 5. 使用建议

### 5.1 开发联调

可以使用 debug 构建，但应默认认为：

- 主机环境是可信的。
- 设备不是保密边界。
- 调试接口足以改变后续认证策略。

### 5.2 对外分发前

至少做到：

1. 使用 `hardened` 构建，彻底关闭 Debug HID。
2. 重构敏感材料的落盘保护策略。
3. 明确最终的 UP / UV 硬件方案。
4. 定义 secure boot 与 rollback policy。
5. 做真实设备级升级、恢复和掉电测试。

## 6. 不应该误判的事情

以下结论目前都不成立：

- “能注册和断言，所以已经是安全认证器”
- “CI 能过，所以已经适合量产”
- “支持签名镜像入口，所以 secure boot 已经完成”
- “有 PIN，所以已经具备完整 UV”

当前更准确的描述是：MeowKey 已经具备可调试的认证器功能骨架，并开始形成较清晰的安全边界，但距离量产安全设计还有明显距离。
