# 自建云网关 · 方案备忘

Status: ready-for-human  
Updated: 2026-09-05  
Product: RIG-Hover（文文）· 固件基线 `rig_omni`

## 目标架构（已确认）

```
设备 (rig_omni 协议)
    ↓  Opus 音频 + hello/listen/stt/tts/llm/mcp
你的服务器（自建网关）
    ↓  转发 / 编排
├── ASR  → 豆包语音识别
├── LLM  → 自有 Agent / 各家模型（可换）
├── TTS  → 火山引擎
└── VLM  → 自有识图接口（MCP vision.url）
```

**一句话：** 设备只连自己的服务器；Key 与模型编排都在服务器上；不做设备直连厂商（非 BYOK / 非 `rig_local` 主路径）。

## 明确不选的路径

| 路径 | 原因 |
|------|------|
| 继续只靠陆吾官方云 | 无法把流量与 Agent 完全掌控在自己手里 |
| `rig_local` BYOK 主方案 | 设备直连厂商；ASR/TTS 默认 DashScope，且 Key 落在设备侧 |
| 设备固件内直连豆包/火山 | 要改两套客户端，成本高；与「设备→我的服务器」目标不符 |

## 固件侧策略

- **保持 `rig_omni` 云协议客户端**（WebSocket 或 MQTT + Opus）。
- 关键改动：把 **`ota_url`** 指到自建 OTA，由 OTA 响应下发自己的 `websocket` / `mqtt` 配置。
- 板级运动 / 表情 / MCP 工具仍可在 `boards/hover/` 二开；与云网关可并行。

## 配网 UX（已确认 · 2026-09-06）

- **继续 BluFi**（不切 SoftAP）。
- **不需要二维码**：App 直接 BLE 扫描 `RIG-Hover*` → EspBlufi 写 WiFi。
- **屏上小程序码**：回头把 `wificonfig` 表情换成其他图标/提示（如「请打开 App 添加设备」），仅改 assets，不改配网协议。
- 详研：`research-own-app-wifi-provisioning.md`

## 唤醒词（已确认 · 2026-09-06）

- **暂不更换**，继续「小陆同学」`wn9_xiaolutongxue`。
- 乐鑫 A/B 定制搁置；MultiNet（C）仅作后续可选。
- 详研：`research-wake-word-replacement.md`

## 表情 / EAF（已记录 backlog · 2026-09-06）

- 机制已摸清：GIF → [EAF Converter](https://esp32-gif.espressif.com/) → `boards/hover/emoji/` + `emote.json` → assets；云端 `llm.emotion` 驱动。详研：`research-eaf-emotion-customization.md`
- **三项都要做，烧录成功后再排优先级：**
  1. 换配网图标（`wificonfig_*`，不要小程序码）
  2. 整套品牌脸替换（尽量保持现有 emotion 英文名）
  3. 远程 assets 流水线（CDN `assets.bin` + `set_download_url` / 下载 Apply）
- 决策节点：Step 2 烧录验收通过之后。

## 服务器必须具备的能力

1. **OTA 配置接口** — 返回 websocket/mqtt、可选 firmware、server_time；后续可再处理 activation。
2. **实时会话** — hello、listen、stt、tts、llm、mcp；Opus 上下行；打断/状态机。
3. **模型适配** — ASR↔豆包、TTS↔火山、LLM↔自有 Agent；内部可换厂商。
4. **多模态** — MCP `initialize` 下发 `vision.url`；收 JPEG + question，回文字。
5. **MCP 编排** — 调用设备工具：`self.robot.*`、`self.camera.*` 等。

## 与官方现状的对照

- 官方 `rig_omni`：**设备 → 陆吾云 → 各家**（设备不直连模型）。
- 我们要做的：同一形态，只把「陆吾云」换成「自建网关」；ASR/TTS 在网关内转发到豆包 / 火山。

## 学习路线（按解读文档第 10 节，一步一步来）

| # | 步骤 | 状态 | 工单 |
|---|------|------|------|
| 1 | 先当用户玩通一遍 | **已完成** | `issues/01-play-as-user.md` |
| 2 | 本地编译官方 Hover 固件 | **已完成**（2026-09-07 烧录验收通过，设备 IP 192.168.1.230） | `issues/02-build-hover-firmware.md` |
| 3 | 加一个自己的 MCP 动作 | 未开始 | — |
| 4 | 决定云策略并落地自建网关 | 方案已确认，实现未开始 | 见上文「目标架构」 |
| 5 | 接多模态（识图） | 未开始 | — |
| 6 | （可选）外挂电脑 MCP | 未开始 | — |

说明：文档第 4 步曾并列 `rig_local`；**我们已选定「自建 OTA + WS/MQTT 网关」**（设备→你的服务器→豆包/火山/Agent），不再以 BYOK 为主路径。

## 网关落地子顺序（进入第 4 步时用）

1. 定技术栈 + 是否基于协议兼容开源服务端二次开发
2. 最小 OTA + WebSocket：设备改 `ota_url` 能握手 hello
3. 打通一轮：麦 → 豆包 ASR → LLM → 火山 TTS → 喇叭
4. 再加 MCP（转头/移动）与 vision 识图

## 相关本地资料

- 解读文档：`docs/RIG-Hover/RIG-Hover-深度解读与二开指南.html`
- 固件仓库：本目录 `rig_omni`
- 参考（非主路径）：`rig_local` / `rig_local_web`（BYOK）；`mcp-demo-browser`（外挂 MCP）

## 备注

- 用户设备：已购 RIG-Hover；深度二开 → 自建云 → 自有 Agent → 多模态。
- 节奏：先硬件/官方云验收，再固件，再自建云；不跳步。
