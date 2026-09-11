# 02: 蓝牙能改音量

**What to build:** 在已联网 GATT 上发设音量请求后，再读状态，音量数字跟着变，喇叭实际响度变化。

**Blocked by:** 01 联网后能被扫到并读出状态

**Status:** ready-for-agent

- [ ] `set_volume` 合法范围 0–100 成功
- [ ] 随后 `get_device_status` 的音量与刚设置的一致
- [ ] 非法参数返回 `ok: false` 且不崩
- [ ] 烧录前停住，等人确认

## Comments
