# 02 — 本地编译官方 Hover 固件

Status: ready-for-human  
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

