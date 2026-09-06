# 烧录前原理笔记（唤醒词 / 配网码 / 表情）

Updated: 2026-09-06  
Related: Step 2 烧录前的产品理解

## 1. 唤醒词「小陆同学」

- **检测位置：纯本地（端侧）**，Espressif WakeNet 模型 `wn9_xiaolutongxue`，打进 assets 分区。
- **与云端关系**：唤醒之后才连云开会话；云不负责「听没听到唤醒词」。默认 `CONFIG_SEND_WAKE_WORD_DATA=n`，连唤醒音频都不上传。
- **能换吗：能，但要换模型 + 重编资源/固件**，不是后台改个字符串。
  - 官方路径：乐鑫自定义唤醒词流程 → 新模型目录 → 改 `WAKENET_MODEL` / `boards/hover/wakenet/`
  - 另有 `USE_CUSTOM_WAKE_WORD`（MultiNet，拼音配置），与当前 wn9 路径不同
  - 海外版可并存 `Hey Kira`
- 自建网关后：仍建议端侧唤醒；云只收后续对话。

## 2. 开机微信小程序码 / 配网

### 实际链路（两段式）

1. **屏幕上的「码」** = 表情资源 `wificonfig.eaf` 里的**静态画面**（构建时从 `wificonfig_domestic.eaf` 或 overseas 拷贝），**不是**设备运行时按 MAC 动态生成的码。
2. 微信扫码 → 打开「小陆同学」小程序（微信生态深链）。
3. 小程序再通过 **BluFi（乐鑫 BLE 配网协议）** 扫描附近蓝牙名  
   `RIG-Hover` + 蓝牙 MAC 后两字节十六进制，例如 `RIG-HoverA1B2`。
4. 用户选 2.4G WiFi，小程序经 BluFi 把 SSID/密码写入设备。

### 码里「带了什么」

- **固件侧：码本身不携带设备唯一参数**；设备身份在 **BLE 广播名** 上。
- 微信小程序码通常编码：小程序 appId + 页面 path + scene（具体字符串在图片像素里，需解码小程序码工具才能还原；仓库里只有 `.eaf` 动画帧，没有明文 URL 配置文件）。
- 国内/海外两套 QR 仅是**两张不同静态图**（指向不同小程序/落地页）。

### 换成自己的 App 扫码配网

理论可行，要改两端：

| 改什么 | 说明 |
|--------|------|
| 屏上的码 | 换 `wificonfig_*.eaf` 里的图 → 指向你的 App 下载页 / Universal Link / 自定义 scheme |
| 手机端 | 实现 **Espressif BluFi 客户端**（或改用 SoftAP 热点配网 / 声学配网，固件已有相关选项） |
| 蓝牙名 | 可改 `BOARD_TYPE` / BluFi 命名逻辑，App 按同样规则扫描 |

**只换二维码图片、App 不会 BluFi → 配不成网。**

## 3. 脸部表情动画

- 实现：圆形 LCD + **EAF** 动画，配置在 `boards/hover/240_240/emote.json`，资源在 `emoji/*.eaf`。
- 谁触发：本地状态机（listen/thinking…）+ 云端 JSON `{"type":"llm","emotion":"happy"}` 等。
- **可定制**：换/加 `.eaf`，改 `emote.json`，重打 assets。
- **可远程更新**：`self.assets.set_download_url` → 设备下载新 `assets.bin`（含表情等）写入 SPIFFS；自建后台可托管该包。
- 自建网关后：继续在会话里下发 `emotion` 字段即可驱动表情。

## 对自建云方案的影响

- 唤醒词、配网 UX、表情资源主要在 **固件 + assets**，不全依赖陆吾云。
- 自建网关优先接对话/ASR/TTS/MCP；配网小程序替换、自定义唤醒词、表情包运营可作为并行或后续工单。
