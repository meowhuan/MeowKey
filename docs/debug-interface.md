# 调试接口说明

## 1. 为什么会有 Debug HID

标准 FIDO HID 适合给浏览器和操作系统走正式认证流程，但它不适合做固件 bring-up 阶段的开发诊断。

MeowKey 默认开发构建额外暴露一个 Debug HID，用于：

- 拉取最近的固件诊断日志
- 列出当前凭据槽位
- 清空诊断缓存
- 清空凭据存储
- 让浏览器 WebUI 和 Rust 管理器更容易做联调

默认开发构建的 USB 产品名也会刻意做成非常显眼的 `MeowKey DEV UNSAFE RP2350`，避免和更适合对外分发的固件混淆。

## 2. 接口枚举

默认构建：

- `FIDO HID`
  `usagePage=0xF1D0`, `usage=0x01`
- `Debug HID`
  `usagePage=0xFF00`, `usage=0x01`

硬化构建：

- 只保留 `FIDO HID`

## 3. Debug HID 上的命令

Debug HID 走的仍是 CTAPHID 报文格式，但额外支持一个开发专用命令：

- `CTAPHID_DIAG` = `0x40`

其子动作定义在 `src/ctap_hid.c`：

- `1`
  读取当前诊断日志快照。
- `2`
  清空诊断日志。
- `3`
  清空凭据存储。
- `4`
  列出凭据摘要。
- `5`
  读取当前 UP 配置摘要。
- `6`
  写入新的 UP 配置，供后续 UI 重构直接对接。
- `7`
  写入新的会话态 UP 配置，只影响当前上电。
- `8`
  清除会话态 UP 覆盖，恢复到持久化 baseline。

其中 `3/4` 属于危险管理命令，受 `MEOWKEY_ENABLE_DANGEROUS_DEBUG_COMMANDS` 控制；默认 debug 构建会带上它们，但也可以在保留 Debug HID 的同时单独关掉。
`5/6/7/8` 不直接暴露凭据明文，但它们会影响后续认证策略。当前 `meowkey_rp2350_usb` 的默认 `source` 是 `bootsel`；如果把 `source` 写成 `none`，后续注册/断言的 UP 检查会被直接放行。

动作 `5` 返回 UTF-8 JSON，字段格式如下：

```json
{
  "enabled": true,
  "source": "bootsel",
  "gpioPin": -1,
  "gpioActiveLow": true,
  "tapCount": 2,
  "gestureWindowMs": 750,
  "requestTimeoutMs": 8000,
  "sessionOverride": false,
  "persisted": {
    "enabled": true,
    "source": "bootsel",
    "gpioPin": -1,
    "gpioActiveLow": true,
    "tapCount": 2,
    "gestureWindowMs": 750,
    "requestTimeoutMs": 8000
  }
}
```

动作 `6/7` 的请求载荷固定为 9 字节：

- `[0]`
  固定为动作码 `6` 或 `7`
- `[1]`
  `source`，`0=none`，`1=bootsel`，`2=gpio`
- `[2]`
  `gpioPin`，带符号 8 位；`-1` 传 `0xff`
- `[3]`
  `gpioActiveLow`，`0/1`
- `[4]`
  `tapCount`
- `[5..6]`
  `gestureWindowMs`，小端 `uint16`
- `[7..8]`
  `requestTimeoutMs`，小端 `uint16`

`6` 写入的是持久化 baseline，会跨 debug 重刷保留；`7` 只改当前上电会话，重启后自动消失。当前 `hardened` 启动还会拒绝继承 legacy 或 Debug HID 写入的持久化 UP baseline，并回退到编译默认值。后续 UI 既可以复用持久化接口，也可以按场景只走会话态覆盖。

## 4. 风险边界

Debug HID 的风险不在“协议不标准”，而在“能力过强”：

- 可以暴露调试期内部状态
- 可以读取并改写当前 UP 策略，包括持久化 baseline 与当前会话覆盖
- 在开启危险调试命令时，还可以列出凭据摘要和清空凭据槽位

因此：

- Debug 构建只适合开发、联调和实验室环境。
- 发布给终端用户的固件应关闭 Debug HID。
- Rust 管理器和 WebUI 都应视为开发工具，而不是生产客户端。

另外需要注意：

- `DIAG 3` 当前只会清空 credential slots，不会清空 PIN 状态和 UP 配置。
- `DIAG 4` 当前返回的是凭据摘要，不是私钥明文，但仍会泄露 RP、用户名、signCount 和 credential ID 前缀。
- 当前 `hardened` 启动不会继承 legacy / Debug HID 持久化的 UP baseline；但 debug 会话本身仍然可以即时绕过 UP。

## 5. 如何关闭

Windows 本地：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-hardened -DisableDebugHid
```

直接 CMake：

```bash
cmake -S . -B build-hardened -G Ninja \
  -DPICO_BOARD=meowkey_rp2350_usb \
  -DMEOWKEY_ENABLE_DEBUG_HID=OFF
cmake --build build-hardened
```

## 6. 对桌面工具的影响

关闭 Debug HID 后：

- 浏览器 WebUI 无法连接设备
- Rust 原生管理器无法连接设备
- 设备仍可作为标准 FIDO HID 认证器被系统或浏览器枚举

这正是预期行为。

## 7. `probe` 固件不是 Debug HID

`probe` 固件是单独的 USB 串口探测镜像，不复用 Debug HID：

- 它不承载 CTAP2 认证器逻辑。
- 它的职责是输出 GPIO 编码电阻/拔码与 I2C EEPROM/ID 候选信息。
- 配套脚本 `scripts/probe-board.ps1` 会把报告整理成 preset 草案。

因此：

- `probe` 适合未知底板识别。
- `debug` 适合协议联调。
- `hardened` 适合更接近分发的认证器固件。
