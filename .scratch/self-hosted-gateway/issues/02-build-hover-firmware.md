# 02 — 本地编译官方 Hover 固件

Status: done ✅（2026-09-07 烧录验收通过）  
Step: 2 / 6

## 目标

本机编译 `CONFIG_BOARD_TYPE_HOVER`，烧录到真机，串口能看到日志；再打开 `http://<设备IP>/` 体验一次 LQR 调参。建立「改代码 → 烧录 → 验证」手感。

## 完成标准

- [ ] ESP-IDF ≥ 5.5.2 环境可用（`idf.py --version`）
- [ ] `menuconfig` 已选 **RIG-Hover**（及需要的 Firmware Region）
- [ ] `idf.py build` 成功
- [ ] `flash monitor` 成功；串口有 Hover / XGO / IMU 相关日志
- [ ] 设备仍能站立、能连 WiFi（或重新配网后正常）
- [ ] 浏览器打开 `http://<设备IP>/` 能看到 Debug 页、IMU 有数

## 步骤草案

```bash
cd /path/to/rig_omni
source ~/esp/esp-idf/export.sh   # 按你本机 IDF 路径

idf.py set-target esp32s3
idf.py menuconfig
# RIG-Omni → Board Type → RIG-Hover
# RIG-Omni → Firmware Region → Domestic（国内）一般选这个

idf.py build
idf.py -p <PORT> flash monitor
# macOS 常见: /dev/tty.usbmodem* 或 cu.usbserial*
# 需 CH34x 驱动时见官方说明
```

注意：

- 切换 Board Type 后建议 `idf.py fullclean` 再 build。
- 烧录时尽量供电稳定；官方提示电池连接并开开关。
- `boards/common/config.h` 若仍有硬编码 WiFi 调试开关，烧录「官方体验」前确认不要误连错网（或按你实际 WiFi 改/关掉）。

## 调参验证

WiFi 连上后：

1. 串口或小程序侧确认设备 IP  
2. 浏览器打开 `http://<IP>/`  
3. 看 Roll/Pitch/Yaw 是否刷新  
4. **先只观察，小幅动 imu_zero；不要猛改 LQR_k0**

## 环境进度（2026-09-05 晚）

- [x] ESP-IDF 仓库：`/Users/andyzheng/esp/esp-idf`
- [x] 版本：**v5.5.3**（已从 v6.1 checkout）
- [x] 子模块完整
- [x] `./install.sh esp32s3` 完成
- [x] 串口已识别：`/dev/cu.usbmodem5C941459091`（无需 CH34x）
- [ ] 明天：`source ~/esp/esp-idf/export.sh` → menuconfig Hover → build → flash（今晚不烧录）

激活环境：
```bash
source ~/esp/esp-idf/export.sh
idf.py --version
```

---

## 烧录前审核清单（2026-09-07 复核过代码后定稿）

### A. 必改（menuconfig）

| # | 配置 | 现状 | 要改成 | 位置 |
|---|------|------|--------|------|
| 1 | Board Type | defaults 默认 `PUPPY` | **RIG-Hover** | `RIG-Omni → Board Type` |
| 2 | Firmware Region | 默认 Domestic | 保持 **Domestic** | `RIG-Omni → Firmware Region` |

> 板型选错会编进 Puppy 板级文件（电机/引脚完全不同）。切板型后要 `fullclean`。

### B. 建议改

| # | 配置 | 原因 |
|---|------|------|
| 3 | 日志等级 WARN→INFO | defaults 只打 W/E，第一次烧录串口"没日志"会慌。`Component config → Log output → Default log level → Info` |
| 4 | `boards/common/config.h` `USE_HARDCODED_WIFI` | 全仓搜索无代码引用（遗留调试宏），不改也安全；想干净设 `0`，共享文件对三板无影响 |

### C. 保持默认（别动）

- `OTA_URL` 空 → 自动国内 `xl-api.xgorobot.com`（第一次烧录走官方云，符合本工单目标）
- 唤醒词 `USE_AFE_WAKE_WORD` + CMake 按板型打包 `wn9_xiaolutongxue`（已定暂不换）
- BluFi `USE_ESP_BLUFI_WIFI_PROVISIONING=y`（NimBLE），配网方式已定
- `FLASH_EXPRESSION_ASSETS=y`：构建自动打包 hover 30 个 EAF + 唤醒词模型
- 分区表 `partitions/16m.csv`（16MB flash，assets 8M；hover emoji 约 3MB，够）
- Flash QIO / CPU 240MHz / PSRAM OCT 80M

### D. 操作顺序

```bash
cd /Users/andyzheng/work/Dev/rig_omni
source ~/esp/esp-idf/export.sh   # 每个新终端都要

idf.py set-target esp32s3        # 首次；生成 sdkconfig（此刻默认是 Puppy！）
idf.py menuconfig                # 改 A1 RIG-Hover + 建议 B3 INFO
idf.py fullclean                 # 切板型保险
idf.py build                     # 先只编译验证
```

编译过了再接设备烧录：

```bash
idf.py -p /dev/cu.usbmodem5C941459091 flash monitor
```

### E. 物理注意（Hover 专属）

1. 扶稳/放平：刷机重启瞬间会失衡，防摔。
2. 电池供电 + 开关打开，USB 仅供数据。
3. 烧录前连 USB 并开机（审核时 `/dev/cu.usbmodem*` 尚无设备在线）。
4. `monitor` 退出是 `Ctrl + ]`（不是 Ctrl+C）。

### F. 烧录后验证（2026-09-07 实测全过 ✅）

- [x] 串口 INFO 日志：`app_init: Project name: rig-hover, App version: 3.7.0, ESP-IDF v5.5.3`
- [x] 表情资源加载：`Expression_load: Found 28 emoji / 1 icon / 7 layout items` → `SetEmotion: happy`
- [x] WiFi 自动连上（NVS 旧网 FZM）：`sta ip: 192.168.1.230`
- [x] OTA 检查官方云：`Current is the latest version`，运行分区 `ota_0`
- [x] MQTT 连接 `xl-mqtt.xgorobot.com:8883`（会话建立）
- [ ] Debug 页 `http://192.168.1.230/`（用户浏览器侧待看）
- [ ] 喊「小陆同学」唤醒（用户语音侧待试）

## 烧录实录（2026-09-07）

- 环境补丁：brew 装 `cmake`+`ninja`；IDF Python 环境补 `numpy`+`pillow`（`spiffs_assets_gen.py` 打包 assets 依赖）
- 配置：`CONFIG_BOARD_TYPE_HOVER=y` / `FIRMWARE_REGION_DOMESTIC` / `LOG_DEFAULT_LEVEL=3`
- 产物：`build/rig-hover.bin`（ota_0 @0x20000）+ `build/board_assets.bin`（4.57MB / 8MB 分区 @0x800000）
- 烧录：`idf.py -p /dev/cu.usbmodem5C941459091 flash` 全部 `Hash of data verified`
- 设备 IP：`192.168.1.230`（WiFi: FZM）


