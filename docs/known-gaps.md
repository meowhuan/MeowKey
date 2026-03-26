# 固件逻辑遗漏与后续优先级

这个文件只记录“现有代码已经暴露出来的真实缺口”，不写空泛 roadmap。

## 1. 协议层缺口

### 1.1 `getAssertion` 只返回第一条凭据

当前 `getAssertion` 在没有 `allowList` 时，会按 RP ID 找到第一条匹配凭据后直接返回。

还没有：

- `numberOfCredentials`
- `getNextAssertion`
- 同 RP 多凭据选择策略

影响：

- 一旦同 RP 下存在多条 discoverable credential，行为会和完整 CTAP2 认证器不一致。

### 1.2 凭据管理命令仍缺失

固件内部已经能：

- 存储凭据
- 列出凭据摘要（仅 Debug HID）
- 清空全部凭据（仅 Debug HID）

但还没有标准化的设备内管理命令：

- 按凭据删除
- 枚举全部凭据并带结构化元数据
- 受权限保护的管理接口

### 1.3 `clientPIN` 仅覆盖最小闭环

当前支持：

- `getRetries`
- `getKeyAgreement`
- `setPin`
- `getPinToken`

仍缺：

- `changePin`
- `getPinTokenWithPermissions`
- RP 绑定的权限令牌
- 令牌生命周期和权限校验

### 1.4 CTAPHID 控制命令未完整

未真正实现：

- `CANCEL`
- `LOCK`
- `WINK`
- 更严格的 busy / channel arbitration

### 1.5 Attestation 仍固定为 `none`

这适合开发期，但不适合需要设备身份链的发行版。

## 2. 安全面缺口

### 2.1 存储层仍然没有完整掉电一致性

第一阶段修复已经把 `signCount` 从主凭据区整区擦写中拆出来，改成独立 journal。

但当前仍然没有：

- copy-on-write
- A/B slot
- per-record update

更准确地说，现在缺的是“完整事务化存储”，而不是“完全没有任何 journal”。

影响：

- 掉电仍可能破坏主凭据区或造成 journal / 主区状态不一致。
- 高频断言风险已经下降，但还没到量产级别。

### 2.2 私钥与 PIN 哈希仍明文落在设备 Flash

这符合当前开发期“先跑通逻辑”的状态，但不等于安全产品已经合格。

### 2.3 没有硬件级用户在场 / 用户验证

当前 UV 只等价于：

- 已设置 PIN
- 提供了正确的 `pinUvAuthParam`

还没有：

- 物理按键确认
- 指纹 / 安全元件
- 独立受信输入链路

### 2.4 安全启动与回滚保护还没进入量产策略

仓库支持可选签名镜像，但默认仍是开发态：

- 未烧写 OTP 公钥哈希
- 未启用回滚底线
- 未建立量产更新流程

## 3. 工具链与运维缺口

### 3.1 调试工具依赖 Debug HID

这对开发友好，但也意味着：

- 生产固件与调试固件天然分叉
- WebUI / Rust 管理器当前不能作为正式终端管理器使用

### 3.2 自动化仍缺板上实机测试

当前新增的 CI 能做的是：

- 固件构建
- `probe` 固件构建
- 桌面工具编译检查
- 发布打包

仍做不到：

- 真机插拔测试
- 浏览器兼容性测试
- FIDO conformance

## 4. 当前优先级建议

1. 先把存储层改成更安全的增量更新或双写策略。
2. 再补 `getNextAssertion` 与多凭据流程。
3. 然后实现受权限保护的 credential management。
4. 等协议和升级流程稳定后，再冻结 secure boot / rollback policy。
