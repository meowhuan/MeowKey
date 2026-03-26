这个目录用于放置本地安全启动签名密钥，但默认不应提交私钥。

当前约定的文件名：

- `meowkey-secureboot.pem`

说明：

- 默认开发构建和当前 release workflow 仍然会生成未烧 OTP 的开发态镜像。
- 只有在你明确决定启用 RP2350 secure boot 时，才应该在本地放入签名私钥。
- 如果同时启用 anti-rollback，默认会使用项目预留的 page 3 稀疏 OTP 行（`0xc0` 到 `0xe1`，步长 `3`）写入 thermometer counter；默认 12 行可提供 `288` 个版本槽，需要更大空间时请显式覆盖这些行。
- 启用 secure boot 构建时会额外生成 `meowkey.otp.json`，供你在人工确认后自行烧录 OTP 公钥哈希与相关材料。
- `keys/*.pem` 和 `keys/*.json` 已被 `.gitignore` 忽略。

不要把这个目录误解成“仓库已经默认启用 secure boot”。它只是预留入口。
