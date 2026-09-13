# 20 — 固件接线 CONFIG_OTA_URL 指向自建云

Status: ready-for-agent
Upstream: rig-hover-backend 仓库 ADR-008（docs/adr/008-config-ota-url-firmware.md）

## 目标

Hover 固件 OTA 检查地址指向自建云 `https://robot-test.fozmoly.com/xiaolu/ota/`，通过已存在的 Kconfig 选项 `OTA_URL` 配置，不改共享代码默认行为（Puppy/Arm 零影响）。

## 改动清单

- [x] `main/ota.cc` `GetCheckVersionUrl()`：NVS 覆盖 → **CONFIG_OTA_URL（新增）** → 区域默认
- [x] `main/Kconfig.projbuild`：OTA_URL help 补充解析顺序说明与示例
- [x] `sdkconfig.defaults.esp32s3`：注释更新（此文件不写 URL，板级差异在烧录时配）
- [x] 烧录：Hover `CONFIG_OTA_URL=https://robot-test.fozmoly.com/xiaolu/ota/` 已 flash 到 `/dev/cu.usbmodem5C941459091`（MAC b8:1f:3f:ac:b9:68）

## 烧录操作（在 rig_omni 本机）

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3        # 若已生成过 sdkconfig 可跳过
idf.py menuconfig
# RIG-Omni → Board Type → RIG-Hover
# RIG-Omni → OTA URL override → https://robot-test.fozmoly.com/xiaolu/ota/
idf.py fullclean && idf.py build
idf.py -p /dev/cu.usbmodem5C941459091 flash monitor
```

（板型/日志等级等沿用 .scratch/self-hosted-gateway/issues/02 的烧录审核清单）

## 验收

- [ ] P0 云端上线后：真机开机串口 OTA 检查命中 `robot-test.fozmoly.com` 且返回 200（等设备连上 WiFi 后看串口）
- [ ] 云端未上线时：设备 OTA 失败不影响正常开机运行（联网、表情、LAN 控制均正常）
- [ ] Puppy/Arm 构建（OTA_URL 留空）行为与改动前完全一致
