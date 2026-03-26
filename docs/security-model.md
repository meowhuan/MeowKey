# 当前安全模型

这份文档只描述“当前代码已经实现出来的安全行为”，不替代 FIDO 规范，也不把未来规划写成既成事实。

## 1. 构建档位与信任边界

- `debug`
  默认暴露 FIDO HID + Debug HID，USB 产品名会标成 `MeowKey DEV UNSAFE RP2350`。
- `hardened`
  关闭 Debug HID，只保留标准 FIDO HID，但仍不是量产级安全设计。
- `probe`
  单独的板卡探测镜像，不承载长期认证器逻辑。

当前最重要的边界是：

- `debug` 构建不能被当作可信认证器边界。
- `hardened` 只是去掉开发后门后的较小攻击面基线，不代表敏感材料已经得到硬件保护。
- 当前 RP2350 也不被当作安全元件；设备唯一材料 wrapping 只是 MCU 侧保护，不是不可导出密钥边界。

## 2. 设备当前暴露的接口

- 标准 FIDO HID 始终存在。
- `debug` 构建额外暴露 Debug HID，并支持 `CTAPHID_DIAG (0x40)`。
- Debug HID 的 `1/2/5/6` 在开启 Debug HID 时始终可用。
- Debug HID 的 `3/4` 只有在 `MEOWKEY_ENABLE_DANGEROUS_DEBUG_COMMANDS=ON` 时才会编译进去；默认 debug 构建会开启它。

这意味着：

- 关闭 Debug HID 才能真正移除这条开发面。
- 只关闭危险调试命令并不能阻止主机读取日志或改写 UP 配置。

## 3. CTAP2 / WebAuthn 的当前安全语义

- `getInfo` 当前上报 `versions=["FIDO_2_0"]`。
- `getInfo` 当前上报 `extensions=["hmac-secret"]`。
- `getInfo` 当前上报 `options.up=true`、`options.uv=false`、`options.makeCredUvNotRqd=true`。
- `getInfo` 里的 `options.clientPin` 会跟随“是否已设置 PIN”变化。
- attestation 当前固定返回 `"none"`。
- 新创建的凭据当前一律按 discoverable credential 保存。

当前代码的含义是：

- 设备支持标准 CTAP2 基本注册/断言闭环。
- 内建 UV 能力并未声明为 `uv=true`；当前 UV 语义来自 `clientPIN` 路径，而不是更强的本地验证硬件。

## 4. User Presence 当前如何工作

- 固件会把一份 UP baseline 持久化到 Flash。
- 另外还支持一份仅当前上电有效的会话态 UP 覆盖，不落盘。
- 当前配置项包括 `source`、`gpioPin`、`gpioActiveLow`、`tapCount`、`gestureWindowMs`、`requestTimeoutMs`。
- 默认编译配置是 `source=bootsel`、`tapCount=2`、`gestureWindowMs=750`、`requestTimeoutMs=8000`。
- `source=none` 时，运行时 UP 直接放行。
- `source=bootsel` 时，当前板卡默认走双击 `BOOTSEL`。
- `source=gpio` 时，走单独 GPIO 输入并按配置解释高低电平。

当前命令路径上的 UP 规则是：

- `makeCredential` 每次都会走一次 UP 检查。
- `getAssertion` 每次都会走一次 UP 检查，除非命中最近一次成功断言留下的一次性复用窗口。
- 这个复用窗口只用于 `getAssertion`。
- 这个复用窗口只允许复用一次。
- 这个复用窗口只有在前一次 `getAssertion` 成功后才会被设置。
- 当前窗口时长是 `1200ms`。
- 当前匹配键已经改成解析后的请求指纹，所以语义一致但 CBOR 字节序不同的重试也会命中。
- `getNextAssertion` 依赖前一次 `getAssertion` 留下的 pending state，不会再额外要求一次新的 UP。

安全上的直接含义是：

- `source=none` 会让后续注册/断言失去物理确认。
- debug 构建上的主机可通过 Debug HID `DIAG 6` 持久化改写 baseline，也可通过 `DIAG 7` 只改当前上电会话。
- 当前 `hardened` 启动不会信任 legacy 或 Debug HID 写入的持久化 UP 配置；一旦检测到这类状态，会回退到编译默认值并重新落盘。

## 5. Client PIN / UV 当前如何工作

- 当前只支持 `pinUvAuthProtocol=1`。
- 当前支持的 `clientPIN` 子命令只有：
  `getRetries`、`getKeyAgreement`、`setPin`、`changePin`、`getPinToken`。
- `getPinTokenWithPermissions` 当前明确拒绝，不实现权限范围模型。
- 运行时 `pinUvAuthToken` 是 32 字节，仅存在于当前上电会话。
- token 生命周期当前是 `30000ms`。
- 每次成功执行 `getPinToken` 都会重新签发新 token。
- `setPin` / `changePin` 成功后会清掉旧 token。
- 重启后旧 token 也会自然失效。

PIN 状态当前是这样保存和校验的：

- Flash 中持久化的是 PIN 是否已设置、剩余重试次数、以及 SHA-256 前 16 字节形式的 `pinHash`。
- PIN 校验失败会消耗一次重试并立刻持久化。
- 成功拿到 `getPinToken` 后，重试次数会重置到默认值 `8`。
- 重试数归零后会返回 `PIN_BLOCKED`，且这个状态跨重启保留。
- HMAC / `pinHash` 比较路径当前使用常量时间风格比较。

当前 UV 语义还要注意两点：

- 如果请求要求 `uv=true`，代码实际依赖的是合法 `pinUvAuthParam`，不是独立本地验证硬件。
- 即便请求本身不强制 `uv=true`，只要提供了合法 `pinUvAuthParam`，当前 `makeCredential` / `getAssertion` 仍会把本次操作视为 `user_verified`。

这会影响两个输出：

- 断言 / 注册返回的 `authData` 会带上 UV bit。
- `hmac-secret` 会在 `cred_random_with_uv` 和 `cred_random_without_uv` 两套种子之间切换。

## 6. `hmac-secret` 的当前边界

- `makeCredential` 如果请求了 `hmac-secret`，会在 attested authData 里写扩展声明。
- `getAssertion` 支持 `hmac-secret` 解密与回包。
- `hmac-secret` 当前仍复用 `clientPIN` 的 ECDH shared secret 能力。
- 如果本次断言带有合法 UV，则使用 `cred_random_with_uv`。
- 否则使用 `cred_random_without_uv`。

这保证了：

- 同一凭据在 UV / 非 UV 两种状态下不会产出同一组 `hmac-secret` 派生结果。

## 7. 凭据与状态当前如何落盘

- 主存储格式当前是 `version 6`。
- Flash 尾部保留区当前拆成“主凭据区 + signCount journal”两部分。
- 主凭据区使用 A/B 槽提交，并带 `generation` / `payload_crc32` / `header_crc32`。
- journal header 会绑定当前主区的 `store_version`、`store_generation`、`store_payload_crc32`。
- `signCount` 更新优先写 journal；journal 满了以后会把当前视图压实进新的主槽。

当前持久化到普通 Flash 的内容包括：

- 私钥
- `credentialId`
- RP ID
- 用户 ID / 用户名 / 显示名
- `credRandom` 的 UV / 非 UV 两份种子
- `signCount`
- PIN 哈希
- PIN 重试计数
- UP 配置

当前存储层已经做到的事：

- 主区掉电一致性比早期整区重写更强。
- `signCount` 不再要求每次断言都重写整个主区。
- journal 不会跨错的主区代际直接复用。
- 主区 payload 当前已经改成用设备唯一材料派生出的密钥做 at-rest wrapping，并附带 keyed tag 校验。

当前仍然没有做到的事：

- 磨损均衡
- 安全元件 / OTP 绑定的机密保护
- 真正不可导出的密钥边界

另外还有两个容易误解的点：

- Debug HID 的“清空凭据存储”只会清掉 credential slots，不会清掉 PIN 状态和 UP 配置。
- `DIAG 6` 写入的 UP baseline 会在 debug 重刷之间保留；`DIAG 7` 的会话态覆盖只在当前上电有效。
- 当前 `hardened` 启动会拒绝继承 legacy 或 Debug HID 写入的持久化 UP 状态，并恢复到编译默认值。

## 8. Debug HID 当前到底能做什么

- `DIAG 1` 读取诊断日志快照。
- `DIAG 2` 清空诊断日志。
- `DIAG 5` 读取当前生效的 UP 配置摘要，并附带持久化 baseline 摘要。
- `DIAG 6` 写入新的持久化 UP 配置。
- `DIAG 7` 写入新的会话态 UP 配置，仅影响当前上电。
- `DIAG 8` 清除会话态 UP 覆盖，恢复到当前持久化 baseline。

如果编译了危险调试命令，还会额外支持：

- `DIAG 3` 清空凭据槽位。
- `DIAG 4` 列出凭据摘要。

`DIAG 4` 当前不会导出私钥，但会泄露：

- RP ID
- 用户名
- signCount
- credential ID 前缀
- 当前 store 摘要

因此在 debug 构建上：

- 主机不仅能观察调试状态，还能改变后续认证策略。
- 把 `source` 写成 `none` 后，设备会在当前会话里立即跳过物理确认。
- 如果通过 `DIAG 6` 改写持久化 baseline，这个状态会在后续 debug 重刷之间保留，直到配置被改回去。
- 当前 `hardened` 启动会拒绝继承 legacy / Debug HID 持久化的 UP 状态，所以风险重点不再是“debug 可永久降级 hardened”，而是“debug 会污染 debug 自身的后续状态，并破坏当下会话的物理确认语义”。

## 9. 当前最主要的剩余安全问题

- 私钥、`credRandom`、PIN 哈希虽然已经不再直接明文落盘，但当前 wrapping 仍依赖主 MCU 可访问的设备唯一材料，不等于安全元件或真正不可导出的密钥边界。
- 当前 UV 只是 PIN 语义，没有更强的本地受信输入。
- 当前没有权限范围 token，也没有基于 RP 的细粒度授权。
- 仓库已经支持可选的 secure boot / OTP / anti-rollback 构建链路，但默认不替用户烧录 OTP，也还没有量产级 provisioning / 恢复流程。
- `webauthn.c` 里的部分敏感临时缓冲区仍未系统清零，特别是 `hmac-secret` 和签名相关的中间数据。
- debug 构建上的 Debug HID 仍然足以破坏“未来每次操作都要物理确认”的假设。

## 10. 结论

当前代码已经具备：

- 可用的 FIDO/CTAP2 最小闭环
- 可配置的 UP
- 基础的 Client PIN
- 初版事务化存储

但它仍然不是量产安全认证器。更准确的描述是：

- `debug` 构建是协议联调设备。
- `hardened` 构建是去掉开发后门后的较小攻击面基线。
- 真正的不可导出私钥边界、量产恢复链路和硬件级机密保护都还没有完成。
