# Mac：MG24 原渲染器 + 预展开只读资源

当前有两个入口：`quake_game` 已接入完整 MG24 `Host_Frame`、本地 server/client、
编译为 C 的 Quake 游戏逻辑、玩家/怪物/武器/粒子、HUD 和音效调度；`quake_mac`
保留为世界渲染与资源格式的回归测试器。两者使用 MG24 边扫描、surface/span、
alias/sprite 渲染的原有 C 路径，不在 Mac 上执行 ARM 汇编。

## 完整游戏：生成和运行

从仓库根目录执行：

```sh
python3 Tools/RP2350Pack/run_pipeline.py build/pak0.pak \
  -o build-host/mac-game-resources --resource-profile mac-game
build-host/mac-game-resources/host-tools/quake_game \
  --assets build-host/mac-game-resources/quake-resources.qres --map e1m1
```

W/S 前后、A/D 平移、左右箭头转向、Space 跳跃、Ctrl/鼠标左键开火、数字键选武器，
Esc 打开原游戏菜单，关闭窗口退出。当前尚未接入鼠标视角移动。
不指定 `--frames` 时持续运行；默认按 60 Hz 调度。SDL 主线程处理显示、按键和
QAD1 解码混音，游戏逻辑和渲染在 worker 上执行。

完整游戏必须使用 `mac-game` 资源配置：它输出 IDPX alias 模型，原始索引色皮肤
直接留在 XIP，由 MG24 的直接内存路径采样，不再生成 SPI 专用逐三角形皮肤流。
旧 `mac` 配置继续输出 IDPM，供原资源/世界渲染回归使用，不能混用到完整游戏。
`.cfg/.rc/.txt/.ent` 与 QLV entities 在打包时保证末尾可访问 NUL，目录/section
长度仍保留原文本长度。

可重复的完整游戏检查：

```sh
python3 platform/macos/test_game.py \
  --player build-host/mac-game-resources/host-tools/quake_game \
  --assets build-host/mac-game-resources/quake-resources.qres \
  --output build-host/mac-game-resources/validation
```

它运行 start、e1m1～e1m8，每关 360 次 Host_Frame，并验证每关进入 signon 4、
玩家开火消耗弹药、ADPCM 解码和缓冲区尺寸，保存 JSON、日志和截图。
这是运行回归，尚不等于逐关通关、存读档、所有菜单和敌人行为都已验证。
AddressSanitizer 构建入口为 `build_game.py --sanitize`；本机 ASan runtime 在
进入 main 前的 dyld/malloc 初始化中死锁，不能将其算作通过的内存安全检查。

本轮原始 pak0 的 QXIP 为 **15,826,560 B**（超过 15 MiB **97,920 B**），
64 位 QNAT 为 **5,465,448 B**，Mac 完整 QRES 为 **21,308,776 B**。
QRES 包括 QLV 与宿主原生展开结构以及重定位记录，不是硬件烧录格式。
`mac-game` 允许宿主超额但始终报告真实尺寸；`--require-flash-fit` 会拒绝超额
完整包。该阶段使用主程序1 MiB、资源15 MiB作为比较基准；当前 RP2350 已改为主程序预留768 KiB、资源15.25 MiB，并生成更紧凑的 ARM QRN1 镜像，见 [RP2350完整固件](../rp2350/game/README.md)。

内存报告中的 zone、全局状态、Z-buffer、服务、栈分别列出，framebuffer/行缓冲是
service 的子项，不能重复相加。报告还列出遗留内部/外部 Flash 模拟数组的预留量，
以及逐关生成的 alias/sprite/UI/资源注册表等元数据量。**这些剩余生成路径尚未
全部搬到离线编译器**；当前没有硬件 Flash 写入，但不能声称已消除所有逐关构造。
Mac 的 RSS、64 位结构、系统库占用不能直接作为 RP2350 SRAM 数字。

## 不可变结构在离线阶段生成

处理链：

```
原始 PAK → Python 转换/去重 → QXIP3（QLV1 + TEX1 + LMAP）
                          → qnative_pack（同一引擎 ABI）→ Mac QNAT1
                          → qlevel_assets.h / qnative_assets.h
```

`qnative_pack` 是独立上位机工具，使用引擎的真实 C 结构布局，将 planes、nodes、
leaves、surfaces、texinfo、纹理动画指针表和 model/brush 入口预先展开。
原生镜像覆盖每个 BSP 的全部 inline brush model，并预生成 hull 0 与压缩后的
clipnode 表，供完整游戏碰撞使用。世界渲染测试器仍可直接访问 QLV1 碰撞记录。
它也生成已绑定指针字段的显式重定位记录；不靠扫描内存猜测指针。
纹理像素、顶点、边、surfedges、marksurfaces、PVS、light samples、entities
继续引用去重后的 QXIP，不复制到原生结构镜像。

播放器启动时验证 QXIP 配对 CRC、QNAT CRC、ABI 和重定位范围，将 QNAT 用
`MAP_PRIVATE` 映射，按两个实际加载基址修正指针，随后 `mprotect(PROT_READ)`。
**Brush 大数组在换关时直接绑定；完整游戏仍复制少量 model 注册入口。**
世界渲染测试器不执行运行时展开。完整游戏的 QLV 路径绕过 Mod_LoadBrushModel；
alias/sprite 等剩余旧路径见上面的限制。

Mac ASLR 下无法事先知道绝对地址，因此启动时重定位是模拟固定 Flash 指针的手段。
QNAT 在 Mac 上确实消耗宿主 RAM，报告单列为 simulated Flash，并保留真实 RSS。
RP2350 后续应按 32 位目标 ABI 和最终烧录基址生成固定指针，或使用只读偏移入口；
此版本没有实现该硬件格式。Mac QNAT **不能直接烧录 RP2350**。

`qnative_assets.h` 提供每个 BSP 的 model 字节入口偏移、ABI 和镜像 SHA256；
给定 native 基址即可得到入口。`qlevel_assets.h` 继续提供 local texture → global
TEX1 映射。选择把入口做成头文件编译进去，或放在只读镜像目录中，本质相同：
关联关系离线确定，运行时不生成同等大小的 SRAM 指针表。

目前 QXIP 保留通用 QLV1，Mac QNAT 是独立的 ABI 验证附件，因此某些不可变元数据
在磁盘上存在两种表示。最终目标镜像应替换相应源记录，并按目标 ABI 重新做 Flash
预算，不能将这里的两个文件相加后宣称已满足 16 MiB Flash 容量。

## 构建与运行

需要 Apple clang、Python 3、SDL2（当前 Homebrew SDL2）。在仓库根目录，
**原 PAK 流水线现在默认自动编译离线工具、展开结构并生成单文件完整包**：

```sh
python3 Tools/RP2350Pack/run_pipeline.py build/pak0.pak -o build-host/complete-resources
build-host/complete-resources/host-tools/quake_mac \
  --assets build-host/complete-resources/quake-resources.qres
```

无需单独传 `--native`。`--native-packer /path/to/qnative_pack` 可复用已编译工具；
`--resource-profile qxip-only` 明确选择以前的纯 QXIP 流程（包括 BSP29 兼容格式）。

完整包 QRES1 无压缩，内含 QXIP3 与 QNAT1，section 按 16 KiB 对齐便于只读/私有映射。
`qresource_package.h` 给出两部分的固定入口；`qnative_assets.h` 和 `qlevel_assets.h`
仍同步生成。`resource-package.json` 与 `summary.json` 都记录完整包尺寸，
包括所有重定位数据、目录和填充；不能使用 QXIP 的剩余空间代表完整包。

当前数据完整包 **20,184,616 B（19.2495 MiB）**：

| 部分 | 字节 |
|---|---:|
| 去重资源 QXIP3 | 15,424,320 |
| 离线结构 QNAT1（含重定位数据） | 4,734,504 |
| QRES 包头 | 64 |
| 对齐填充 | 25,728 |
| 完整包合计 | **20,184,616** |

与当前资源分区 **15,728,640 B（15 MiB）** 相比，超出 **4,455,976 B**。
这是已验证的 Mac 64 位测试包，尚不满足目标 Flash 容量。目标端仍需按 32 位 ABI
替换对应源结构并消除重复，不能把此文件烧入目标资源分区。

添加 `--require-flash-fit` 会在超限时返回非零状态、输出尺寸报告并拒绝发布新包；
已有完整包保持不变。默认 Mac 模式允许输出超出参考硬件容量的测试包，并明确报告超限。

```sh
python3 Tools/RP2350Pack/resource_package.py build-host/complete-resources/quake-resources.qres
python3 Tools/RP2350Pack/tests/test_resource_package.py build-host/complete-resources \
  --player build-host/complete-resources/host-tools/quake_mac
```

已通过完整包确定性、精确尺寸相加、损坏包拒绝、超限保留旧包，以及单文件 9 关
180 帧测试；帧哈希与分离文件版本一致。动态检查使用另外构建的 UBSan 播放器。

已有 QXIP 时可只执行新增阶段：

```sh
python3 Tools/RP2350Pack/native_image.py build-host/preexpanded/quake-assets.qxip \
  --packer build-host/macos/qnative_pack \
  -o build-host/preexpanded/quake-assets-mac.qnat \
  --header build-host/preexpanded/qnative_assets.h \
  --json build-host/preexpanded/quake-assets-native.json
```

CMake 入口也保留：`cmake -S platform/macos -B build-host/macos`，然后
`cmake --build build-host/macos -j6`。本轮机器的 CMake 路径解析出现阻塞，
实际完整构建和验证使用上面的直接 clang 构建脚本。

`mg24_config.h` 的 `QMAC_RENDER_C=1` 选择上游已保留的主机 C 等效路径。
上游用 `WIN32` 标识这些分支；这不表示调用 Windows API。Mac 禁止选择 ARM 汇编。
`QMAC_MG24_RENDERER` 是 CMake 的渲染器开关，默认 ON；OFF 仅用于早期三角形参考器。
MG24 的 SRAM 短指针宏在 Mac 下关闭；BSP/工作数组元素编号仍是普通索引。

W/S 前后，A/D 平移，左右箭头转向，Q/E 上下，Tab 换关，Space 播放测试 ADPCM，
Esc 退出。相机有 BSP 碰撞，没有玩家物理/重力。天空直接从只读原始双层像素采样，
没有为了天空动画增加 2 MiB 预合成序列；水面保留 MG24 turbulence span 路径。

## 双线程和内存

参照 pico8c 的 SDL RGB565 行上传和 queued audio。由于 macOS 视频限制：

- worker 模拟 core0：碰撞相机、MG24 渲染。
- SDL 主线程模拟 core1：键盘、两个 RGB565 行缓冲、QAD1 解码/混音和音频排队。
- 全应用仅一个 320×200 的 8 位 framebuffer，所有权通过互斥锁和条件变量交接。
- 保留 320×152 的 16 位 Z-buffer，并沿用可见性/边结构复用其空闲区域的策略。
- SDL 纹理和驱动缓存属于显示设备模拟；不是额外的游戏 framebuffer。

Mac 指针是 64 位，边结构比 MCU 大，span 容量设为 8191（13 位索引上限）。
真实扫描栈高水位计入报告；用 256 KiB 带保护页的栈承载它，不将预留值当实测用量。
边界溢出立即报错，不静默输出不完整画面。补齐了合法哨兵元素、64 位对齐、
无符号打包和 ARM VCVT 对应的饱和转换；没有替换 MG24 的核心渲染算法。

当前本地 shareware 数据测量（非 sanitizer 构建，9 关 180 帧）：

| 项目 | 字节 |
|---|---:|
| 单索引 framebuffer | 64,000 |
| 两个 RGB565 行缓冲 | 1,280 |
| Z-buffer（包括阶段性复用区） | 97,280 |
| 可写静态 section 总量，含 section 内填充 | 229,904 |
| worker 栈观测高水位 | 61,872 |
| 逐关不可变元数据堆分配 | **0** |
| QXIP 只读映射 | 15,424,320 |
| QNAT 模拟 Flash（21 个 BSP） | 4,734,504 |
| 启动时指针重定位数量 | 146,778 |

静态区 + worker 栈高水位约 285 KiB，**不是完整游戏或 RP2350 SRAM 预算**。
它不包括未来游戏状态、SDL/OS、主线程栈和分配器开销；Mac 的模拟 Flash 另列。
`memory_report.py` 结合 Mach-O linker map 补齐散落在引擎各文件的静态变量，
避免只用 sizeof 少数结构而低报。sanitizer 会增加可写元数据，应使用 release 报告预算。

```sh
python3 platform/macos/build.py --release -o build-host/macos-release
build-host/macos-release/quake_mac \
  --assets build-host/preexpanded/quake-assets.qxip \
  --native build-host/preexpanded/quake-assets-mac.qnat \
  --headless --frames 180 --cycle 20 --scripted \
  --report build-host/macos-release/runtime-memory.json
python3 platform/macos/memory_report.py \
  build-host/macos-release/quake_mac.map \
  build-host/macos-release/runtime-memory.json \
  -o build-host/macos-release/memory.json
```

## 验证

```sh
python3 platform/macos/test_native.py build-host/macos \
  build-host/preexpanded/quake-assets.qxip build-host/preexpanded/quake-assets-mac.qnat
python3 Tools/RP2350Pack/tests/test_runtime.py build-host/preexpanded/pak0conv-python.pak
```

已验证：同一输入离线编译逐字节一致；不同加载地址下 180 帧哈希一致；
9 个可玩关卡共 6480 帧、各转 360°，UBSan 无错误；六种损坏/不匹配镜像被拒绝；
磁盘源镜像保持不变。21 个 BSP 中另外 12 个是独立 brush 资源，没有玩家出生点，
已预展开，但不能把它们当完整关卡启动。固定帧哈希回归针对当前本地 shareware 资产。

资源转换的七项测试还独立用 C loader 数学验证了 1962 个 texinfo 和 42398 个 surface。
ASan 在本机系统运行库初始化阶段出现递归锁等待，未计作通过；实际动态检查使用 UBSan。

旧 QPAK/BSP/混音/参考渲染测试通过；碰撞另有 72,000 次 swept trace 与递归 Quake
参考实现比对通过。旧 UF2 artifact 组合测试需要本地 `build-rp2350/quake_rp2350_bringup.uf2`，
当前缺少该文件，未计作通过；不为此构建或修改 RP2350 固件。
