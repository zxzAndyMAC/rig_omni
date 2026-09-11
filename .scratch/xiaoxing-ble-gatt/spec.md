# Hover 已联网 BLE GATT

Status: ready-for-agent

## Problem Statement

Hover 配网成功后会重启并连上 Wi-Fi，BluFi 关掉。浣课「我的小星」需要在人靠近时用蓝牙读状态、调音量、预览拍照。今天联网后没有对应 GATT。

## Solution

Wi-Fi 已连接且不在配网模式时，用原名 `RIG-Hover` + MAC 后两字节广播新服务。App 写 JSON 方法名（与现有 MCP 相同），固件调已有工具，Notify 返回 JSON。配网仍只用 BluFi。烧录到试验机前必须等人确认。

## User Stories

1. As an App, I want 已联网的 Hover 可被扫到且服务不是配网那个, so that 不会误进 BluFi
2. As an App, I want `get_device_status` 经蓝牙返回与 MCP 相同的状态对象, so that 能画设备页
3. As an App, I want `set_volume` 经蓝牙生效, so that 滑条有反馈
4. As an App, I want 亮度、主题、表情、转头、平移、旋转、预览、重启同样经蓝牙调用现有工具, so that 试验期能对通路
5. As a 固件维护者, I want 配网模式仍只开 BluFi, so that 激活小星流程不被新服务抢走
6. As a 试验机主人, I want 镜像烧进去之前被口头确认, so that 不会在不知情时被刷

## Implementation Decisions

- 服务 `0xFFA0`，写 `0xFFA1`，通知 `0xFFA2`。
- JSON：`id` + `method` + `params` → `ok` + `result` 或 `error`。
- Write 投递到现有 MCP 主线程调度，不在 BLE 回调里直接动舵机或摄像头。
- 仅 Hover 板；不改 Puppy/Arm 专属文件。
- 方法清单与浣课 spec「我的小星」一致。

## Testing Decisions

对外：用手机或 nRF 连上后，发 `get_device_status` 能收到带音量与 Wi-Fi 的 JSON；`set_volume` 后再次读取音量变化；`preview` 后屏幕出图。不测 NimBLE 句柄。

## Out of Scope

传图、云识别、改广播名前缀、其它板型、自动烧录。

## Further Notes

完整产品约定在浣课仓库 `.scratch/xiaoxing-ble-device-page/spec.md`。烧录前停住。
