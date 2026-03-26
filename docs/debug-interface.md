# Debug Interface

## English

### 1. Why Debug HID Exists

Standard FIDO HID is right for browsers and operating systems, but it is not enough for firmware bring-up and board-level diagnostics. MeowKey therefore exposes an additional Debug HID interface in development builds.

It is used for:

- diagnostics log snapshots
- credential listing
- clearing diagnostics
- clearing credential slots
- board and protocol bring-up through the browser UI and Rust manager

### 2. Interface Enumeration

Default build:

- `FIDO HID`: `usagePage=0xF1D0`, `usage=0x01`
- `Debug HID`: `usagePage=0xFF00`, `usage=0x01`

Hardened build:

- standard FIDO HID only

### 3. Commands

Debug HID still uses CTAPHID framing, but it adds:

- `CTAPHID_DIAG = 0x40`

Actions in `src/ctap_hid.c`:

- `1`: fetch diagnostics snapshot
- `2`: clear diagnostics
- `3`: clear credentials
- `4`: list credential summaries
- `5`: fetch current UP config summary
- `6`: write persisted UP config
- `7`: write session-only UP config
- `8`: clear session override

Actions `3/4` are gated by `MEOWKEY_ENABLE_DANGEROUS_DEBUG_COMMANDS`.

Action `5` returns UTF-8 JSON like this:

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

Actions `6/7` use a 9-byte payload:

- `[0]`: action `6` or `7`
- `[1]`: source, where `0=none`, `1=bootsel`, `2=gpio`
- `[2]`: signed `gpioPin`, with `0xff` meaning `-1`
- `[3]`: `gpioActiveLow`
- `[4]`: `tapCount`
- `[5..6]`: little-endian `gestureWindowMs`
- `[7..8]`: little-endian `requestTimeoutMs`

`6` persists across debug reflashes. `7` only affects the current power session.

### 4. Security Boundary

Debug HID is risky because it is powerful:

- it exposes internal state
- it can change current and persisted UP behavior
- with dangerous commands enabled, it can enumerate and clear credential slots

Important details:

- `DIAG 3` clears credential slots only; it does not clear PIN state or UP config
- `DIAG 4` does not export private keys, but it leaks RP IDs, usernames, sign counts, and credential ID prefixes
- hardened startup refuses to inherit legacy or Debug HID persisted UP state

### 5. How to Disable It

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-hardened -DisableDebugHid
```

CMake:

```bash
cmake -S . -B build-hardened -G Ninja \
  -DPICO_BOARD=meowkey_rp2350_usb \
  -DMEOWKEY_ENABLE_DEBUG_HID=OFF
cmake --build build-hardened
```

### 6. Effect on Desktop Tools

Without Debug HID:

- browser WebUI cannot connect
- Rust manager cannot connect
- the device still enumerates as a normal FIDO authenticator

### 7. Probe Is Separate

The `probe` firmware is a separate USB serial image, not a Debug HID mode. Its job is board discovery and preset drafting.

## 中文

### 1. 为什么会有 Debug HID

标准 FIDO HID 适合浏览器和操作系统走正式认证流程，但不适合固件 bring-up 阶段的开发诊断。MeowKey 因此在开发构建里额外暴露了一个 Debug HID。

它主要用来做：

- 拉取诊断日志快照
- 列出当前凭据摘要
- 清空诊断缓存
- 清空凭据槽位
- 让浏览器 WebUI 和 Rust 管理器更容易做板级和协议联调

### 2. 接口枚举

默认构建：

- `FIDO HID`：`usagePage=0xF1D0`，`usage=0x01`
- `Debug HID`：`usagePage=0xFF00`，`usage=0x01`

硬化构建：

- 只保留标准 FIDO HID

### 3. 支持的命令

Debug HID 仍然走 CTAPHID 报文格式，但额外增加了：

- `CTAPHID_DIAG = 0x40`

`src/ctap_hid.c` 里当前定义的动作有：

- `1`：读取诊断日志快照
- `2`：清空诊断日志
- `3`：清空凭据槽位
- `4`：列出凭据摘要
- `5`：读取当前生效的 UP 配置摘要
- `6`：写入持久化 UP 配置
- `7`：写入只在当前上电有效的会话态 UP 配置
- `8`：清除会话态覆盖

其中 `3/4` 受 `MEOWKEY_ENABLE_DANGEROUS_DEBUG_COMMANDS` 控制。

动作 `5` 返回 UTF-8 JSON，格式如下：

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

- `[0]`：动作码 `6` 或 `7`
- `[1]`：`source`，其中 `0=none`、`1=bootsel`、`2=gpio`
- `[2]`：带符号 `gpioPin`，`0xff` 表示 `-1`
- `[3]`：`gpioActiveLow`
- `[4]`：`tapCount`
- `[5..6]`：小端 `gestureWindowMs`
- `[7..8]`：小端 `requestTimeoutMs`

`6` 写入的是持久化 baseline，会跨 debug 重刷保留；`7` 只影响当前上电会话。

### 4. 风险边界

Debug HID 的风险不在于“它不是标准接口”，而在于“它的能力过强”：

- 会暴露开发期内部状态
- 能改写当前和持久化的 UP 行为
- 如果危险命令开启，还能枚举并清空凭据槽位

还要注意：

- `DIAG 3` 只清空凭据槽位，不会清空 PIN 状态和 UP 配置
- `DIAG 4` 不会导出私钥，但会泄露 RP、用户名、signCount 和 credential ID 前缀
- `hardened` 启动不会继承 legacy 或 Debug HID 持久化写入的 UP 状态

### 5. 如何关闭

Windows：

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

### 6. 对桌面工具的影响

关闭 Debug HID 后：

- 浏览器 WebUI 无法连接设备
- Rust 管理器无法连接设备
- 设备仍然会作为普通 FIDO 认证器被枚举

### 7. Probe 不是 Debug HID

`probe` 固件是单独的 USB 串口镜像，不是 Debug HID 的一个模式。它的职责是做底板识别和 preset 草案生成。
