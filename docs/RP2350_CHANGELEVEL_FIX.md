# 正常换关修复与发布验证（2026-09-29）

实机确认：前次对齐修复后，难度传送门已通过；进入章节传送门时出现
`HOST CHANGE LEVEL at host_cmd.c:362`。这是主动 Sys_Error，不是新的 HardFault。

修复：

- 删除 Host_Changelevel_f 中无条件的旧调试中断，继续调用现有 SV_SaveSpawnparms、SV_SpawnServer 和客户端重连流程。
- QMAC_GAME（Mac 完整引擎及 RP2350 完整引擎）允许换关时先显示加载画面。旧 WIN32 调试断言将这次合法更新误判为 `Screen updated before!`，新换关回归实际复现了这个后续问题。
- 保留其他错误检查，不批量删除 FIXME。资源损坏、实体溢出等检查仍然需要报错。

## 为什么需要改进工具

资源合法不代表引擎控制流正确。此前 ARM 的多地图测试用 `map` 重新启动各关，遗漏了玩家过关的 `changelevel`、保留玩家参数和重连路径。必须把包结构验证与游戏流程验证分开记录，不能把前者描述为完整游戏验证。

已有 QRN1 检查包括 CRC、镜像范围、模型类型、部分模型图引用和 Flash 分区边界。本次新增目录、模型数组、BSP 数据、纹理、碰撞和动画/精灵结构的原生地址对齐检查。损坏样本测试重新计算 CRC 后再验证，确保未对齐指针确实由结构检查发现。普通像素字节流不强加指针对齐要求。生成器 arm_native.py 已调用该校验，因此新检查自动进入资源生成流程；复用旧资源时 UF2 校验同样会执行它。

本次不改变 QRN1 格式、资源字节或 Flash 布局，无需重烧资源。

## 新增回归入口

`test_arm_firmware.py --episode-test --frames 100` 将初始位置移到章节门前，面向门并前进，随后执行原始触发器、QuakeC changelevel、命令队列、服务器和客户端代码。成功标准包含实际调用 qcc_changelevel，并在 e1m1 完成 signon。

`--changelevel-cycle --cycle 50 --frames 450` 用 changelevel 连续测试 start 和 e1m1–e1m8，共九张地图；地图必须完成客户端 signon。它与旧的 map 测试保留为不同路径。

发布工具新增显式运行门槛：

```sh
python3 Tools/RP2350Pack/build_game_firmware.py build/pak0.pak \
  -o build-host/rp2350-release \
  --engine-test-python build-host/arm-test-venv/bin/python
```

所选 Python 需要 unicorn 和 pyelftools。开启后运行难度门、章节门、九地图连续换关、按键回归；任一失败就终止，不生成新的成功报告。报告记录 engine_tested、各项结果及固件/资源哈希；未指定此选项时明确记录 engine_tested=false。硬件时序仍需实机验证。

后续应继续增加实际关卡出口、死亡重开、存读档能力边界和实体压力场景；不能仅凭当前测试宣称所有游戏路径已验证。
