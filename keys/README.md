# Secure Boot Keys

## English

This directory is reserved for local secure-boot signing material. Private keys should not be committed.

Current default filename:

- `meowkey-secureboot.pem`

Notes:

- default development builds and default release runs still work without OTP programming
- only place a signing key here when you have decided to use the RP2350 signed-boot flow
- enabling signed boot produces `meowkey.otp.json`, which you still review and program separately
- anti-rollback uses the project-selected sparse page-3 OTP rows by default unless you override them
- `keys/*.pem` and `keys/*.json` are ignored by `.gitignore`

This directory is an entry point for local signing, not a claim that secure boot is already fully provisioned by default.

## 中文

这个目录预留给本地 secure boot 签名材料使用，私钥不应该被提交进仓库。

当前默认文件名：

- `meowkey-secureboot.pem`

需要注意：

- 默认开发构建和默认 release 流程都可以在不烧录 OTP 的情况下工作
- 只有在你明确决定走 RP2350 signed-boot 链路时，才应该把签名私钥放进这里
- 启用 signed boot 后会生成 `meowkey.otp.json`，但你仍然需要自行审核并单独烧录
- anti-rollback 默认使用项目预留的 page 3 稀疏 OTP 行，除非你显式覆盖
- `.gitignore` 已经忽略 `keys/*.pem` 和 `keys/*.json`

这个目录只是本地签名入口，不代表仓库默认已经完成 secure boot provisioning。
