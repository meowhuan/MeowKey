# Known Gaps

## English

This file tracks the gaps that are visible in the current codebase. It is not a generic future roadmap.

### 1. Protocol Gaps

Credential management is still incomplete. The firmware can store credentials, page structured summaries through the formal management channel, list legacy summaries through Debug HID, and clear all slots through Debug HID, but it still does not expose a safe in-device management flow for:

- deleting one credential at a time
- promoting credential-changing actions into the formal management channel
- protected management commands with permission scope

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
- the WebUI still depends on Debug HID, while the Rust manager shell can now read the formal management channel but still keeps Debug-HID-only maintenance paths
- the Windows WinUI manager shell can read the formal management channel, but per-credential management and scoped authorization are still incomplete

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

凭据管理仍然不完整。固件已经能存储凭据、通过正式管理通道分页读取结构化摘要、通过 Debug HID 列出旧摘要、并通过 Debug HID 清空所有槽位，但仍然没有安全的设备内管理接口来完成：

- 按单条凭据删除
- 把会改动凭据状态的操作迁移进正式管理通道
- 带权限范围控制的管理命令

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
- WebUI 仍然依赖 Debug HID，而 Rust 管理器壳层已经能读取正式管理通道，但维护路径里仍然保留 Debug-HID-only 能力
- Windows WinUI 管理器壳层已经能读取正式管理通道，但单条凭据管理和权限范围授权仍未补齐

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
