# start 地图传送门冻结排查（2026-09-28）

实机已成功启动游戏。用户报告进入 EASY 难度走廊后的传送门时画面、转向和射击全部停止，声音继续。这个门在 start 地图内传送到章节选择大厅，不会执行 changelevel。

早期排查时尚未确定根因。原发布 ELF 在 Cortex-M33 指令仿真中通过 EASY 门：执行原始传送触发、两次粒子效果、到达目标，继续运行 60 帧；100 ms 模拟计时步长也通过。仿真替换了外围服务，不能证明真实双核、USB、LCD、DMA 或异常行为正确。

诊断固件增加：

- core 0 HardFault 使用独立 1 KiB 紧急栈，记录 PC、LR、SP、CFSR、HFSR、BFAR、MMFAR；进入紧急栈前清除 MSPLIM。
- Sys_Error 先发布错误文本，再输出 USB 日志。
- core 1 在服务循环、逐行显示和等待混音锁时检查错误，绕过 framebuffer 所有权将错误直接写到 LCD。
- 首次提交画面后，超过 15 秒没有新的画面时显示 `NO FRAME FOR 15 SECONDS`。这表示超时，不能单独证明死锁；特别慢的加载也可能触发。该诊断屏会停下外围服务，需要重启。

只需更新诊断主程序 UF2，QRN1 资源文件和 Flash 分区保持兼容。再次重现后记录整张错误屏。PC 地址必须使用对应诊断 ELF 解析，不能使用之前发布的 ELF。

测试命令：

```sh
build-host/arm-test-venv/bin/python Tools/RP2350Pack/test_arm_firmware.py \
  build-host/full-firmware/game/quake_rp2350.elf \
  build-host/rp2350-game-assets/quake-native.qrn --teleport-test easy --frames 60

build-host/arm-test-venv/bin/python Tools/RP2350Pack/test_arm_firmware.py \
  build-host/full-firmware/game/quake_rp2350.elf \
  build-host/rp2350-game-assets/quake-native.qrn --fault-test
```

`--teleport-test` 只调整玩家初始位置到相应难度门前，并发送前进指令；原始游戏逻辑完成传送。`--fault-test` 注入已知异常栈帧，验证异常入口记录 PC/LR/SP，并验证 LCD 诊断渲染产生 200 行 RGB565 数据。实际外围显示仍待实机验证。

## 实机故障定位与修复

用户上传诊断屏：PC=0x1005aaf4、CFSR=0x01000000、HFSR=0x40000000。
该 PC 对应当时诊断 ELF 的 `R_DrawSolidClippedSubmodelPolygons` 中的
`strd r0, r1, [r1, #-8]`。CFSR 指明 UNALIGNED；BFAR/MMFAR 有效位未置位，屏上这两个值不能作为出错地址。

该 ELF 中 `textureCacheBuffer=0x2005d057`。MG24 会复用这块字节数组保存
`mvertex_t` 和 `bedge_t`，但移植定义缺少显式对齐。裁剪边存储从该地址后移
6000 字节开始，仍然未按 4 字节对齐，STRD 因此触发硬件异常。旧仿真没有检查这种硬件对齐要求，故曾错误地通过这条路径。

修复为所有纹理临时缓冲定义和公共声明添加 `_Alignas(8)`，覆盖 Mac 原生指针和 ARM
指针布局，不增加缓冲容量，不改变 QRN1 资源 ABI。ARM 验证脚本增加 ELF 符号对齐检查，
旧诊断 ELF 确实被检查拒绝。新增模型裁剪调用计数，确认回归实际执行相关渲染函数。

修复版保留 HardFault/Sys_Error 屏幕，移除诊断版的 15 秒无画面停止机制，避免正常较慢加载被当作错误。资源 UF2 无需更新。修复版尚需用户在实机重走传送门确认。
