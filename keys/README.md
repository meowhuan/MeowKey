这个目录用于放置本地安全启动签名密钥，但默认不应提交私钥。

当前约定的文件名：

- `meowkey-secureboot.pem`

说明：

- 默认开发构建和当前 release workflow 仍然会生成未烧 OTP 的开发态镜像。
- 只有在你明确决定启用 RP2350 secure boot 时，才应该在本地放入签名私钥。
- `keys/*.pem` 和 `keys/*.json` 已被 `.gitignore` 忽略。

不要把这个目录误解成“仓库已经默认启用 secure boot”。它只是预留入口。
