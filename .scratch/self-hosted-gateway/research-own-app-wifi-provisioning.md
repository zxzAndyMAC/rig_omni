# Research: 换成自己的 App 做 WiFi 配网

Updated: 2026-09-06  
Primary sources: `main/boards/common/blufi.cpp`, `wifi_board.cc`, `Kconfig.projbuild`, `sdkconfig.defaults.esp32s3`, Espressif BluFi docs / EspBlufi apps

## 产品决策（2026-09-06 已确认）

- **继续 BluFi**；不切 SoftAP。
- **不要二维码**：App 直接 BLE 扫描 `RIG-Hover*` 即可。
- **回头改 UI**：把默认 `wificonfig` 小程序码换成其他图标/文案提示（仅 assets / EAF，协议不动）。

## 结论（先看这个）

官方路径是 **屏上静态「入口码」→ 微信小程序 → BluFi（BLE）写 WiFi**。  
自有 App：**真正要兼容的是 BluFi**；二维码可选、已决定不做。设备身份靠蓝牙名，配网成功后还会经 BluFi 自定义帧回传 **STA MAC（6 字节）**。

当前 Hover 默认固件已开 BluFi：

```
sdkconfig.defaults.esp32s3:
  CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING=y
  CONFIG_USE_HOTSPOT_WIFI_PROVISIONING=n
```

## 端到端链路（现状）

```
无 SSID（或连接超时）
  → WifiBoard::StartWifiConfigMode()
  → 屏设 emotion "wificonfig"（静态小程序码画面）
  → Blufi::init()
  → BLE 广播名 = BOARD_TYPE + BT MAC[4..5] hex
       Hover → "RIG-Hover" + 如 "A1B2" → "RIG-HoverA1B2"
  → 手机 BluFi 客户端连接 → DH/AES 协商 → 拉 WiFi 列表（最多 10 个，按 RSSI）
  → 下发 SSID/密码 → 设备连 AP → 成功后:
       · esp_blufi_send_wifi_conn_report(SUCCESS)
       · esp_blufi_send_custom_data(sta_mac, 6)   ← App 可用来绑定设备
       · OnWifiConfigEnd() → 延迟 5s → esp_restart()
```

触发入口：`wifi_board.cc`（无凭据开机 / 连接超时 / 用户主动进配网）。

## 屏幕上的「码」到底干什么

| 层 | 作用 |
|----|------|
| `wificonfig.eaf` | 仅 UI：国内/海外两张**静态图**之一，构建时拷贝 |
| 微信小程序码像素 | 打开「小陆同学」小程序（appId/path/scene 在图里，仓库无明文） |
| BLE 名 `RIG-HoverXXXX` | **设备发现与配对身份** |
| BluFi custom data | 配网成功后回传 **WiFi STA MAC**，供云/账号绑定 |

因此：换自己的码 = 换一张指向你 App 的二维码（Universal Link / 自定义 scheme / 应用商店落地页）。**不换 App 里的 BluFi 实现，只换图 → 配不成网。**

## App 必须实现什么（BluFi 路径，推荐）

协议是乐鑫标准 BluFi，不是陆吾私有：

| 项 | 值 |
|----|-----|
| GATT Service | `0xFFFF` |
| Phone → Device | `0xFF01`（write） |
| Device → Phone | `0xFF02`（read + notify） |
| 安全 | Diffie–Hellman + AES-CFB128（固件已接 mbedtls） |
| 过滤广播名 | 前缀 `RIG-Hover`（来自编译宏 `BOARD_TYPE`） |

典型 App 步骤（与 EspBlufi / 小程序逻辑一致）：

1. 扫 BLE，过滤 `name.startsWith("RIG-Hover")`
2. `connect` → 发现 Service/Characteristic
3. `negotiateSecurity`
4. `requestDeviceWifiList`（设备侧事件 `GET_WIFI_LIST`）
5. 用户选 2.4G SSID，输入密码（ESP32-S3 不支持 5G）
6. `configure`：Station 模式 + SSID + password → `REQ_CONNECT_TO_AP`
7. 等 `wifi_conn_report` success
8. 解析 **custom data 6 字节 = STA MAC**，写入你的用户-设备绑定
9. 设备约 5 秒后重启；App 提示扶住（Hover 会暂时失衡）

现成参考实现（可二开进你的 App）：

- Android: [EspressifApp/EspBlufiForAndroid](https://github.com/EspressifApp/EspBlufiForAndroid)
- iOS: [EspressifApp/EspBlufiForiOS](https://github.com/EspressifApp/EspBlufiForiOS)
- 文档侧曾描述 uni-app 小程序 `utils/blufi.js`（本仓库无源码，仅有 `docs/content/Mobile Web Interface/` 说明）

官方协议说明：  
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ble/blufi.html

### 固件侧已处理的事件（App 可依赖）

见 `blufi.cpp` `_handle_event`：

- BLE connect/disconnect、adv start/stop
- `SET_WIFI_OPMODE` / `RECV_STA_SSID` / `RECV_STA_PASSWD` / `RECV_STA_BSSID`
- `REQ_CONNECT_TO_AP`（写 NVS `SsidManager`，直连，15s 超时）
- `GET_WIFI_LIST` / `GET_WIFI_STATUS`
- 成功：`send_wifi_conn_report` + `send_custom_data(sta_mac, 6)` + reboot

## 三条配网路线对比（做自己的 App 时怎么选）

Kconfig `WiFi Configuration Method`（互斥主路径 + 可叠加声学）：

| 方案 | 固件改动 | App 复杂度 | UX | 适用 |
|------|----------|------------|-----|------|
| **A. 继续 BluFi** | 几乎零（换表情 QR 图可选） | 中：嵌 EspBlufi SDK | 接近官方小程序 | **推荐默认** |
| **B. SoftAP 热点** | menuconfig 切 `USE_HOTSPOT`；与 BluFi **不能同时** | 低：连设备热点 + HTTP 表单/API | 需切 WiFi，易踩坑 | 想避开 BLE 时 |
| **C. 声学 AFSK** | 可与 BluFi 并行开 | 需播编码音频 | 差，调试用 | 不推荐产品化 |

SoftAP 路径（`wifi_board.cc`）：`StartConfigAp()` → 提示连接热点名 + 浏览器打开 `GetApWebUrl()`。BluFi init 时若已在 config AP 模式会直接失败（代码显式禁止并存）。

## 二维码设计建议（自有 App）

最小可用：

```
https://your.app/provision
  ?product=hover
```

打开 App 后进入「附近设备」页，再按 BLE 名扫描。  
**不必**把 MAC 编进二维码（屏上是静态图，编不进每台设备）；若要坚持「一机一码」，要改固件在运行时用 LCD 画动态 QR（工作量大，当前未做）。

进阶：码里带 `batch` / `channel` / 邀请码等业务参数；设备身份仍以 BLE + 成功后的 STA MAC 为准。

## 与自建网关的边界

| 能力 | 谁负责 |
|------|--------|
| 发现设备、写 WiFi | App ↔ 设备 BluFi（局域网/近场） |
| 账号绑定（MAC） | App 收 custom data → 你的后台 |
| 对话 ASR/LLM/TTS | 配网成功并 OTA/WS 指向你的网关之后 |

配网阶段**不依赖**陆吾云；换 App 配网与自建 gateway 正交，可并行。

## 建议落地顺序（若做成工单）

1. 用官方 EspBlufi App 对现网 Hover 验证 BLE 名与配网（证明协议兼容）
2. 在你的 App 嵌入 EspBlufi SDK，过滤 `RIG-Hover*`
3. 换 `wificonfig_*.eaf` 指向你的下载/唤起链接
4. 后台：用 custom data 的 STA MAC 做设备绑定
5. （可选）后期再考虑 SoftAP 或动态 QR

## 风险 / 注意

- 仅 2.4G WiFi
- 配网成功会 **重启**；Hover 需扶住
- BluFi 与 SoftAP 配网互斥
- iOS BLE 后台/权限、微信小程序 BLE 与原生 App BLE API 不同；原生 App 用系统 CoreBluetooth / Android BLE
- 蓝牙名长度：`BOARD_TYPE` + 4 hex ≤ 缓冲区 24
