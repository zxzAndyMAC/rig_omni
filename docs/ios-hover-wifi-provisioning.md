# iOS SwiftUI：识别 Hover 并 BluFi 配网

> **给谁用：** 在现有 iOS App 里加「发现 Hover + 配 Wi-Fi」的客户端工程师 / Agent。  
> **合同来源：** 本仓库固件 `main/boards/common/blufi.cpp`、`wifi_board.cc`、`hover/hover_board.cc`，以及当前 `sdkconfig`。  
> **不改固件。** App 必须迁就设备已实现的 Espressif **BluFi** 协议。  
> **不要自己实现 BluFi 帧。** 用乐鑫官方库 [EspBlufiForiOS](https://github.com/EspressifApp/EspBlufiForiOS)，SwiftUI 只做发现、流程和界面。

当前 Hover 固件：`CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING=y`，热点配网关闭。手机连设备热点、打开网页配网 **这条路不存在**。

---

## 0. 产品要做成什么样

用户第一次拿到 Hover（气垫船）时，机器人还没有家用 Wi-Fi。App 要：

1. 引导用户让机器人进入配网（广播 BLE）
2. 扫描并认出 **这台 Hover**（不是 Puppy / Arm，也不是别的 BLE 耳机）
3. 连上机器人 BLE
4. 拉机器人扫到的 Wi-Fi 列表
5. 用户选 SSID、输入密码
6. 下发凭证，等机器人连上路由器
7. 告诉用户成功；机器人约 5 秒后自己重启，之后走云端 OTA（见 `docs/cloud-backend-integration.md`）

iOS **读不到用户路由器密码**，必须手输。可选：用 `NEHotspotNetwork` 预填当前手机连着的 SSID（需要定位权限），密码仍手输。

---

## 1. 机器人何时才会被扫到

BluFi **只在配网模式**里 `esp_blufi_adv_start()`。平时对话、已连上 Wi-Fi 时，App **扫不到** Hover 配网广播。

### 1.1 进入配网的三种方式（写进 App 引导文案）

| 场景 | 设备行为 | 用户操作 |
|------|----------|----------|
| 出厂 / 从未配过网 | 开机约 1.5 s 后自动 `StartWifiConfigMode()` | 开机即可扫 |
| 开机连已知 Wi-Fi 超过 60 s 失败 | 停 Station，进入配网 | 等屏幕出现配网表情 |
| 开机过程中还没连上 | 单击 **Boot 键**（`kDeviceStateStarting && !IsConnected`） | 开机后立刻点一下 Boot |

屏幕会切到 `wificonfig` 表情并播 `OGG_WIFICONFIG`。这时 BLE 名称才会出现。

### 1.2 强制重新配网

Hover **Boot 或触摸长按 ≥ 3 秒**：擦 NVS 并重启。重启后没有已存 SSID，会再走自动配网。

App 文案建议：

> 请开机，等屏幕出现配网动画。若已连过网，请长按机身按键约 3 秒直到重启，再打开本页。

### 1.3 配网成功后

设备发成功报告 → 回调 `OnWifiConfigEnd()`（成功语音）→ **延迟 5 秒 `esp_restart()`**。  
App 收到成功后不要继续写特征，提示「机器人即将重启」，断开 BLE。

---

## 2. 怎么认出是 Hover

广播名在 `ESP_BLUFI_EVENT_INIT_FINISH` 里生成：

```text
名称 = BOARD_TYPE + MAC[4] + MAC[5]   （蓝牙 MAC，两位大写十六进制）
Hover 的 BOARD_TYPE = "RIG-Hover"
例子：RIG-HoverA1B2
```

`blufi_device_name` 缓冲区 24 字节。`RIG-Hover` + 4 hex = 13 字符，合法。

**扫描过滤（必须）：**

```swift
name.hasPrefix("RIG-Hover")
```

同协议的其它板：

| 前缀 | 产品 |
|------|------|
| `RIG-Hover` | 气垫船（本需求） |
| `RIG-Puppy` | 机器狗 |
| `RIG-Arm` | 机械臂 |
| `RIG-Bot` | Bot |

第一期只展示 Hover。同一房间多台 Hover 用后缀四位 hex 区分。

GATT 发现用服务 UUID，不靠名字：

| 角色 | UUID | 权限 |
|------|------|------|
| BluFi Service | `0xFFFF` | 主服务 |
| Phone → Device | `0xFF01` | Write / Write Without Response |
| Device → Phone | `0xFF02` | Notify（可读） |

扫描时过滤 `CBUUID(string: "FFFF")` 可减少无关设备；**展示前仍要用名称前缀确认是 Hover**（其它 ESP BluFi 示例板也会广播 `0xFFFF`）。

NimBLE `ATT MTU` 固件配了 256。连接后协商 MTU（EspBlufi 库会做）。

配对：固件 `sm_io_cap = 4`（NoInputNoOutput）。**不要弹配对码**，不要 Bond。

---

## 3. 端到端时序（App 必须按这个顺序）

```
用户让 Hover 进配网
        │
App 扫描 BLE  ──名称 RIG-Hoverxxxx──► 列表
        │ 用户点选
连接 GATT，发现 FFFF / FF01 / FF02
订阅 FF02 Notify
        │
EspBlufi 安全协商（DH → MD5 → AES-CFB128，CRC16）
        │ 协商成功前不要发 SSID
negotiateSecurity()
        │
requestDeviceScanWiFiList()     // GET_WIFI_LIST
        │ 设备最多等扫描 10 s，回最多 10 个 AP（按 RSSI 排序）
展示列表，用户选 SSID、输密码
        │
configure STA:
  - 可选 setOpMode(.sta)
  - send STA SSID
  - send STA Password
  - requestConnectSta()         // REQ_CONNECT_TO_AP
        │ 设备直连，超时 15 s
成功：wifiConnReport == STA_CONN_SUCCESS
      另有 custom data：STA MAC 6 字节
失败：STA_CONN_FAIL，BLE 仍连着，允许改密码重试
        │ 成功后设备 5 s 重启，广播消失
App 显示成功，disconnect
```

凭证发送顺序固件要求：先收到 `RECV_STA_SSID` 和 `RECV_STA_PASSWD`，再处理 `REQ_CONNECT_TO_AP`。EspBlufi 的 `configure` API 会按这个顺序发包。

连接成功时固件还会 `esp_blufi_send_custom_data(sta_mac, 6)`。App 应用这 6 字节作为设备 MAC，后续绑定云账号（OTA 的 `Device-Id` 实际是 STA/系统 MAC，与这 6 字节应对齐）。

---

## 4. SwiftUI 页面规格

建议独立 `Provisioning` 流程，不要塞进设置里的一个 Alert。

### 4.1 信息架构

```
ProvisioningRoot
 ├─ PermissionGate          蓝牙未授权
 ├─ HowToEnterPairing       引导开机 / 长按
 ├─ DeviceList              扫描到的 RIG-Hover*
 ├─ Connecting              连 BLE + 协商密钥
 ├─ WiFiPicker              机器人扫到的 AP
 ├─ PasswordForm            密码 + 显示/隐藏
 ├─ Applying                下发中，15 s 超时进度
 ├─ Success                 MAC、即将重启
 └─ Failure                 可重试，不退出到根
```

用 `NavigationStack` + 一个 `ProvisioningSession: ObservableObject` 驱动。

### 4.2 各页要点

**HowToEnterPairing**

- 插画：Hover 屏幕是配网二维码/动画
- 三条步骤：开机 → 等配网动画 → 点「开始扫描」
- 次要：「已经配过网？」→ 说明长按 3 秒重置
- 主按钮：开始扫描（先检查 `CBManagerState.poweredOn`）

**DeviceList**

- 每行：名称 `RIG-HoverA1B2`、RSSI 信号格、可选「距离近」排序
- 空态：还没看到机器人，倒计时「请靠近并确认屏幕是配网动画」
- 扫描指示：`ProgressView` + 「正在查找 Hover…」
- 点一行 → Connecting（禁止连两台）

**Connecting**

- 文案顺序：连接蓝牙 → 安全协商 → 获取附近 Wi-Fi
- 失败：超时 10 s 未连上 / 协商失败 → 回列表

**WiFiPicker**

- 列表来自 **机器人扫描结果**，不是 iPhone 的 Wi-Fi 列表
- 最多约 10 条，已按 RSSI 排序
- 行：SSID、锁图标（BluFi 列表只有 ssid+rssi，没有 authmode；有密码的家用网一律当需要密码）
- 「刷新」再次 `requestDeviceScanWiFiList`
- 「列表没有我的网」→ 手动输入 SSID（隐藏 SSID）
- 可选：预填 iPhone 当前 SSID（见 6.3）

**PasswordForm**

- SecureField + 显示明文开关
- 空密码：仅当用户明确选「开放网络」时允许
- 主按钮：连接到「某某」

**Applying**

- 不可返回（防重复下发）；提供「取消」只断开 BLE，不保证设备状态
- 15 s 内等 `STA_CONN_SUCCESS` 或 `STA_CONN_FAIL`

**Success**

- 「已连接到 {ssid}」
- 展示 MAC（custom data）
- 「机器人正在重启，大约 5 秒后可用」
- 完成后进入你 App 里的设备页 / 绑定云

**Failure**

- 密码错误 / 超时：留在密码页，不清 SSID
- BLE 断开且未成功：回 DeviceList，提示保持配网动画

### 4.3 状态机（`ProvisioningSession`）

```text
idle → scanning → connecting → negotiating → fetchingWifi
    → pickingWifi → applying → succeeded
                              ↘ failed (stay / retry)
任意步：bluetoothOff / unauthorized → PermissionGate
```

---

## 5. 推荐工程结构（嵌进现有 App）

不要在 Swift 里手写 DH/AES。乐鑫库是 **Objective-C**，用 bridging header 包一层。

```
App/
 ├─ Provisioning/
 │   ├─ ProvisioningRootView.swift
 │   ├─ Views/          Permission / HowTo / DeviceList / WiFi / Password / Result
 │   ├─ ProvisioningSession.swift      @MainActor ObservableObject
 │   ├─ HoverDevice.swift              id, name, rssi, peripheral
 │   ├─ BlufiClient.swift              对 EspBlufiClient 的 Swift 封装
 │   └─ BlufiClientDelegate.swift
 ├─ Bridging/
 │   └─ YourApp-Bridging-Header.h      #import "ESPBlufiClient.h" 等
 └─ Info.plist                         蓝牙（及可选定位）说明
Vendor/BlufiLibrary/                   EspBlufiForiOS 的 BlufiLibrary
```

`BlufiClient` 对外只暴露：

```swift
protocol BlufiClienting {
    func startScan()
    func stopScan()
    func connect(_ device: HoverDevice)
    func disconnect()
    func negotiateSecurity()
    func requestWiFiList()
    func provision(ssid: String, password: String)
}

enum ProvisioningEvent {
    case bluetoothState(CBManagerState)
    case devices([HoverDevice])
    case connected
    case securityDone
    case wifiList([(ssid: String, rssi: Int)])
    case applying
    case success(staMAC: Data, ssid: String)
    case failure(ProvisioningError)
}
```

`ProvisioningSession` 订阅 `ProvisioningEvent`，只改 UI 状态。

### 5.1 与 EspBlufi 的调用对应

| 你的方法 | 库（概念名，以仓库头文件为准） |
|----------|--------------------------------|
| connect | `ESPBlufiClient` 连上 `CBPeripheral` |
| negotiateSecurity | `negotiateSecurity` |
| requestWiFiList | `requestDeviceWifiScan` / `requestDeviceStatus` 同类 API |
| 发 SSID | `configure` 里 STA SSID |
| 发密码 | STA Password |
| 连接 AP | `requestConnectSta` 或 configure 的 connect 标志 |
| 成功 | delegate：`wifiConnectionReport` staConnSuccess |
| STA MAC | delegate：`gattNotification` / `customData` 长度 6 |

接入时打开 [EspBlufiForiOS](https://github.com/EspressifApp/EspBlufiForiOS) 的 `ESPBlufiClient.h`、`ESPBlufiDelegate.h`，以头文件方法名为准；上面是职责映射，不要凭记忆点方法。

扫描：可以用 `CBCentralManager.scanForPeripherals(withServices: [CBUUID(string: "FFFF")])`，也可以让 EspBlufi 自带 scanner 再在回调里过滤 `RIG-Hover`。

---

## 6. iOS 系统能力

### 6.1 Info.plist（必须）

```xml
<key>NSBluetoothAlwaysUsageDescription</key>
<string>需要蓝牙来发现并给 Hover 机器人配置家庭 Wi-Fi</string>
<key>NSBluetoothPeripheralUsageDescription</key>
<string>需要蓝牙来发现并给 Hover 机器人配置家庭 Wi-Fi</string>
```

iOS 13+ 以 `NSBluetoothAlwaysUsageDescription` 为准，旧系统还要 Peripheral 那条。

### 6.2 不需要的能力

- 不需要 Wireless Accessory Configuration（那是 MFi/WAC）
- 不需要把 App 做成 Wi-Fi 热点
- 不需要 Background Modes → Bluetooth-central（前台配网即可）。若以后要后台扫描，必须带服务 UUID 扫，且体验会差，第一期不要做

### 6.3 预填当前 SSID（可选）

iOS 14+：`NEHotspotNetwork.fetchCurrent` 需要：

- `NSLocationWhenInUseUsageDescription`
- 能力：Access Wi-Fi Information
- 用户在定位授权为「使用期间」

拿不到就当没有，密码框留空。**永远不要**试图读钥匙串里的 Wi-Fi 密码。

### 6.4 真机

CoreBluetooth 配网必须 **真机**。模拟器没有 BLE。

---

## 7. 错误与超时（按固件行为）

| 现象 | 固件侧 | App |
|------|--------|-----|
| 扫不到 | 未进配网 / 已连 Wi-Fi 停广播 | 引导开机或长按重置 |
| 连上又断、未成功 | `BLE_DISCONNECT` 且 `!m_provisioned` → 重新广播 | 回列表，可再连 |
| 协商失败 | `REPORT_ERROR` + DH/AES 错误码 | 断开重连，不要继续发 SSID |
| Wi-Fi 列表空 | 扫描 0 个 AP 或超时仍空 | 允许手动输入 SSID，提示靠近路由器 |
| 列表最多 10 个 | `_send_wifi_list` 截断 | UI 注明「显示信号最强的 10 个」 |
| 连路由超时 | 15 s 未拿到 IP → `STA_CONN_FAIL` | 密码页可改密重试，BLE 应仍在 |
| 成功 | `STA_CONN_SUCCESS` + custom 6 字节 MAC，5 s 后重启 | 立刻进 Success，主动 disconnect |
| 开放网络 | 固件仍设 `WIFI_AUTH_WPA2_PSK` 阈值 | 开放热点可能失败；家用 WPA2/WPA3 为主 |

固件连接时写死 `threshold.authmode = WIFI_AUTH_WPA2_PSK`。企业网 / 纯开放网络不作为第一期承诺。

---

## 8. SwiftUI 交互细则（避免踩坑）

1. **扫描与连接互斥。** 连接前 `stopScan()`。
2. **单连接。** `CBCentralManager` 同时只连一台 Hover。
3. **主线程更新 UI。** BLE 回调在后台队列，`ProvisioningSession` 标 `@MainActor`，事件用 `await MainActor.run`。
4. **不要在 `onAppear` 里反复 `scan` 而不 `stop`。** 用 `.task` + `onDisappear` 停扫。
5. **密码页提交后 disable 按钮**，直到 success/fail，防止双击发两次 `REQ_CONNECT_TO_AP`。
6. **成功页倒计时 5 秒** 与设备重启对齐，倒计时结束再「完成」。
7. **中文 SSID** 按 UTF-8 发给库；固件 `strncpy` 进 32 字节 SSID 缓冲，超长会截断。
8. **定位权限被拒** 只影响预填 SSID，不影响 BluFi。
9. **蓝牙开关关闭** 停在 PermissionGate，不要假装在扫。
10. **App 进后台：** 系统可能杀掉未完成的 BLE 连接。进后台时若正在 applying，回来后检查状态；未成功则提示重来。

---

## 9. 和云端文档的衔接

配网成功 ≠ 已绑定你的云。重启后设备会：

1. 用刚写入 NVS 的 SSID 连路由器
2. `POST` OTA URL（见 `docs/cloud-backend-integration.md`）
3. 拿到 `websocket.url` 才能语音

App 成功页之后的下一步（本需求范围外，但不要设计冲突）：

- 用 custom data 的 MAC 当 `Device-Id` 调你的业务绑定接口
- 等设备上线（OTA 成功）再进对话页

配网流程 **不要** 在 BLE 里下发 OTA URL。本固件 BluFi 只收 SSID/密码，没有「自定义云地址」字段。改云地址是固件/NVS 的事，不在这个 App 流程里。

---

## 10. 第一期范围 / 明确不做

**做：**

- 前台扫描 `RIG-Hover*`
- EspBlufi 协商 + 拉 Wi-Fi 列表 + 下发 STA + 成功/失败 UI
- 引导进入配网、长按重置说明
- 真机验收

**不做（第一期）：**

- 自己实现 BluFi 加密帧
- 热点网页配网
- 声波配网
- 同时配 Puppy/Arm（过滤即可，不必做产品选择）
- 后台持续扫描
- 从 iOS 读取 Wi-Fi 密码
- 在配网 BLE 上做 MCP / 语音

---

## 11. 建议实现顺序（现有 App 里加模块）

1. 把 `BlufiLibrary` 拖进工程，bridging header 能 `import` 客户端类，空页面能编译  
2. `CBCentralManager` 扫描，过滤 `RIG-Hover`，真机对着进配网的 Hover 能出现一行  
3. 连接 + 打印服务/特征，确认 `FFFF/FF01/FF02`  
4. `negotiateSecurity` 成功  
5. 拉 Wi-Fi 列表并做 `WiFiPicker`  
6. 下发家用 SSID/密码，收到 success + 6 字节 MAC，设备重启  
7. 打磨引导、失败重试、权限文案  

完成标准：第一次开箱的 Hover，只靠 App + Boot 键/长按，能连上指定路由器并重启。

---

## 12. 源码对照

| 行为 | 位置 |
|------|------|
| 配网方式 = BluFi | `sdkconfig` `CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING` |
| 广播名 `RIG-Hover` + MAC 后两字节 | `main/boards/common/blufi.cpp` `INIT_FINISH` |
| 无 SSID 自动进配网 | `wifi_board.cc` `TryWifiConnect()` |
| 开机单击 Boot 进配网 | `hover_board.cc` `boot_button_.OnClick` |
| 长按 3 s 擦 NVS | `hover_board.cc` `kLongPressResetMs` |
| 连上后停广播、协商、扫 Wi-Fi | `blufi.cpp` `BLE_CONNECT` |
| 最多 10 个 AP | `blufi.cpp` `_send_wifi_list` |
| SSID/密码/请求连接 | `RECV_STA_SSID` / `PASSWD` / `REQ_CONNECT_TO_AP` |
| 连路由超时 15 s | `blufi_wifi_conn` 任务 |
| 成功回报告 + STA MAC + 5 s 重启 | `STA_CONN_SUCCESS` 分支 |
| GATT UUID | ESP-IDF BluFi：Service `0xFFFF`，`0xFF01` / `0xFF02` |
| 官方 iOS 库 | https://github.com/EspressifApp/EspBlufiForiOS |

上游协议说明：https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/api-guides/ble/blufi.html  
与本文冲突时，以本仓库 `blufi.cpp` 为准（例如成功后必重启、custom data 为 6 字节 MAC）。
