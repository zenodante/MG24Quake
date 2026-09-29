# 移植文档导航

当前实现入口：

- [RP2350 完整固件：编译、分区、键位及测试](../platform/rp2350/game/README.md)
- [固件尺寸、内存和验证结果](RP2350_FULL_FIRMWARE_RESULTS.md)
- [Mac SDL 完整游戏与资源预处理](../platform/macos/README.md)

设计及历史测量：

- [QXIP / QLV 中间资源格式](QXIP_RESOURCE_FORMAT.md)：最终 RP2350 镜像为 QRN1。
- [Flash 固定化分配审计](FLASH_IMMUTABILITY_AUDIT.md)：实现前的数据及优化依据。
- [早期 RP2350 硬件诊断](../platform/rp2350/README.md)：独立 bring-up 目标。
- [仅供链接尺寸测量的工程](../platform/rp2350/size_audit/README.md)：不能烧录当游戏运行。

完整游戏使用 `quake_rp2350` + QRN1 资源；`quake_rp2350_bringup` 与旧 QXIP 资源仅用于历史诊断。
