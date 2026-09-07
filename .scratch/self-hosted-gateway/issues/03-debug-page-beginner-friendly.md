# 03 — Debug 页小白友好化（Step 3 热身）

Status: in-progress  
Step: 3 / 6（练「改代码→烧录→验证」闭环）  
Created: 2026-09-07

## 目标

把 `boards/hover/hover_debug_server.cc` 的 `INDEX_HTML` 改成小白友好版：

1. **全中文界面**：标签、说明、按钮中文化
2. **每个参数带一句话人话说明** + 建议范围
3. **危险参数加锁**：LQR_k0~k3、PID 参数默认锁定，需勾选「我知道我在做什么」解锁
4. **修掉官方页面的标注 bug**：第 9 格标着 `YAW_kp` 实际写的是 `imu_zero`（源码 case 8）
5. **安全范围**：输入框限制合理范围，防手滑输爆
6. 保留自动刷新（500ms）与原 API（`/api/data`、`/api/set`）

## 不改的部分（明确边界）

- 只改 `hover_debug_server.cc` 里的 HTML 字符串与前端 JS
- **不动** `/api/set` 的 case 映射（保持协议兼容）
- **不动** `xgo.cc` 控制逻辑
- 只涉及 `boards/hover/`，符合板级隔离

## 参数对照表（已从源码核实）

| 索引 | 页面旧标 | 实际写入 | 人话 | 默认值 | 建议范围 |
|------|----------|----------|------|--------|----------|
| 0 | head | target_head_pos | 头部舵机目标角度 | 0 | ±90 |
| 1 | delta_pos | stable_pos += v | 前后位置增量 | 0 | ±0.5 小步 |
| 2 | POS_kp | pid_pos.fpKp | 位置环 P | 60 | 别动 |
| 3 | POS_kd | pid_pos.fpKd | 位置环 D | 0 | 别动 |
| 4 | VEL_kp | pid_vel.fpKp | 速度环 P | 0.15 | 别动 |
| 5 | VEL_ki | pid_vel.fpKi | 速度环 I | 0.01 | 别动 |
| 6 | PIT_kp | pid_pit.fpKp | 俯仰环 P | 21 | 别动 |
| 7 | PIT_kd | kd_pit | 俯仰阻尼 | 0.7 | 别动 |
| 8 | ~~YAW_kp~~ | **imu_zero** | IMU 零点 | 0 | ⭐ 唯一常调，±2 微调 |
| 9 | delta_yaw | stable_yaw += v | 原地转向增量 | 0 | ±10 |
| 10-13 | LQR_k0~3 | lqr_k[0..3] | 平衡总增益 | 1400/6.8/32/1.6 | ❌ 锁定 |
| 14-19 | var14~19 | 无（保留） | — | — | 移除显示 |

## 完成标准

- [ ] 浏览器打开页面为中文 + 分组（IMU / 安全区 / 危险区）
- [ ] imu_zero 标注正确且可调
- [ ] LQR/PID 默认锁定，解锁开关有效
- [ ] 编译烧录后页面正常拉取 IMU 数据（500ms 刷新）
- [ ] `/api/set` 行为与原版一致（case 映射未变）

## 步骤

1. 重写 `INDEX_HTML`（中文 + 分组 + 锁 + 范围校验）
2. `idf.py build` 增量编译
3. `idf.py -p /dev/cu.usbmodem5C941459091 flash monitor`
4. 浏览器验证 + 手扶实测 imu_zero 调整
