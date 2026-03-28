# Known Gaps

## English

This file tracks the gaps that are visible in the current codebase. It is not a generic future roadmap.

### 1. Protocol Gaps

Credential management is still incomplete, but no longer read-only on the formal channel. Firmware can now:

- page structured summaries through the formal management channel
- delete one credential at a time through the formal management channel
- update user-presence config through the formal management channel
- gate formal write actions behind local UP-confirmed, short-lived permission tokens

Still missing on that path:

- PIN/RP-scoped permission tokens
- richer policy controls and command-specific authorization semantics
- migration of every remaining destructive maintenance action away from Debug HID

`clientPIN` also remains incomplete. Current support covers:

- `getRetries`
- `getKeyAgreement`
- `setPin`
- `changePin`
- `getPinToken`

Still missing:

- `getPinTokenWithPermissions`
- RP-scoped permission tokens
- finer-grained authorization checks

CTAPHID coverage is also partial:

- `CANCEL`
- `LOCK`
- `WINK`
- stricter busy / arbitration rules

Attestation is still fixed to `none`.

### 2. Security Gaps

The storage layer is transactional now, but it is not a finished secure-storage design. What exists today:

- A/B slots
- generation and CRC tracking
- sign-count journal binding to the active store image

What still does not exist:

- wear leveling
- per-record updates
- hardware-backed non-exportable key storage

Private keys, `credRandom`, and PIN material are wrapped with device-bound material, but that is still MCU-side protection, not a secure-element boundary.

User presence is still basic:

- current default path is double-tap `BOOTSEL`
- GPIO-based UP is configurable
- a stronger trusted local confirmation path is still absent

Signed boot and anti-rollback are available as optional building blocks, but the repository still does not define a full provisioning, update, or recovery process.

Sensitive buffer scrubbing has improved on the main `clientPIN`, signing, and `hmac-secret` paths, but there is still no single project-wide abstraction for every sensitive transient buffer.

### 3. Tooling and Operational Gaps

Desktop tooling is still split across formal and maintenance transports, which means:

- development and hardened builds are intentionally split
- the WebUI still depends on Debug HID
- Rust and WinUI manager shells now both support formal-channel reads and formal write actions, but CTAP bring-up / diagnostics / debug maintenance paths still depend on Debug HID
- the manager authorization model is still not `clientPIN` permission-token complete

CI can currently validate:

- firmware builds
- probe firmware builds
- desktop compilation checks
- release packaging

CI still cannot validate:

- real hardware plug / unplug behavior
- browser compatibility across real hosts
- formal FIDO conformance

### 4. Practical Priorities

The next useful priorities are:

1. improve storage behavior and update granularity
2. add permission-scoped credential management
3. connect UP / GPIO configuration to real UI flows
4. stabilize the secure-boot and rollback policy after update / recovery paths are clearer

## 中文

这个文件只记录当前代码已经暴露出来的真实缺口，不写空泛 roadmap。

### 1. 协议层缺口

凭据管理仍然不完整，但已经不再是“正式通道只读”。固件现在已经可以：

- 通过正式管理通道分页读取结构化摘要
- 通过正式管理通道按单条凭据删除
- 通过正式管理通道写入 user presence 配置
- 在正式写操作前先走本地在场确认，再发放短时权限 token

仍然缺少的部分包括：

- 与 `clientPIN` / RP 范围联动的权限 token
- 更细粒度、按命令语义拆分的授权策略
- 把剩余的破坏性维护动作继续从 Debug HID 迁出

`clientPIN` 也还没有覆盖完整权限模型。当前支持的是：

- `getRetries`
- `getKeyAgreement`
- `setPin`
- `changePin`
- `getPinToken`

仍然缺少：

- `getPinTokenWithPermissions`
- 与 RP 绑定的权限 token
- 更细粒度的授权校验

CTAPHID 控制命令也还不完整：

- `CANCEL`
- `LOCK`
- `WINK`
- 更严格的 busy / arbitration 规则

Attestation 目前仍固定为 `none`。

### 2. 安全面缺口

存储层虽然已经事务化，但还不是一个完整的 secure storage 方案。当前已经有：

- A/B 槽
- generation 和 CRC 校验
- 与主存储绑定的 sign-count journal

仍然没有：

- 磨损均衡
- per-record 更新
- 硬件支持的不可导出私钥边界

私钥、`credRandom` 和 PIN 材料现在已经做了设备绑定 wrapping，但这仍然只是 MCU 侧保护，不是安全元件级边界。

用户在场链路也仍然比较基础：

- 当前默认路径是双击 `BOOTSEL`
- GPIO 型 UP 已经可配置
- 更强的本地可信确认路径还没有落地

Signed boot 和 anti-rollback 已经有可选构建块，但仓库仍然没有完整的 provisioning、升级和恢复流程定义。

敏感缓冲区清理已经在主要的 `clientPIN`、签名和 `hmac-secret` 路径上有所改善，但项目里仍然没有统一覆盖所有敏感临时缓冲区的抽象。

### 3. 工具链与运维缺口

桌面工具目前仍然分裂在正式通道和维护通道两类传输之上，这意味着：

- 开发构建和硬化构建会天然分叉
- WebUI 仍然依赖 Debug HID
- Rust 与 WinUI 管理器壳层现在都支持正式通道读写，但 CTAP 联调 / 诊断 / 调试维护路径仍然依赖 Debug HID
- 管理授权模型还没有达到 `clientPIN` permission-token 的完整语义

CI 目前能做的是：

- 固件构建
- probe 固件构建
- 桌面工具编译检查
- 发布打包

CI 还做不到：

- 真机插拔与板上行为验证
- 真实主机环境下的浏览器兼容性验证
- 正式 FIDO 一致性测试

### 4. 当前更实用的优先级

目前更值得继续推进的顺序是：

1. 继续改进存储层和增量更新策略
2. 实现带权限范围的 credential management
3. 把 UP / GPIO 配置真正接入 UI
4. 在升级 / 恢复路径更清晰后，再收紧 secure boot 与 rollback policy
