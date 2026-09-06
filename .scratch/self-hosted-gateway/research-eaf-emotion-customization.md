# Research: 表情定制与 EAF 管理（Hover）

Updated: 2026-09-06  
Primary sources: `boards/hover/emoji/`、`240_240/emote.json`、`emote_display.cc`、`assets.cc`、`application.cc`、`tools/spiffs_assets/build.py`、`CMakeLists.txt`；乐鑫 [EAF Converter](https://esp32-gif.espressif.com/) / `esp_emote_expression` / `esp_emote_gfx`

## 一句话

表情 = **圆屏 240×240 上的 EAF 动画**；名字在 `emote.json` 登记，文件在 `emoji/*.eaf`；运行时 `SetEmotion("happy")` 按名字播。云端用 JSON `{"type":"llm","emotion":"happy"}` 驱动聊天表情；本地状态机也会切 listen / thinking / wificonfig 等。

## 架构链路

```
GIF（设计稿）
  → 乐鑫在线转换 https://esp32-gif.espressif.com/  →  *.eaf
  → 放入 boards/hover/emoji/
  → 在 boards/hover/240_240/emote.json 登记 name/src/loop/fps
  → 构建：spiffs_assets/build.py 打进 assets 分区（含 index.json）
  → 开机 Assets::EmoteStrategy 挂载分区 → emote_load_assets
  → Display::SetEmotion(name) → emote_set_anim_emoji
```

渲染栈：`EmoteDisplay` → 组件 `esp_emote_expression`（`emote_mount_assets` / `emote_set_anim_emoji`）→ LCD（GC9A01）。  
文件魔数：`.EAF`（`89 45 41 46`）。Hover 现有约 **30 个** `.eaf`，合计约 **3.0 MB**。

## 目录职责（只改 Hover）

| 路径 | 作用 |
|------|------|
| `main/boards/hover/emoji/*.eaf` | 动画本体 |
| `emoji/wificonfig_{domestic,overseas}.eaf` | 配网画面源；构建时拷成 `wificonfig.eaf`（gitignored） |
| `main/boards/hover/240_240/emote.json` | 表情名 ↔ 文件、loop、fps |
| `main/boards/hover/240_240/layout.json` | 布局：主动画区、toast、listen 条、动态 QR 控件位等 |
| `main/display/emote_display.cc` | 显示封装（共享层；换表情资源一般不用改） |

板级隔离：换表情只动 `boards/hover/`，不要同时改 puppy/arm。

## 当前登记的表情名（emote.json）

**聊天/情绪类（云端常用）：**  
neutral, happy, sad, angry, surprised, thinking, winking, cool, laughing, loving, crying, confused, embarrassed, sleepy, confident, delicious, kiss, relaxed, shocked, silly

**系统/状态类（固件自切）：**  
listen（聆听）, thinking（STT 后）, wificonfig, scanning, launch, calibration, nvs_reset, remote_mode, icon_speaker_zzz

## 谁在什么时候切表情

| 触发 | 来源 | 典型 emotion |
|------|------|----------------|
| 云端 LLM | `{"type":"llm","emotion":"..."}` | happy / sad / …（须是已登记名） |
| 云端 Alert | `{"type":"alert",...,"emotion":"..."}` | 任意已登记名 |
| 本地 STT 完成 | `application.cc` | `thinking` |
| 进入聆听 | 状态机 | `listen` |
| 配网 / 扫网 | wifi_board | `wificonfig` / `scanning` |
| 开机 / 资源加载完 | application | `launch` / `happy` |
| 长按清 NVS | hover_board | `nvs_reset` |

未知名字：引擎侧行为取决于 `esp_emote_expression`（通常无对应资源则播不出或保持上一表情）；**自建网关下发的 emotion 字符串必须与 `emote.json` 的 `emote` 字段一致**。

## 怎么做新表情（制作）

1. **设计**：圆屏 **240×240**，注意圆形可视区（四角会被裁掉）。
2. **导出 GIF**（或工具支持的序列）。
3. **转 EAF**：打开 [https://esp32-gif.espressif.com/](https://esp32-gif.espressif.com/)  
   - 可裁剪、缩放、高级编码  
   - 输出 `.eaf`（压缩：RLE / Huffman / JPEG 等，见乐鑫 EAF player 文档）
4. 另有 gfx 工具站：[gfx-gen-tool](https://gfx-gen-tool.pages.dev/)（文档提及）
5. 本仓 docs 曾引用 `scripts/Image_Converter/batch_gif_to_eaf.py`（调在线服务批转）；**当前工作树未检出该脚本**，优先用官网转换器。

## 怎么接入固件（管理）

### 替换已有表情（最简单）

- 用同名文件覆盖，例如替换 `happy.eaf`  
- **不必改** `emote.json`（名字不变）  
- 重编/重打 assets 并烧录（或走远程 assets 更新）

### 新增表情名

1. 放入 `emoji/myface.eaf`  
2. 在 `emote.json` 增加一行，例如：
   ```json
   {"emote": "myface", "src": "myface.eaf", "loop": true, "fps": 30}
   ```
3. 网关/提示词里开始下发 `"emotion":"myface"`  
4. 重编 assets

### 改配网画面（已定：不要小程序码）

- 改 `wificonfig_domestic.eaf` / `wificonfig_overseas.eaf`（提示「打开 App 添加设备」类静态/动画）  
- 构建按区域生成 `wificonfig.eaf`  
- 协议仍是 BluFi，只换画面

### `emote.json` 字段

| 字段 | 含义 |
|------|------|
| `emote` | 运行时名字（`SetEmotion` / 云端 emotion） |
| `src` | `emoji/` 下文件名 |
| `loop` | 是否循环；`nvs_reset` 为 false |
| `fps` | 播放帧率；配网码常用 `1`，多数表情 `30` |

`layout.json` 一般不用为「换一套脸」而改；除非要动控件位置或加新 UI 层。

## 远程更新（自建后台）

不重刷整包固件也能换表情包：

1. 构建出完整 **`assets.bin`**（含 wakenet + 全部 eaf + index；与现网分区布局一致）  
2. 托管到你的 HTTPS URL  
3. 设备侧：MCP `self.assets.set_download_url` 写入 NVS，或走现有「检查新 assets」流程  
4. `Assets::Download` → 写入分区 → `Apply()` → `emote_load_assets`

注意：assets 分区有大小上限；Hover 仅 emoji 已约 3MB，加新动画前要算分区余量。换表情包通常仍带着唤醒词模型一起打包（当前构建管线是一套 assets）。

## 与自建网关的关系

| 层 | 谁管 |
|----|------|
| 动画像素 / 文件 | 固件 assets（本地或你 CDN 的 assets.bin） |
| 何时播哪个脸 | 网关在 `llm` 消息里填 `emotion`；或本地状态机 |
| 词表对齐 | 网关 system prompt / 工具应使用 `emote.json` 里的名字 |

运营模式可以是：后台维护「表情包版本」→ 出 assets.bin → 设备 OTA assets；对话策略只下发名字。

## 定制工作流建议

1. **先换一张**（如 `wificonfig` 或 `happy`）验证 GIF→EAF→烧录全链路  
2. 再批量替换品牌脸，保持 **现有 emotion 名字**（网关不用改）  
3. 需要新语义时再加新 `emote` 名，并同步网关词表  
4. 稳定后把打包 assets.bin + CDN + `set_download_url` 做成运营流程  

## 风险 / 注意

- 只改 Hover 的 emoji / emote.json（板级隔离）  
- emotion 名拼错 = 屏上无预期动画  
- 分区容量：新动画要控体积（转换器压缩、降 fps/帧数）  
- `wificonfig.eaf` 由构建生成，改源文件 `*_domestic/overseas`  
- 共享代码 `emote_display.cc` / `assets.cc` 改动要考虑三板兼容；单纯换资源不必动它们  

## 产品决策（2026-09-06）

- **三项全部要做**，顺序在 **Step 2 烧录成功后再排**：
  1. 换配网图标（`wificonfig_*`）
  2. 整套品牌脸替换（优先保持现有 emotion 英文名）
  3. 远程 assets 流水线（CDN + 下载 Apply）
- 当前：只记录，不实施。

## 待你拍板（烧录后）

~~先做哪块？~~ → 已改为「全部做，烧录后再排」。  
烧录验收后确认：是否保持现有 emotion 英文名（推荐只换画面）。
