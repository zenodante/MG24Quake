# 768 KiB 分区与运行时数据固定化审计

> 历史分析：以下是完整固件实现前的分配审计。文中的 binder 未完成、诊断固件尺寸等描述已被后续实现取代。当前状态见 [完整固件结果](RP2350_FULL_FIRMWARE_RESULTS.md) 和 [构建说明](../platform/rp2350/game/README.md)。


日期：2026-09-28。结论依据当前代码、ARM GCC 类型布局以及 Mac 完整游戏九关
3,240 次 Host_Frame 的实际分配记录。这里的“Flash 固定化”指上位机生成不可变
数据并烧录进资源镜像，不是运行时写 Flash。

## 结论

应继续固定化 alias/sprite 描述、WAD/UI 索引、模型/声音入口及每关资源映射。
不过当前 zone 堆的主要内容已经是可变游戏状态，不能通过把整个堆搬进 Flash
继续节省大块 SRAM。应将“旧加载器生成的只读资源元数据”与“真正可变的堆”分开。

此次没有把任何尺寸审计 ELF 包装为可烧录主程序。可实际构建的 RP2350 程序仍为
`quake_rp2350_bringup` 硬件/资源验证程序：世界视图、碰撞、显示、输入和音频验证，
不含完整游戏主体。板级配置沿用现有工程的 `pimoroni_explorer`（ST7789 显示、
QwSTPad 输入），未经实物验证。完整游戏目标的 native binder 和 SRAM 布局尚未完成。

## 当前产物与地址

| 区域 | 半开地址范围 | 容量 |
|---|---|---:|
| 主程序可用区 | 0x10000000～0x100BF000 | 782,336 B |
| 更新保护区 | 0x100BF000～0x100C0000 | 4,096 B |
| 资源区 | 0x100C0000～0x11000000 | 15,990,784 B（15.25 MiB） |

主程序预留合计 768 KiB。没有单独存档分区。分区 ABI 升为 v3，从原 1 MiB 边界
迁移时必须同步使用新固件和新资源位置；只更新旧固件或旧资源中的一个会不匹配。

当前资源 UF2 存放 QXIP3/QLV1、TEX1、LMAP、IDPX alias 模型和 QAD1 音频，包含
339 文件、21 个 BSP、396 个去重纹理。资源原始大小 15,826,560 B，字节余量
164,224 B。UF2 按 256 B 写入后实际末端为 0x10FD7F00，余 164,096 B。

硬件验证程序 bin 为 78,212 B；程序 UF2 为 157,184 B；资源 UF2 为 31,653,376 B。
UF2 是传输容器，文件大小不是 Flash 占用。逐块验证了 family ID、编号、地址、
保护区、无交叉写入及完整源字节重建；旧 1 MiB 地址、错误 family/块数被拒绝。
这验证格式与布局，不代表已经在实物上测试。

## 真正的 zone 堆：峰值 53,864 B

启用 `build_game.py --memory-audit` 的主机观察器，记录全部 Z_Malloc2/Z_Free；
每次出现新峰值，保存当时仍存活的分配，避免把不同时间或不同地图的峰值相加。
九关均有 357 帧处于 signon 4，玩家开火/受伤和 ADPCM 检查通过。

| 峰值时分配来源 | 含块管理头的字节 | 块数 | 固定到 Flash 的判断 |
|---|---:|---:|---|
| ED_Alloc | 51,640 | 311 | 实体整体不能固定；只能按类别分析不变字段 |
| SV_ClearWorld | 1,768 | 1 | 空间划分轴/平面/子节点可以，实体链表必须可写 |
| CL_ParseServerInfo | 408 | 1 | nodeHadDlight，每帧清除/修改，必须在 SRAM |
| zone 自身管理头 | 48 | — | 分配器状态，必须可写 |

实体占约 95.9%。物理位置/速度、生命值、弹药、动画帧、AI 目标、nextthink、
实体链表和区域链表都会修改。即使地图中的门或物品起初不动，触发、拾取、
复活、死亡等仍会改动它们，不能因为来自 BSP entities 就将整个对象设为 const。
证据见 `sv_phys.c`、`sv_main.c`、`quakeProgs.c` 和 `entity_getters_setters.h`。

`world.c` 的 areanode 需要拆成不可变空间树与可变 trigger/solid 链表头；移动
实体时 SV_UnlinkEdict/SV_LinkEdict 会修改链表。最多只有约 KB 级收益，优先级低。
`r_light.c:R_PushDlights/R_MarkLights` 会清空并设置 nodeHadDlight；它属于必要
的可变 sidecar，不应进不可改写 Flash。

### 实体字段还能否拆分？

可以有选择地做，但必须按实体类型和生命周期验证，而不是按字段名称猜测。
path_corner 的位置/目标名、部分静态装饰、部分触发器配置是候选；但 model、
frame、think、use、spawnflags 等都存在运行时赋值路径，不能全局认定不可变。
动态生成的弹丸、背包、碎片还需要 RAM 版本，存读档也不能依赖临时指针。

MG24 已将静态灯实体拆成 romEntvars 与可变外壳，可沿用此结构，把 romEntvars
从运行时 storeToInternalFlash 改成资源包中的固定记录。普通实体若只移走一两个
16 位字段，新增的 32 位描述符指针和对齐可能抵消收益；应先算每类净收益。

这是 Mac 的 64 位堆数据，不是 RP2350 运行实测。ARM GCC 测得原生 memblock_t
为 16 B（Mac 为 32 B），monster_edict_t 212 B、player_edict_t 312 B、
func_edict_t 216 B、trigger_edict_t 116 B、path_corner_edict_t 32 B。
311 个独立实体分配在 Mac 中仅块头就占 9,952 B；如果以后仍有 SRAM 压力，可
考虑按实体类型分组分配，减少块头和碎片。这是 RAM 分配优化，不是 Flash 固定化，
也不要求恢复短指针压缩。

## 旧加载路径生成的数据：同时存活的记录峰值 63,936 B

这部分当前写入 Mac 的 internalFlash 模拟数组，**不在上述 zone 峰值之内**。
数组中指针高水位为 65,768 B，包含对齐和 common-zone 页边界空隙；记录净和
63,936 B。只有加载时的临时构建缓冲会进入 zone，随后释放。

| 来源 | 字节 | 可固定化方案与限制 | 优先级 |
|---|---:|---|---|
| Mod_LoadAliasModelMemoryReady | 24,544 | 离线生成 mdl/stvert/skin/frame/group 描述；三角形、帧顶点、皮肤继续直接引用 XIP；模型选帧状态留 RAM | 高 |
| Draw_Init | 16,384 | conchars 像素已经在 gfx.wad，内存映射下直接引用，通常无需另存一份 | 高 |
| finalizeModKnown | 8,192 | 原生 model 描述可固定；needload、清缓存/失效状态应移到 RAM sidecar，不能原样冻结整个 mod_known | 高 |
| W_LoadWadFile | 5,216 | 打包时规范化 WAD 名称并展开目录，运行时直接引用；可替换原目录，避免重复空间 | 高 |
| CL_ParseServerInfo | 5,112 | 固定每关 model/sound 映射；需要统一 server/client 编号，并处理 demo/不同玩法的资源集合 | 高，需编号设计 |
| SV_SpawnServer | 2,048 | 固定每关 model precache 名称/ID 表；不得将启动时遍历得到的顺序未经验证直接写死 | 同上 |
| Sbar_Init | 1,080 | 离线生成 HUD 图片指针表，图片仍引用 WAD 原像素 | 高 |
| qcc_makestatic | 960 | 静态灯/装饰 romEntvars 预生成，保留实体外壳和必要可见性状态；考虑技能/玩法过滤 | 中 |
| Mod_LoadSpriteFrame / Mod_LoadSpriteModel | 336 | 帧尺寸/原点/像素指针固定，sprite 当前帧和实体位置仍可变 | 高 |
| R_InitTextures | 64 | 缺失纹理描述直接编译为 const 或生成到资源包 | 易做但收益小 |

`model.c:Mod_ClearAll` 会改写 needload，并为 sprite 清空 data；如果直接把现有
model_t 数组放入只读 Flash，会在换关时出错。需要去掉“加载/失效缓存”语义，
改为固定资源描述加少量可变选择/状态，而不只是把 malloc 替换为 const。

每关 precache 映射有必要固定化，但它比 BSP local texture → global TEX1 更复杂。
BSP 纹理映射是纯文件数据；model/sound precache 的集合与次序来自 spawn/game
逻辑，可能受 skill、deathmatch、coop 和 demo 影响。应生成可验证的每关资源
清单或各配置变体，让 server/client 共用编号，未知请求明确失败，不静默错配。

## 不在堆里的其他 SRAM

当前 game_sound.c 的 `game_sfx[MAX_SFX]` 是静态数组，ARM 为 255×44 = 11,220 B。
名字、数据地址、长度和 loop 点可生成全局只读表，注册状态用位图/小型 sidecar；
播放位置、混音增益、ADPCM predictor/index 等必须保持可变。不要把固定声音描述
和实时 mixer channel 一起冻结。

帧缓冲、Z-buffer、边/span 工作区、光照计算工作区、粒子、网络消息、按键状态、
混音器和栈也不应视为“未固定化资源”。它们是计算和通信所需的工作内存。

观察器本身有 274,472 B 的静态记录空间，并增加主机栈调用；不进入正式构建，
不能把观察器版 RSS/栈高水位当目标内存。现有 15 MiB/64 MiB 模拟 Flash BSS 数组
是主机旧路径工具，应在目标实现中删除，不能占用 RP2350 SRAM。

## 资源空间还需继续核算

本次 UF2 是当前 QXIP 资源镜像，不包含 Mac QNAT 的 64 位重定位包，也还不是
最终 RP2350 全部原生固定指针镜像。ARM ABI 的保守替换估算为：

- QLV 若干结构替换为 ARM 原生结构，并加入全部 model/brush 入口：15,868,332 B。
- 再加入 hull0：16,000,020 B，比新的 15.25 MiB 资源区多 9,236 B。
- 此估算仍保留原 dmodels 表，共 635×64 = 40,640 B。如果所有消费者迁移到
  原生描述后移除这份重复数据，理论上可回收它；但不能在旧诊断 reader 仍依赖
  QLV models 时直接删掉。

不能简单在现有镜像后面追加上表的 63.9 KB：应替换旧描述、复用原像素并去掉
重复表，再按目标 ABI 重算。alias 的原始/运行时字段大多能替换，字体/WAD 无需
重复像素。最终容量要以新容器和目标 binder 实际生成结果验证，不能仅凭本次
QXIP 尚余 164 KB 就宣布全部原生资源已经装下。

推荐顺序：先完成 target-native brush binder 与无重复容器；随后离线化 alias/
sprite/WAD/HUD 和资源映射；最后依据真正的 ARM RAM 峰值决定是否细拆普通实体。
优先消除确定只读的加载产物，不以牺牲游戏动态状态为代价降低堆数字。

## 重现分配分析

```sh
python3 platform/macos/build_game.py --memory-audit -o build-host/memory-audit
python3 platform/macos/test_game.py \
  --player build-host/memory-audit/quake_game \
  --assets build-host/mac-game-resources/quake-resources.qres \
  --output build-host/release-768k/memory-audit
```

原始分配报告含 zone_peak_allocations 与 legacy_flash_peak_allocations；两张表
分别记录各自同时存活峰值。此次未实际冻结新字段，也没有把主机读写路径假装成
已经完成的硬件 XIP binder。
