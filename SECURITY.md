# Security Policy

## English

MeowKey is an open-source authenticator firmware project for personal devices and community redistribution. Security guidance in this repository is written around real current behavior, not around assumed factory controls.

### Supported Configurations

- `debug`: developer-only; Debug HID is exposed and the host can influence security-relevant behavior
- `hardened`: recommended baseline for personal or community firmware distribution
- `probe`: board-discovery image only; not a long-term authenticator image

Detailed boundaries, implemented hardening, and remaining risks live in:

- [docs/security.md](docs/security.md)
- [docs/security-model.md](docs/security-model.md)

### Reporting

Until the repository has a dedicated security mailbox, please report issues through a private maintainer channel instead of opening them publicly when the report contains:

- reliable reproduction steps
- device key material
- credentials or PIN material
- a proof of concept that can cause irreversible user data loss

Useful report details:

- firmware version or tag
- whether the build was `debug`, `hardened`, or `probe`
- board name or preset
- whether Debug HID was enabled

## 中文

MeowKey 是一个面向个人设备和开源分发场景的认证器固件项目。这个仓库里的安全说明只描述当前代码已经实现出来的真实边界，不把尚未完成的工厂控制、量产恢复链路或硬件信任根写成既成事实。

### 受支持的构建配置

- `debug`
  仅适合开发调试。Debug HID 会暴露额外能力，主机还能影响安全相关行为。
- `hardened`
  当前更适合个人分发和社区分发的基线版本。
- `probe`
  仅用于底板识别和 preset 草案生成，不是长期认证器固件。

更完整的安全边界、已落实的收紧项和剩余风险见：

- [docs/security.md](docs/security.md)
- [docs/security-model.md](docs/security-model.md)

### 报告方式

在仓库还没有专用安全邮箱之前，如果问题报告包含下列内容，请优先通过维护者私下渠道提交，不要直接公开：

- 可稳定复现的利用步骤
- 设备密钥材料
- 凭据或 PIN 材料
- 可能导致用户数据不可恢复的 PoC

提交报告时，建议同时提供：

- 固件版本或 tag
- 使用的是 `debug`、`hardened` 还是 `probe`
- 板卡型号或 preset 名称
- 是否启用了 Debug HID
