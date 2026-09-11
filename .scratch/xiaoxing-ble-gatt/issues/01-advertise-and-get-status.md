# 01: 联网后能被扫到并读出状态

**What to build:** Hover 连上家里 Wi-Fi 之后，手机用新服务能扫到 `RIG-Hoverxxxx`，发出读状态请求后能收到音量、Wi-Fi 名等字段。配网模式行为不变。

**Blocked by:** None (can start immediately)

**Status:** claimed

- [ ] 已联网、非配网时广播名仍为 `RIG-Hover` + MAC 后两字节，服务不是配网那个
- [ ] 写一条读状态的 JSON，Notify 回来的对象里能看到音量与 Wi-Fi 相关字段
- [ ] 处于配网模式时仍只有 BluFi，不抢发现
- [ ] 只动 Hover 共用联网路径，不改 Puppy/Arm 专属文件
- [ ] 烧录到真机前停住，等人确认

## Comments

- 代码已写：Hover 在 STA 连上后启动 `0xFFA0` GATT，JSON `self.get_device_status`。尚未编译烧录。**烧录前等确认。**
