# Research: 唤醒词替换 / WakeNet 怎么来

Updated: 2026-09-06  
Primary sources:
- [ESP-SR Wake Words Customization](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html)
- [esp-sr#88 TTS 训练申请](https://github.com/espressif/esp-sr/issues/88)
- 本仓：`main/boards/hover/wakenet/wn9_xiaolutongxue/`、`main/CMakeLists.txt`、`main/Kconfig.projbuild`、`main/audio/wake_words/custom_wake_word.cc`

## 先纠正一个常见误解

**WakeNet 模型不是你在本机用几句录音就能「训练」出来的。**  
训练管线在乐鑫侧；你能做的是：

1. 向乐鑫申请 / 付费定制，拿到 `wn9_*` 模型目录；或  
2. 用不需要新 WakeNet 的 **MultiNet 自定义词**（拼音命令词，效果与专业唤醒不同）；或  
3. 直接换用 ESP-SR 已开源的现成唤醒词。

当前 Hover 的「小陆同学」本身就是 **TTS Pipeline V2 训出来的模型**，证据在 `_MODEL_INFO_`：

```
wakenet9_tts2h12_Xiao3Lu4Tong2Xue2_3_0.618_0.623
```

文件：`wn9_data` + `wn9_index` + `_MODEL_INFO_`，由 CMake 打进 assets（`WAKENET_MODEL=wn9_xiaolutongxue`）。

## 三条可行路径

### 路径 A：乐鑫 TTS 社区定制（免费商用，排队）

入口：[esp-sr#88](https://github.com/espressif/esp-sr/issues/88)

- 乐鑫用 TTS 合成语料训练 WakeNet；宣称相对真人语料约 90–98% 效果（V2/V3）。
- 模型与 esp-sr 同许可证，**可商用**。
- 自 2024-08-01 起，新词申请需满足其一：
  - 有进行中的项目，提交时附 **项目链接 + 简介**；或  
  - 该唤醒词获 **5+ upvote**
- 需同意 [Wake Word Submission Agreement](https://github.com/espressif/esp-sr/blob/master/docs/_static/Wake%20Word%20Submission%20Agreement.pdf)
- 语言：中/英/日/法（TTS Pipeline V3）；资源有限，热门词优先
- **你不自己训**；发 Issue 等模型发布后，把目录拷进 `boards/hover/wakenet/<name>/` 并改 `WAKENET_MODEL`

选词建议（乐鑫工程师在 #88 反复强调）：

- 约 **3–6 个音节**（如「小爱同学」「你好小智」）
- **避免 1–2 音节**（误唤醒难压）
- 中文尽量清晰、不易与日常口语撞车
- 商用前自行处理商标/品牌权

### 路径 B：乐鑫付费离线定制（量产高精度）

文档：Customization Process

两种付费方式：

| 方式 | 你做什么 | 周期（文档） |
|------|----------|--------------|
| 自备语料 | ≥ **20,000** 条合格样本 | 语料齐后约 2–3 周训练优化 |
| 乐鑫采语料 | 谈需求；另收采集费 | 采集时间另议 + 2–3 周 |

费用：按唤醒词数量与量产规模，联系 `sales@espressif.com`。

#### 自备语料硬性要求

**文件格式**

- 16 kHz、16-bit signed、单声道、WAV

**采样**

- **> 500 人**，男女老幼；其中 **≥ 100 名儿童**
- 安静房间（< 40 dB），建议专业录音室
- 高保真麦克风
- 每人 **1 米** 处读唤醒词 **15 遍**（快/中/慢各 5）
- 每人 **3 米** 处再 **15 遍**（快/中/慢各 5）
- 文件名建议带性别/年龄/语速等信息

**模型能力**

- 每个 WakeNet 最多约 **5** 个唤醒词
- 词长通常 **3–6 symbols**
- 可挂多个模型，但吃更多 RAM/算力

硬件腔体/麦阵也会影响唤醒；文档另有录音/AEC/测试建议，量产可寄样机给乐鑫调优。

### 路径 C：固件内 MultiNet「自定义唤醒词」（不训 WakeNet）

Kconfig：`USE_CUSTOM_WAKE_WORD`（与 `USE_AFE_WAKE_WORD` 二选一）

- 用 **MultiNet** + 拼音串，例如 `xiao tu dou` / 显示「小土豆」
- **无需**向乐鑫要新 wn9 模型；改 menuconfig / assets 里 multinet 配置即可
- 适合快速试验；误唤醒、嘈杂环境表现通常 **不如专用 WakeNet**
- 代码：`custom_wake_word.cc`（依赖 multinet 模型 + 拼音命令）

## 换进本仓库的步骤（拿到模型之后）

仅 **Hover**（板级隔离）：

1. 新建 `main/boards/hover/wakenet/<新模型名>/`，放入 `_MODEL_INFO_`、`wn9_data`、`wn9_index`（以乐鑫交付为准）
2. `main/CMakeLists.txt` 中 Hover 的 `WAKENET_MODEL` 改为新目录名（当前 `wn9_xiaolutongxue`）
3. 保持 `USE_AFE_WAKE_WORD`（S3 + PSRAM，带 AFE/AEC）
4. 重编 assets + 固件并烧录验证

也可先试用 ESP-SR 已有开源词（如 `wn9_nihaoxiaozhi_tts`），验证打包链路，再等定制模型。

## 和云端的关系

- 检测：**纯端侧**；换词不依赖自建网关。
- 云端最多收到「已唤醒」后的会话；默认不上传唤醒音频（`CONFIG_SEND_WAKE_WORD_DATA=n`）。

## 建议怎么选

| 目标 | 建议 |
|------|------|
| 快速试自己的叫法、可接受效果一般 | 路径 C MultiNet |
| 正式品牌唤醒词、成本可控 | 路径 A 提 Issue #88（附项目说明） |
| 量产、要压误唤醒到很低 | 路径 B 付费 + 真人语料 |

## 产品决策（2026-09-06 已确认）

- **暂时不换唤醒词**，继续用「小陆同学」`wn9_xiaolutongxue`。
- A / B（乐鑫定制）现阶段无能力，搁置。
- C（MultiNet 自定义）列为**后续可选**，需要时再开。

## 待你拍板（已关闭 · 见上）

~~1. 目标唤醒词文案？ 2. A/B/C？ 3. 并存还是替换？~~
