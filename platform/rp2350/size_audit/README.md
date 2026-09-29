# 完整游戏 ARM Flash 尺寸审计（不是可烧录固件）

使用 pico8c 中的 ARM GCC 15.3.1 和 Pico SDK 2.3.1，将当前 Mac 已运行的
MG24 完整游戏 C 路径交叉编译为 Cortex-M33/Thumb、SDK 的 softfp 调用 ABI，
再链接真实的 RP2350 core1 LCD/DMA、PWM 音频、I2C 输入和 USB stdio 服务。
SRAM 对象使用 ARM 原生 32 位指针。没有使用桌面 ELF 大小推算，也没有把诊断
固件的 78 KB 当成完整游戏体积。

## 重现

在仓库根目录执行：

```sh
python3 Tools/RP2350Pack/firmware_size_audit.py \
  --pico8c-root ../pico8c -o build-host/rp2350-firmware-size
```

需要 CMake、Ninja，以及 pico8c 下的 SDK/ARM GCC。本工具不下载依赖、不生成
UF2、不烧录、不修改现有 Flash 分区。结果为 report.json、分模块编译日志、
源文件哈希、链接命令、map 和标为 SIZE_ONLY_DO_NOT_FLASH 的 ELF/bin。

## 2026-09-28 实测

| 引擎/库配置 | Flash 装载字节 | KiB |
|---|---:|---:|
| -Os + 默认 newlib | 670,424 | 654.71 |
| -Os + LTO + 默认 newlib | 643,888 | 628.80 |
| -Os + LTO + newlib-nano | 592,960 | 579.06 |

nano 配置重新编译 SDK 和引擎，并保留 `_printf_float`、`_scanf_float`，没有靠
关闭浮点输入输出取得小体积。板级源码沿用现有 SDK/工程优化设置，整个工程
尚未做全局 LTO 或游戏功能裁剪。

数字为 `__flash_binary_end - __flash_binary_start`，并与 objcopy 导出的
二进制长度交叉核对；包括启动、代码、只读常量和 RAM 初始化数据的 Flash
装载副本。不计 debug 信息，也不把 BSS/NOLOAD 的宿主模拟数组算进 Flash。

链接器边界检查：

- 最小配置限制为 500 KiB（512,000 B）：链接失败，FLASH 超出 80,960 B。
- 最小配置限制为 640 KiB 分区减 4 KiB guard：链接通过，余 58,304 B。
- 默认 -Os 配置限制为 768 KiB 分区减 4 KiB guard：链接通过，余 111,912 B。

若“500 KB”指十进制 500,000 B，则最小配置超出 92,960 B，同样不够。

## 空间决策

500 KiB 目前不可用。640 KiB 是依赖 LTO + nano 的候选方案；768 KiB 是保留
默认库也能容纳当前审计代码的更保守方案。均需在完成目标资源绑定并测试后
再定版。当前仍保持原来的 1 MiB 主程序分区。

| 主程序分区（含 guard） | 剩余资源分区 | 相对当前增加 |
|---|---:|---:|
| 1 MiB | 15 MiB | 0 |
| 768 KiB | 15.25 MiB | 256 KiB |
| 640 KiB | 15.375 MiB | 384 KiB |

当前 IDPX/QLV QXIP 为 15,826,560 B，因此上述 768/640 KiB 方案分别给该 QXIP
留下 164,224/295,296 B。**这些不是最终原生资源包的余量**：还需计入目标 ABI
展开、剩余 alias/sprite/UI 元数据和最终布局；Mac QRES 的 64 位重定位包不能
直接用于这个目标容量结论。

## 适用边界

这是完整引擎代码的链接审计，不是已经跑通的 RP2350 移植：

- `platform.c` 的目标原生 brush binder 是明确报错的占位实现；没有伪称能加载
  硬件资源。最终入口/校验/绑定实现会改变大小。
- 当前引擎保留宿主内部和外部 Flash 模拟 BSS 数组，审计使用扩大后的虚拟 RAM
  区及迁移后的 scratch 地址。这些 ELF **不能烧录运行，也不能验证 SRAM 够用**。
- 剩余旧加载和诊断路径尚未裁剪；后续离线化可以缩小部分代码。
- 主循环、server/client、C 化 Quake 逻辑、MG24 渲染、HUD/菜单、音效调度均参与
  编译/链接；编译期未采用通过关闭游戏主体来取得小体积的替代程序。
