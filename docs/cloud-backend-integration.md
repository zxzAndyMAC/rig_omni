# RIG-Omni 自建云对接规格

> **给谁用：** 在现有服务端里扩展「设备接入 + 语音会话 + 机器人控制」的后端工程师 / Agent。
> **合同来源：** 本仓库固件源码（`main/ota.cc`、`main/protocols/`、`main/application.cc`、`main/mcp_server.*`、`main/boards/*`）。设备只认这份行为，不认口头描述。
> **协议族：** 小智（xiaozhi）兼容。设备不直连 ASR / TTS / LLM，只认 OTA HTTP + 实时会话通道。
> **推荐路径：** 只实现 **WebSocket**。MQTT+UDP 在本仓库当前固件上不完整，见 [附录 A](#附录-a-mqttudp不建议先做)。

对照实现时，以本文的请求/响应字段为准。上游小智文档若与本文冲突，以本文（本固件）为准。

---

## 0. 你要在现有服务里加什么

把现有服务当成「业务后端」，在旁边（或内部）加四个边界清晰的模块。不必重写整站。

| 模块 | 角色 | 现有服务通常已有？ | 必须新增？ |
|------|------|-------------------|-----------|
| **OTA HTTP** | 设备开机发现会话地址、可选下发固件 | 可能已有设备表 | **必须**。设备找不到这个接口就连不上你 |
| **WebSocket 会话** | 一条连接同时走 JSON 控制 + Opus 音频 | 很少已有 | **必须** |
| **语音管线** | ASR → LLM → TTS，由会话模块驱动 | 可能已有 LLM | **必须**（可复用现有模型网关） |
| **MCP 客户端** | 会话建立后向设备发现并调用工具 | 无 | **P1**。没有就不能用语音控机器人 |

设备侧角色反过来：设备是 MCP **服务器**，你的云是 MCP **客户端**。

```
现有服务（账号 / 设备库 / 模型网关 / 管理后台）
        │
        ├── 新增 OTA HTTP  ── 设备开机 POST 一次
        ├── 新增 WS 会话   ── 唤醒后长连接
        │       ├── 调你现有的 ASR / LLM / TTS
        │       └── MCP initialize / tools/list / tools/call
        └── 新增视觉 HTTP  ── 拍照识别（可选）
```

**第一期做完的验收：** 设备改 OTA 地址后能唤醒、听见自己说话（或模型回复）、屏幕出现 STT/TTS 字幕。机器人动作是第二期。

---

## 1. 设备如何找到你的云

开机联网后，设备对 **OTA URL** 发一次 HTTP。成功响应里的 `websocket.url` 写入 NVS，之后会话直连该 URL。

### 1.1 默认 OTA 地址（固件编译进二进制）

| 固件区域 | 默认 URL |
|----------|----------|
| Domestic（国内） | `https://xl-api.xgorobot.com/xiaolu/ota/` |
| Overseas（海外） | `https://xl-api.luwudynamics.ai/xiaolu/ota/` |

解析顺序（`Ota::GetCheckVersionUrl()`）：

1. NVS 命名空间 `wifi`、键 `ota_url`（非空则用它）
2. 否则用上表按 `CONFIG_FIRMWARE_REGION_*` 选择

`menuconfig` 里的 `CONFIG_OTA_URL` **当前代码未读取**，改它不会改设备行为。要把设备指到你的云：

- 写 NVS `wifi/ota_url`，或
- 改 `main/ota.cc` 里的默认字符串后重编固件

路径可以自定义（不必叫 `/xiaolu/ota/`），只要设备请求的 URL 就是你实现的那个。

### 1.2 协议选择（OTA 响应决定，不是编译选项）

```
OTA 响应含 mqtt 对象     → 固件用 MQTT+UDP
否则含 websocket 对象    → 固件用 WebSocket
两者都没有               → 固件仍创建 MQTT 客户端，随后因没有 endpoint 失败
```

**自建云第一期：只返回 `websocket`，不要返回 `mqtt`。** MQTT 优先于 WebSocket。

---

## 2. OTA HTTP

### 2.1 请求

| 项 | 值 |
|----|----|
| 方法 | `POST`（body 非空时；本固件总会带系统信息 JSON） |
| 成功 | HTTP **200** + JSON body。非 200 设备重试（最多 10 次，间隔 10s 起指数退避） |
| 失败后 | 设备仍可能继续启动，但没有 websocket 配置就无法对话 |

**请求头：**

| Header | 来源 | 用途 |
|--------|------|------|
| `Device-Id` | MAC，形如 `aa:bb:cc:dd:ee:ff` | 设备主键 |
| `Client-Id` | UUID，NVS 擦除或整包重刷会变 | 软件实例 ID |
| `User-Agent` | `{BOARD_NAME}/{firmware_version}`，如 `RIG-Puppy/1.2.3` | 板型 + 版本 |
| `Activation-Version` | 有 efuse 序列号为 `2`，否则 `1` | 激活协议版本 |
| `Serial-Number` | 仅 Activation-Version=2 | HMAC 激活用 |
| `Accept-Language` | `zh-CN` 或 `en-US` | 语言 |
| `Content-Type` | `application/json` | |

**Body（设备画像，`version: 2`）：**

```json
{
  "version": 2,
  "language": "zh-CN",
  "flash_size": 16777216,
  "minimum_free_heap_size": "123456",
  "mac_address": "aa:bb:cc:dd:ee:ff",
  "uuid": "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "chip_model_name": "esp32s3",
  "chip_info": { "model": 9, "cores": 2, "revision": 0, "features": 0 },
  "application": {
    "name": "rig-puppy",
    "version": "1.0.0",
    "compile_time": "Sep  9 2026T12:00:00Z",
    "idf_version": "v5.5.2",
    "elf_sha256": "..."
  },
  "partition_table": [
    { "label": "nvs", "type": 1, "subtype": 2, "address": 36864, "size": 16384 }
  ],
  "ota": { "label": "ota_0" },
  "display": { "monochrome": false, "width": 240, "height": 240 },
  "board": {
    "type": "RIG-Puppy",
    "name": "RIG-Puppy",
    "ssid": "MyWifi",
    "rssi": -45,
    "channel": 6,
    "ip": "192.168.1.12",
    "mac": "aa:bb:cc:dd:ee:ff"
  }
}
```

`board.type` / `User-Agent` 前缀：

| Kconfig | `BOARD_TYPE` / `BOARD_NAME` |
|---------|------------------------------|
| Puppy | `RIG-Puppy` |
| Hover | `RIG-Hover` |
| Arm | `RIG-Arm` |
| Bot | `RIG-Bot` |

配网中 `board` 可能没有 `ssid` / `rssi` / `ip`。

云端建议用 `Device-Id`（MAC）做设备主键，用 `application.version` + `board.type` 决定是否下发新固件。

### 2.2 响应：第一期最小 JSON

HTTP 200。字段全部可选解析；缺了对应能力设备就跳过。第一期请返回下面这样，**不要多给 `mqtt` / `activation`**：

```json
{
  "websocket": {
    "url": "wss://your.example.com/xiaozhi/v1/",
    "token": "opaque-token",
    "version": 1
  },
  "firmware": {
    "version": "1.0.0",
    "url": "https://your.example.com/firmware/rig-puppy.bin"
  },
  "server_time": {
    "timestamp": 1730000000000,
    "timezone_offset": 480
  }
}
```

`websocket` 对象里每个 **string / number** 键都会原样写入 NVS 命名空间 `websocket`。固件实际读取：

| 键 | 类型 | 含义 |
|----|------|------|
| `url` | string | WebSocket URL，`ws://` 或 `wss://` |
| `token` | string | 握手 `Authorization`。不含空格时设备自动加 `Bearer ` |
| `version` | number | 二进制音频帧版本。`0` 或省略 → 固件默认 **1**（裸 Opus） |

`mqtt` 对象同样会把每个键写入 NVS 命名空间 `mqtt`。第一期不要返回它。

### 2.3 响应字段细则

#### `websocket`（P0）

没有这个对象，设备无法用 WebSocket 对话。

#### `firmware`（P1）

| 字段 | 类型 | 行为 |
|------|------|------|
| `version` | string | 语义化比较：按 `.` 分段比整数。`1.2.0` > `1.1.9`；前缀相同且新版本段更多则视为更新（`1.0.0.1` > `1.0.0`） |
| `url` | string | 同时有 version 和 url 才比较。若判定有新版本，设备 **立刻** `GET url` 刷机并重启 |
| `force` | number | `1` 时即使 version 不更高也刷 |

第一期把 `version` 设成 **不高于** 设备当前版本，或省略 `url`，避免误刷。

固件下载要求见 [第 8 节](#8-固件与资源-ota)。

#### `server_time`（P1）

| 字段 | 类型 | 行为 |
|------|------|------|
| `timestamp` | number | Unix **毫秒** |
| `timezone_offset` | number | **分钟**。有则 `timestamp += offset * 60 * 1000` 再 `settimeofday` |

没有也能对话，设备时钟不准。

#### `activation`（本仓库：不要返回）

官方小智用它做绑定码。本仓库 `Application::CheckNewVersion()` 只要看到 `activation.challenge` 就会：

1. 播激活语音
2. `nvs_flash_erase()`
3. `esp_restart()`

**不会**走 `POST .../activate` 的 HMAC 流程。OTA 响应带 `activation` 会把用户 WiFi 清掉。HMAC 激活接口实现见 [附录 B](#附录-b-hmac-激活本仓库开机路径未使用)——仅当你改固件配合时才需要。

### 2.4 现有服务怎么接 OTA

建议一个路由，例如 `POST /xiaolu/ota/` 或挂到你现有的设备 API 前缀下。逻辑：

1. 用 `Device-Id` upsert 设备行（MAC、UUID、板型、固件版本、语言、最后在线）
2. 签发短时 `token`（建议绑定 MAC，WS 握手再验）
3. 返回 `websocket.url`（按内网/公网、国内/海外选不同 host）
4. 比较固件版本，决定 `firmware` 是否给新 url
5. 填 `server_time`

不要在这个接口里做对话、不要在这里调 LLM。

---

## 3. WebSocket 会话

### 3.1 连接

设备只在需要说话时连（唤醒 / 按键），不是开机常连。MQTT 才是开机常连。

**握手头（设备设置）：**

| Header | 值 |
|--------|-----|
| `Authorization` | OTA 的 `token`；无空格则 `Bearer {token}`。token 为空则不设此头 |
| `Protocol-Version` | 与 hello 的 `version` 相同，默认 `"1"` |
| `Device-Id` | MAC |
| `Client-Id` | UUID |

云端应：校验 token ↔ Device-Id；同一 MAC 新连接踢掉旧连接；10 秒内回 server hello。

设备等待 server hello 超时 = **10 秒**，然后报「服务器超时」并回到 idle。

通道空闲：**120 秒**没有任何下行（JSON 或二进制）则设备认为超时并断开。TTS 播放期间请持续推音频或控制消息。

### 3.2 Hello

设备连上后立刻发一条 JSON 文本帧。云必须回一条 `type=hello` 且 `transport=websocket` 的 JSON，否则握手失败。

**设备 → 云：**

```json
{
  "type": "hello",
  "version": 1,
  "features": {
    "mcp": true,
    "aec": false
  },
  "transport": "websocket",
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

| 字段 | 含义 |
|------|------|
| `version` | 二进制协议版本，与 Header `Protocol-Version` 一致 |
| `features.mcp` | 本固件恒为 `true` |
| `features.aec` | 仅当设备 AEC 模式为服务端 AEC 时为 `true`。此时请用 binary v2（带 timestamp）做回声消除 |
| `audio_params` | **上行**麦克风：16 kHz、单声道、60 ms Opus（`OPUS_FRAME_DURATION_MS = 60`） |

**云 → 设备：**

```json
{
  "type": "hello",
  "transport": "websocket",
  "session_id": "任意非空字符串",
  "audio_params": {
    "format": "opus",
    "sample_rate": 24000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

| 字段 | 设备如何用 |
|------|------------|
| `transport` | **必须**等于 `"websocket"`，否则设备丢弃这条 hello |
| `session_id` | 之后设备发出的 JSON 都会带这个值。云用来关联会话 |
| `audio_params.sample_rate` | **下行 TTS** 采样率。设备按这个值解码。常见 `24000`。若与喇叭采样率不同，设备会重采样并可能失真 |
| `audio_params.frame_duration` | 下行帧时长，毫秒。缺省设备当 60 |

hello 完成前不要推 TTS 音频。hello 完成后即可发 MCP `initialize`（见第 6 节）。

### 3.3 二进制音频

WebSocket **binary 帧 = Opus**，**text 帧 = JSON**。不要把 JSON 放进 binary。

OTA `websocket.version`：

#### Version 1（默认，第一期用这个）

payload 就是裸 Opus 包，无额外头。

#### Version 2（服务端 AEC 用）

大端：

```
uint16 version
uint16 type          // 0 = OPUS
uint32 reserved
uint32 timestamp     // 毫秒，给服务端 AEC
uint32 payload_size
uint8  payload[]     // Opus
```

#### Version 3

```
uint8  type
uint8  reserved
uint16 payload_size  // 大端
uint8  payload[]
```

云下行 TTS：version 1 时直接发裸 Opus binary。设备只在 `kDeviceStateSpeaking` 时把下行音频送去解码；listening 时丢弃，避免和麦克风抢喇叭。

### 3.4 会话状态机（你必须对齐）

设备状态（`device_state.h`）：

`idle → connecting → listening ⇄ speaking → idle`

```
唤醒/按键
  → OpenAudioChannel（WS + hello）
  → SendStartListening
  → listening：上行 Opus
  → 云发 tts.state=start
  → speaking：播下行 Opus，停上行
  → 云发 tts.state=stop
  → auto 模式回到 listening；manual 模式回到 idle
打断（再唤醒 / abort）
  → 设备发 abort，丢弃发送队列
  → 云立刻停 TTS
```

默认 listening mode（`GetDefaultListeningMode()`）：

| 设备 AEC | mode 字段 | 含义 |
|----------|-----------|------|
| 关（默认） | `auto` | 设备端 VAD 判说完后停听，等 TTS |
| 开（设备或服务端） | `realtime` | 边听边说，需要 AEC |

手动按键听会发 `mode: "manual"`，TTS stop 后回到 idle，不再自动听。

---

## 4. JSON 消息目录

所有 JSON 都有 `type`。设备发出的业务消息还带 `session_id`（hello 之后）。未知 `type` 设备打日志并忽略。

### 4.1 设备 → 云

#### `listen` / `start`

开始把麦克风 Opus 推给你。

```json
{"session_id":"xxx","type":"listen","state":"start","mode":"auto"}
```

`mode`：`auto` | `manual` | `realtime`。

**云：** 开始收 binary、送 ASR；`auto` 时用 VAD/ASR 终点决定「用户说完」。

#### `listen` / `stop`

```json
{"session_id":"xxx","type":"listen","state":"stop"}
```

**云：** 停止把新音频当作用户说话（当前 utterance 可做完 ASR）。

#### `listen` / `detect`（本地唤醒）

```json
{"session_id":"xxx","type":"listen","state":"detect","text":"小陆同学"}
```

若编译了 `CONFIG_SEND_WAKE_WORD_DATA`，这条之前会先推一段唤醒词 Opus，可供声纹。未编译则只有这条 JSON。

**云：** 可用来开新一轮、做声纹、或忽略 `text` 只当「用户唤醒了」。

#### `abort`

```json
{"session_id":"xxx","type":"abort","reason":"wake_word_detected"}
```

`reason` 仅在因唤醒打断时出现，值为 `wake_word_detected`。其它打断可能没有 `reason`。

**云：** 立即停 TTS、丢弃未发完的音频、取消进行中的 LLM 流。不要再发 `tts.stop` 之前的残留 binary。

#### `mcp`

见第 6 节。设备把 JSON-RPC **结果**包在 `payload` 里发给你。

#### `goodbye`

仅 MQTT 通道。WebSocket 关连接不发 goodbye。

### 4.2 云 → 设备

#### `hello`

见 3.2。必须是握手的第一条业务 JSON。

#### `stt`（P0）

用户这句话的识别结果。设备用来：屏幕显示用户文本、切 thinking 表情、播「发送」音效（AEC 关闭时）。

```json
{"session_id":"xxx","type":"stt","text":"向前走两步"}
```

在 `tts.start` **之前**发。只在 listening 时设备会切 thinking。

#### `llm`（P1）

改屏幕表情。不发也能说话。

```json
{"session_id":"xxx","type":"llm","emotion":"happy","text":"😀"}
```

设备只读 `emotion` 字符串。建议用设备已有 EAF 名（小写）：

`neutral` `happy` `sad` `angry` `surprised` `shocked` `thinking` `listen` `sleepy` `confused` `cool` `confident` `embarrassed` `laughing` `loving` `relaxed` `silly` `winking` `crying` `kiss` `delicious`

未知名字不会崩，只是可能没有动画。`thinking` / `listen` 设备自己也会设，云再设会覆盖。

#### `tts`（P0）

控制喇叭状态机。**顺序固定：**

```json
{"session_id":"xxx","type":"tts","state":"start"}
```

设备切 `speaking`，开始接受 binary 音频。

```json
{"session_id":"xxx","type":"tts","state":"sentence_start","text":"好的，我走两步"}
```

屏幕显示助手字幕。可按句多次发。

然后推送 Opus binary 帧。

```json
{"session_id":"xxx","type":"tts","state":"stop"}
```

设备：AEC 关闭时播结束音；`manual` → idle；否则 → listening 并再发 `listen/start`。

规则：

- `start` 之前不要推 TTS 音频（会被丢）
- `stop` 之后不要再推该轮音频
- 收到设备 `abort` 后不要再 `start`/`stop` 这一轮；等下一轮 `listen/start`

#### `mcp`（P1）

见第 6 节。你把 JSON-RPC **请求**放在 `payload`。

#### `system`（P2）

```json
{"session_id":"xxx","type":"system","command":"reboot"}
```

本固件只实现 `reboot`。未知 command 打日志。

#### `alert`（P2）

三个字段都是字符串，缺一则忽略：

```json
{
  "session_id": "xxx",
  "type": "alert",
  "status": "Warning",
  "message": "Battery low",
  "emotion": "sad"
}
```

设备改状态栏、表情、聊天文本，并播振动音。

#### `custom`（P2，默认编译关闭）

需 `CONFIG_RECEIVE_CUSTOM_MESSAGE`。默认 sdkconfig **未打开**。打开后：

```json
{"session_id":"xxx","type":"custom","payload":{"anything":true}}
```

`payload` 必须是 object，设备把它打印到聊天区。

---

## 5. 语音管线（云内部，设备看不见）

设备只提供 Opus 帧和 listen/abort。下面是你现有服务里要串起来的部分。

### 5.1 推荐数据流

```
上行 Opus (16 kHz, 60 ms)
  → 解码 PCM
  → ASR（流式更好）
  → 用户说完（auto：VAD/静音；manual：listen/stop；realtime：持续）
  → 发 type=stt
  → LLM（带历史、人设、MCP tools）
  → 可选 type=llm emotion
  → 若 LLM 要调工具：MCP tools/call，把结果再喂 LLM
  → type=tts start
  → TTS 流式出 PCM → 编码 Opus（采样率 = server hello）
  → 边编码边推 binary
  → 按句 type=tts sentence_start
  → type=tts stop
```

### 5.2 和现有模型网关对接时的约束

| 约束 | 值 |
|------|-----|
| 上行编码 | Opus，16 kHz，mono，60 ms/帧 |
| 下行编码 | Opus，采样率与 hello 声明一致（建议 24 kHz） |
| 打断 | 设备 `abort` 必须取消 ASR/LLM/TTS 任务 |
| 多轮 | 用 `session_id` + `Device-Id` 存短期记忆 |
| 语言 | OTA 的 `Accept-Language`；运行中 MCP `self.set_language` 可能改成 `zh-CN` / `en-US` |
| 并发 | 一设备一会话；新 WS 踢旧 WS |

### 5.3 第一期可以偷懒的地方

- TTS 采样率先固定 24 kHz，hello 里写死
- ASR 非流式：等 VAD 结束再识别整段（auto 模式延迟会大一些，但能通）
- LLM 先不调 MCP，只聊天
- 不做声纹、记忆总结、知识库

---

## 6. MCP（语音控机器人）

MCP 跑在 **已打开的音频通道**上，不是独立端口。封装：

```json
{
  "session_id": "xxx",
  "type": "mcp",
  "payload": { }
}
```

`payload` 是 JSON-RPC 2.0 对象。`jsonrpc` 必须是 `"2.0"`。`id` 必须是 **number**（设备用 `valueint`）。`notifications*` 开头的 method 设备直接丢弃。

设备是 server，你是 client。

### 6.1 会话建立后立刻做

hello 成功后（可与第一轮 ASR 并行）：

1. `initialize`
2. `tools/list`（如需控制台工具再带 `withUserTools: true`）
3. 把工具列表注入 LLM
4. LLM 需要时 `tools/call`，等结果再继续生成

### 6.2 `initialize`

**云 → 设备：**

```json
{
  "session_id": "xxx",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
      "capabilities": {
        "vision": {
          "url": "https://your.example.com/vision/explain",
          "token": "optional-bearer"
        }
      }
    }
  }
}
```

`params.capabilities.vision` 可选。有则写入摄像头 `SetExplainUrl`。之后 `self.camera.take_photo` 才会 POST 图片。没有摄像头的板子忽略。

**设备 → 云：**

```json
{
  "session_id": "xxx",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
      "protocolVersion": "2024-11-05",
      "capabilities": { "tools": {} },
      "serverInfo": { "name": "RIG-Puppy", "version": "1.0.0" }
    }
  }
}
```

`serverInfo.name` 是 `BOARD_NAME`。

### 6.3 `tools/list`

```json
{
  "session_id": "xxx",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 2,
    "method": "tools/list",
    "params": {
      "cursor": "",
      "withUserTools": false
    }
  }
}
```

| 参数 | 默认 | 含义 |
|------|------|------|
| `cursor` | 空 = 从头 | 上一页返回的 `nextCursor`（工具 **name**） |
| `withUserTools` | false | true 才包含 `user_only` 工具（重启、刷机等）。LLM 默认不要开 |

设备单包大约 **8000 字节**。超了返回 `nextCursor`，你再带 cursor 请求直到没有 `nextCursor`。

工具 JSON：

```json
{
  "name": "self.dog.move",
  "description": "...",
  "inputSchema": {
    "type": "object",
    "properties": {
      "dog_vx": { "type": "integer", "minimum": -100, "maximum": 100 }
    },
    "required": ["dog_vx", "dog_vyaw", "time"]
  }
}
```

`user_only` 工具额外：

```json
"annotations": { "audience": ["user"] }
```

没有 default 的字段在 `required` 里。整数可能带 `minimum` / `maximum`。

### 6.4 `tools/call`

```json
{
  "session_id": "xxx",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 3,
    "method": "tools/call",
    "params": {
      "name": "self.dog.Sit",
      "arguments": {}
    }
  }
}
```

参数类型必须匹配：boolean / number / string。整数越界设备回 error。缺必填字段回 `Missing valid argument: {name}`。未知工具回 `Unknown tool: {name}`。

工具在设备**主线程**执行，可能阻塞（动作 `vTaskDelay` 1s）。你要能等。

**成功：**

```json
{
  "session_id": "xxx",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 3,
    "result": {
      "content": [{ "type": "text", "text": "true" }],
      "isError": false
    }
  }
}
```

`text` 可能是 `"true"` / `"false"` / 数字字符串 / JSON 字符串 / 人类可读中文。图片结果 `type=image`（屏幕截图类工具）。

**失败：**

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "error": { "message": "Failed to capture photo" }
}
```

把 `content[0].text` 或 `error.message` 喂回 LLM，再生成对用户说的话。

### 6.5 通用工具（三板型都有，给 LLM）

| name | 参数 | 说明 |
|------|------|------|
| `self.get_device_status` | 无 | 返回 JSON：音量、亮度、主题、电量、WiFi、固件版本、语言、AEC、温度。**控设备前先调这个** |
| `self.audio_speaker.set_volume` | `volume` int 0–100 **必填** | 设音量 |
| `self.screen.set_brightness` | `brightness` int 0–100 | 有背光才注册 |
| `self.screen.set_theme` | `theme` string：`light` / `dark` | 有 LVGL 主题才注册 |
| `self.camera.take_photo` | `question` string | 拍照并 POST 到 vision URL，返回模型文本。没设 vision url 会报错 |
| `self.camera.preview` | 无 | 只拍照显示，不上云 |
| `self.set_power_mode` | `mode`：`performance` / `balanced` / `low_power` | 性能/休眠 |
| `self.set_language` | `language`：`zh-CN` / `en-US` | 显示与语音语言 |
| `self.set_press_to_talk` | `mode`：`press_to_talk` / `click_to_talk` | 按住说 / 单击说（部分板） |

`get_device_status` 示例：

```json
{
  "audio_speaker": { "volume": 70 },
  "screen": { "brightness": 80, "theme": "dark" },
  "battery": { "level": 85, "charging": false },
  "network": { "type": "wifi", "ssid": "MyWifi", "signal": "strong" },
  "firmware_version": "1.0.0",
  "language": "zh-CN",
  "board_type": "...",
  "interrupt_mode": "off",
  "chip": { "temperature": 41.2 }
}
```

`interrupt_mode`：`off` / `device` / `server`。

### 6.6 仅控制台工具（`withUserTools: true`，不要给对话 LLM）

| name | 参数 | 说明 |
|------|------|------|
| `self.get_system_info` | 无 | 完整设备画像 JSON（同 OTA body） |
| `self.reboot` | 无 | 1 秒后重启 |
| `self.set_aec` | `enable` int 0–1，默认 0 | 服务端 AEC，持久化 |
| `self.upgrade_firmware` | `url` string | GET 该 URL 刷机。注意：固件把这段说明字串当成了 **default**，因此 `url`  technically 可选——调用时仍应传真实 URL |
| `self.screen.get_info` | 无 | `{width,height,monochrome}` |
| `self.screen.snapshot` | `url` string，`quality` int 1–100 默认 80 | JPEG POST 到 url 字段 `file` |
| `self.screen.preview_image` | `url` string | GET 图片显示在屏上 |
| `self.assets.set_download_url` | `url` string | 下次启动拉资源包 |

### 6.7 Puppy 工具

| name | 参数 | 说明 |
|------|------|------|
| `self.dog.move` | `dog_vx` -100–100，`dog_vyaw` -100–100，`time` 0–10000 ms | 前正后负；左转正右转负；`time=0` 持续 |
| `self.dog.calibrate` | `mode` 0–1 | 1 进入标定，0 退出 |
| `self.dog.Wave` | 无 | 打招呼 |
| `self.dog.Naughty` | 无 | 撒娇 |
| `self.dog.Swing` | 无 | 前后晃 |
| `self.dog.Lookup` | 无 | 抬头祈求 |
| `self.dog.Rolling` | 无 | 左右摇 |
| `self.dog.Angry` | 无 | 生气 |
| `self.dog.Swimming` | 无 | 游泳 |
| `self.dog.Pee` | 无 | 撒尿 |
| `self.dog.Stretch` | 无 | 伸懒腰 |
| `self.dog.Bouncing` | 无 | 蹲起 |
| `self.dog.Shaking` | 无 | 摇头 |
| `self.dog.Sit` | 无 | 坐下 |
| `self.dog.Scratch` | 无 | 挠痒 |
| `self.dog.Hug` | 无 | 抱抱 |
| `self.dog.Reset` | 无 | 站立复位 |
| `self.dog.action_loop` | `flag` 0–1 | 表演循环 |
| `self.laser.control` | `mode` 0 关 / 1 开 / 2 切换 | 激光 |
| `self.dog.set_motor_angle` | `angle1..4` -135–135，`angle5` -30–30，`time` 0–10000 | 即兴五关节。描述里要求一次指令流不超过约 15 条、间隔 100–500 ms |
| `self.ble.remote_control` | `enable` 0–1 | 开/关 BLE 遥控 |

无参动作工具执行时会 `vTaskDelay(1000)`，LLM 连调会排队。

### 6.8 Hover 工具

| name | 参数 | 说明 |
|------|------|------|
| `self.robot.head_angle` | `angle` -75–75 | 头：正左负右，0 中 |
| `self.robot.move` | `distance` -20–20 cm | 正前负后，未说默认 10 |
| `self.robot.rotate` | `angle` -180–180° | **机身**旋转，正左负右，未说默认 30。与转头区分 |
| `self.status.battery` | 无 | 电量文本 |
| `self.emoji.show` | `name` string | 播表情，如 `happy` |

### 6.9 Arm 工具

| name | 参数 | 说明 |
|------|------|------|
| `self.dog.calibrate` | `mode` 0–1 | 与 Puppy 同名 |
| `self.arm.teach_start` | 无 | 进入示教，舵机放松，最长 15 s |
| `self.arm.teach_stop` | 无 | 结束录制，不自动回放 |
| `self.arm.teach_play` | 无 | 回放；未示教过返回 -1 |
| `self.arm.teach_cancel` | 无 | 丢弃轨迹 |
| `self.arm.teach_status` | 无 | 状态文本 |
| `self.arm.idle_motion` | `enable` 0–1 | 空闲微动 |
| `self.arm.stretch` / `apology` / `shy` / `coquetry` / `bow` / `feign_death` / `wag_tail` | 无 | 预设动作 |
| `self.arm.node` | 无 | 点头 |
| `self.arm.shake` | 无 | 摇头 |
| `self.arm.x` | `x` -3–3 cm | 头前后，累加，有限幅 |
| `self.arm.y` | `y` -2–2 cm | 头左右 |
| `self.arm.z` | `z` -5–5 cm | 头上下 |
| `self.arm.yaw` | `yaw` -60–60° | 头左右转 |
| `self.arm.pitch` | `pitch` -30–30° | 正下负上 |
| `self.arm.roll` | `roll` -30–30° | 歪头 |
| `self.laser.control` | `mode` 0–2 | 激光 |
| `self.ble.remote_control` | `enable` 0–1 | BLE 遥控 |

示教流程给 LLM：`teach_start` → 用户拖动 → `teach_stop` → `teach_play`。

### 6.10 LLM 侧建议

- System prompt 用 **当前这次** `tools/list` 的 name + description，不要写死板级工具（Puppy/Hover/Arm 不同）。
- 控音量/亮度前先 `self.get_device_status`。
- 用户说「看一下这是什么」才 `take_photo`；只说「拍张照」用 `preview`。
- 不要把 `reboot` / `upgrade_firmware` / `set_aec` 暴露给对话模型。

---

## 7. 视觉 HTTP

MCP `initialize` 带了 `capabilities.vision.url` 之后，`self.camera.take_photo` 会：

```
POST {url}
Device-Id: {mac}
Client-Id: {uuid}
Authorization: Bearer {token}     # token 非空才加
Content-Type: multipart/form-data; boundary=----ESP32_CAMERA_BOUNDARY
Transfer-Encoding: chunked
```

字段：

| name | 内容 |
|------|------|
| `question` | 用户问题文本 |
| `file` | `filename="camera.jpg"`，`Content-Type: image/jpeg` |

成功：**200**，body 当纯文本返回给工具（通常是模型说明）。非 200 工具抛错，设备回 MCP error。

在现有服务里可复用已有的多模态网关，包一层符合上面 multipart 的路由即可。

`self.screen.snapshot` 是另一条 POST：字段名同样是 `file`，文件名 `screenshot.jpg`，**没有** Device-Id 头（只设了 Content-Type）。接收端不要写死必须有 Device-Id。

---

## 8. 固件与资源 OTA

### 8.1 固件二进制

OTA 响应或 MCP `self.upgrade_firmware` 给出 URL 后：

```
GET {firmware_url}
```

要求：

- HTTP **200**
- **Content-Length 必须有且 > 0**（长度为 0 设备失败）
- Body 是 ESP32-S3 app image（带 `esp_image_header` + `esp_app_desc`）
- 支持分块读；设备 4 KB 页写入

刷完 `esp_ota_set_boot_partition` 并重启。不要在升级中途关连接就当成功。

按 `board.type` 提供不同镜像：`rig-puppy.bin` / `rig-hover.bin` / `rig-arm.bin`。

### 8.2 资源包

设备启动时若 NVS `assets/download_url` 有值会去下载。控制台可通过 `self.assets.set_download_url` 设置。第一期可忽略。

---

## 9. 在现有服务里的落地顺序

按模块往现有项目里加，每期都有可测的完成标准。

### 第 0 期 — 发现

- [ ] 新增 `POST /xiaolu/ota/`（或你的前缀）
- [ ] upsert 设备（MAC / UUID / 板型 / 版本）
- [ ] 返回 **仅** `websocket` + `server_time` + 一个不会触发升级的 `firmware.version`
- [ ] **不**返回 `mqtt`、`activation`
- [ ] 设备能配网后打到该接口且 HTTP 200（抓包或服务端日志）

完成标准：日志里有 `Device-Id`，设备不再报「检查新版本失败」。

### 第 1 期 — 协议打通（可以没有真模型）

- [ ] WS 路由，校验 `Authorization` / `Device-Id`
- [ ] 10 s 内回 hello（`transport=websocket`，TTS `sample_rate=24000`）
- [ ] 收到 binary 原样或延迟回放（回声），验证设备进 speaking
- [ ] 按 4.2 发假 `stt` → `tts start` → 一句 `sentence_start` → binary → `tts stop`

完成标准：唤醒后屏幕出现字幕、喇叭出声，然后回到 listening/idle。

### 第 2 期 — 真语音

- [ ] 上行 Opus → 现有 ASR
- [ ] 现有 LLM 多轮（按 Device-Id 存历史）
- [ ] 现有 TTS → Opus 24 kHz 下行
- [ ] 处理 `abort`（取消生成与播放）
- [ ] `auto` 模式 VAD 结束再回完整 STT

完成标准：能多轮对话，二次唤醒能打断。

### 第 3 期 — 控机器人

- [ ] hello 后 `initialize` + `tools/list`（可分页）
- [ ] LLM function calling 接到 MCP `tools/call`
- [ ] 按 `serverInfo.name` / 工具列表区分 Puppy / Hover / Arm
- [ ] 视觉路由（可选）

完成标准：说「坐下」Puppy 坐下；说「左转 30 度」Hover 转；说「点头」Arm 点头。

### 第 4 期 — 运维

- [ ] 按板型托管固件，OTA 比较版本后下发 url
- [ ] 控制台远程 `system.reboot` / MCP 刷机（user tools）
- [ ] 在线状态（WS 连接 = 会话中；MQTT 才是常在线——你没用 MQTT 就不要做「开机在线」除非另做心跳）

WebSocket 方案下设备空闲是断开的，「是否在线」只能表示「正在对话」或依赖你自己的业务心跳。不要按手机 IM 的常连接模型设计。

---

## 10. 现有代码里建议的对象边界

不必照搬语言，按职责切开，避免和现有 HTTP API 搅在一起。

```
OtaHandler            只做 POST OTA：登记设备、签发 token、拼 JSON
DeviceSession         一条 WS：hello、收发 JSON/binary、超时 120s、abort
AudioPipeline         Opus↔PCM、调 ASR/TTS；不感知 MCP
ConversationAgent     调 LLM、决定是否 tools/call、发 stt/llm/tts
McpClient             initialize / list / call，把结果还给 Agent
VisionExplainHandler  multipart 图片 → 多模态模型 → 文本
FirmwareStore         按 board.type + version 给下载 URL
```

`DeviceSession` 是唯一知道 WebSocket 帧类型的地方。LLM 不要直接 `ws.send`。

Token：OTA 签发、WS 校验，绑定 `Device-Id`，过期时间可长（设备会把 token 存 NVS，直到下次 OTA 覆盖）。

---

## 11. 本仓库相对上游小智的硬差异

实现时按这些做，不要抄官方云的激活流。

1. **OTA 出现 `activation.challenge` → 设备擦 NVS 重启。** 不要返回 `activation`。
2. **同时返回 mqtt 和 websocket → 走 MQTT。** 第一期只返回 websocket。
3. **MQTT 客户端从不 `Subscribe()`。** 即使你做了 MQTT 网关，下行也到不了设备，除非改固件。
4. **`CONFIG_OTA_URL` 未接线。** 改默认地址靠 NVS `wifi/ota_url` 或改 `ota.cc`。
5. **`custom` 消息默认未编译。**
6. **空闲无 WS。** 不能靠 WS 做远程开机指令；远程控设备要么等下次唤醒，要么以后做 MQTT（并改固件 Subscribe）。

---

## 12. 验收用例（可当集成测试脚本）

用一台真机或模拟 hello/listen 的客户端。

| # | 步骤 | 期望 |
|---|------|------|
| 1 | POST OTA，无 mqtt/activation | 200，含 websocket.url |
| 2 | POST OTA 带低于设备的 firmware.version | 设备不升级 |
| 3 | WS 不回 hello | 设备 10 s 内失败回 idle |
| 4 | hello `transport` 不是 `websocket` | 设备当握手失败 |
| 5 | 回声：收到 Opus 原样送回 + tts start/stop | 能听到自己 |
| 6 | 真 ASR+TTS 一轮 | 屏幕有用户句和助手句 |
| 7 | 说话中再唤醒 | 收到 abort，TTS 停 |
| 8 | 120 s 不发下行 | 设备断开 |
| 9 | initialize + tools/list | 返回该板型工具，Puppy 有 `self.dog.Sit` |
| 10 | tools/call `self.dog.Sit` | 狗坐下，MCP result `true` |
| 11 | take_photo 未配 vision | MCP error，不崩会话 |
| 12 | 配了 vision 后 take_photo | 你的 explain 接口收到 jpeg + question |

---

## 13. 源码对照表

| 行为 | 文件 |
|------|------|
| OTA URL、请求头、响应解析 | `main/ota.cc` |
| 协议选择 | `main/application.cc` `InitializeProtocol()` |
| JSON 入站分发 | `main/application.cc` `OnIncomingJson` |
| listen / abort / mcp 出站 | `main/protocols/protocol.cc` |
| WS hello、二进制帧 | `main/protocols/websocket_protocol.cc` |
| MQTT+UDP | `main/protocols/mqtt_protocol.cc` |
| 超时 120 s | `main/protocols/protocol.cc` `IsTimeout()` |
| MCP JSON-RPC | `main/mcp_server.cc` / `mcp_server.h` |
| 通用 / 控制台工具 | `main/mcp_server.cc` |
| Puppy 工具 | `main/boards/puppy/puppy_board.cc` |
| Hover 工具 | `main/boards/hover/hover_board.cc` |
| Arm 工具 | `main/boards/arm/arm_board.cc` |
| 视觉 POST | `main/boards/common/esp32_camera.cc` `Explain()` |
| 设备画像 | `main/boards/common/board.cc` `GetSystemInfoJson()` |
| 设备状态 JSON | `main/boards/common/wifi_board.cc` `GetDeviceStatusJson()` |
| 默认 OTA 域名 | `main/Kconfig.projbuild`、`main/ota.cc` |
| Opus 帧长 60 ms | `main/audio/audio_service.h` |

上游参考（有出入时以本表文件为准）：

- https://github.com/78/xiaozhi-esp32/blob/main/docs/websocket.md
- https://github.com/78/xiaozhi-esp32/blob/main/docs/mqtt-udp.md
- https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-protocol.md

---

## 附录 A. MQTT+UDP（不建议先做）

仅当你改固件补上 `Subscribe()` 之后才有用。OTA 返回 `mqtt` 则整机会走这条，WebSocket 被跳过。

OTA `mqtt` 对象写入 NVS 的键：

| 键 | 含义 |
|----|------|
| `endpoint` | `host` 或 `host:port`。无端口则 **8883 + TLS**；其它端口明文 TCP |
| `client_id` | MQTT client id |
| `username` / `password` | 认证 |
| `keepalive` | 秒，默认 30 |
| `publish_topic` | 设备 **只往这里 Publish**。空则发不出 hello |

设备 hello：`type=hello`，`version=3`，`transport=udp`，features/audio_params 同 WS。

云必须经 MQTT 回：

```json
{
  "type": "hello",
  "transport": "udp",
  "session_id": "xxx",
  "audio_params": { "format": "opus", "sample_rate": 24000, "channels": 1, "frame_duration": 60 },
  "udp": {
    "server": "x.x.x.x",
    "port": 8888,
    "key": "0123456789ABCDEF0123456789ABCDEF",
    "nonce": "0123456789ABCDEF0123456789ABCDEF"
  }
}
```

`key` / `nonce` 为 hex。AES-128-CTR。UDP 包：

```
u8  type = 0x01
u8  flags
u16 payload_len   // 写入 nonce[2..3] 网络序
u32 ssrc
u32 timestamp     // nonce[8..11]
u32 sequence      // nonce[12..15]
u8  payload[]     // CTR 加密的 Opus
```

JSON 控制消息与 WebSocket 相同。客户端主动关通道会发 `type=goodbye`。服务端发 goodbye 时设备不再回 goodbye。

当前固件不订阅任何 topic，云的 hello 回复送达不了设备。

---

## 附录 B. HMAC 激活（本仓库开机路径未使用）

接口形态供对照。**在改固件之前不要在 OTA 响应里启用。**

- OTA 可含 `activation: { message, code, challenge, timeout_ms }`
- 设备 `POST {ota_url}activate`（url 无尾斜杠则加 `/activate`）
- Body：`{"algorithm":"hmac-sha256","serial_number":"...","challenge":"...","hmac":"<hex>"}`
- HMAC：ESP HMAC_KEY0，对 challenge 字节做 SHA-256
- 200 = 成功；202 = 仍等待用户确认（设备当 timeout）

本仓库看到 challenge 会擦除 NVS，不会 POST activate。

---

## 附录 C. 最小 OTA 响应与最小 WS 时序（可直接当 mock）

**OTA 200 body：**

```json
{
  "websocket": {
    "url": "ws://10.0.0.2:8000/xiaozhi/v1/",
    "token": "dev-token",
    "version": 1
  },
  "firmware": { "version": "0.0.1" },
  "server_time": { "timestamp": 1730000000000, "timezone_offset": 480 }
}
```

**WS 时序：**

```
C: (upgrade, headers Authorization/Device-Id/Client-Id/Protocol-Version)
C: {"type":"hello","version":1,"features":{"mcp":true},"transport":"websocket","audio_params":{"format":"opus","sample_rate":16000,"channels":1,"frame_duration":60}}
S: {"type":"hello","transport":"websocket","session_id":"s1","audio_params":{"format":"opus","sample_rate":24000,"channels":1,"frame_duration":60}}
C: {"session_id":"s1","type":"listen","state":"start","mode":"auto"}
C: (binary opus)...
S: {"session_id":"s1","type":"stt","text":"你好"}
S: {"session_id":"s1","type":"llm","emotion":"happy"}
S: {"session_id":"s1","type":"tts","state":"start"}
S: {"session_id":"s1","type":"tts","state":"sentence_start","text":"你好，我在"}
S: (binary opus)...
S: {"session_id":"s1","type":"tts","state":"stop"}
```

把这段做成 mock server，不接模型也能验收第 1 期。
