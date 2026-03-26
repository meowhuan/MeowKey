# Security Policy

MeowKey 目前仍然是开发期认证器固件。

## Supported Configurations

- `debug` build: unsupported for production use
- `hardened` build: best-effort release baseline

受支持的安全边界、已修补项和剩余风险见：

- [docs/security.md](docs/security.md)

## Reporting

在仓库尚未配置专用安全邮箱之前，请优先通过维护者的私下渠道报告问题，不要直接公开：

- 可稳定复现的利用步骤
- 设备密钥材料
- 会导致用户数据不可恢复的 PoC

报告时建议同时提供：

- 固件版本或 tag
- 使用的是 `debug` 还是 `hardened` 构建
- 板卡信息
- 是否启用了 Debug HID
