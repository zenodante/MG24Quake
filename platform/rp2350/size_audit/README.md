# Complete-engine ARM Flash size audit — not flashable firmware

> Historical measurements from 2026-09-28, before the complete target binder and SRAM layout were finished. The implemented port now uses a 768 KiB reservation; see [current firmware](../game/README.md).

This audit cross-compiles the Mac-tested MG24 full-engine C paths with ARM GCC 15.3.1 and Pico SDK 2.3.1 from pico8c, targeting Cortex-M33/Thumb with the SDK softfp calling convention. It links real RP2350 core 1 LCD/DMA, PWM audio, I2C input and USB stdio code. SRAM objects use native 32-bit pointers. Measurements are not extrapolated from desktop ELF size or the 78 KB diagnostic program.

## Reproduce

From the repository root:

```sh
python3 Tools/RP2350Pack/firmware_size_audit.py \
  --pico8c-root ../pico8c -o build-host/rp2350-firmware-size
```

Requires CMake, Ninja and the SDK/toolchain under pico8c. It does not download dependencies, generate UF2, flash hardware or modify partitions. Outputs include report.json, module logs, source hashes, linker command, map, and ELF/bin marked SIZE_ONLY_DO_NOT_FLASH.

## Measurements: 2026-09-28

| Engine/library configuration | Flash load bytes | KiB |
|---|---:|---:|
| -Os + default newlib | 670,424 | 654.71 |
| -Os + LTO + default newlib | 643,888 | 628.80 |
| -Os + LTO + newlib-nano | 592,960 | 579.06 |

The nano configuration rebuilds SDK and engine while retaining `_printf_float` and `_scanf_float`; savings do not come from disabling floating-point I/O. Board sources retain existing optimization settings. Whole-project LTO and gameplay feature stripping were not applied.

Sizes are `__flash_binary_end - __flash_binary_start`, cross-checked against objcopy binary length. They include startup, code, constants and Flash copies of initialized RAM data. Debug information and BSS/NOLOAD simulated Flash arrays do not count toward Flash payload.

Linker boundary tests:

- Smallest configuration at 500 KiB (512,000 B): fails, exceeding FLASH by 80,960 B.
- Smallest configuration at 640 KiB minus a 4 KiB guard: passes with 58,304 B spare.
- Default -Os at 768 KiB minus a 4 KiB guard: passes with 111,912 B spare.

If 500 KB means 500,000 decimal bytes, the smallest configuration exceeds it by 92,960 B.

## Partition decision at the time of this audit

500 KiB was insufficient. 640 KiB was a candidate dependent on LTO/nano; 768 KiB allowed the default library. Both needed final binding and execution tests. The audit itself retained the then-current 1 MiB partition.

| Firmware reservation including guard | Resource partition | Increase over the audit baseline |
|---|---:|---:|
| 1 MiB | 15 MiB | 0 |
| 768 KiB | 15.25 MiB | 256 KiB |
| 640 KiB | 15.375 MiB | 384 KiB |

The IDPX/QLV QXIP was 15,826,560 B, leaving 164,224/295,296 B under the 768/640 KiB options. These were not final native-image margins: target ABI expansion, remaining alias/sprite/UI metadata and final layout still needed accounting. The 64-bit Mac QRES relocation package cannot establish target capacity.

## Scope

This is a full-engine link audit, not a working RP2350 port:

- The native brush binder in platform.c is an explicit error stub. Final entry/validation/binding code changes the size.
- Host internal/external Flash simulation arrays remain in BSS. The audit uses enlarged virtual RAM and relocated scratch addresses. These ELFs **must not be flashed and cannot establish SRAM fit**.
- Legacy loading/diagnostic code had not all been removed; offline conversion could reduce some code further.
- Main loop, server/client, compiled QuakeC, MG24 renderer, HUD/menus and sound scheduling all participate in compilation/linking. The game was not compiled out to produce a smaller substitute program.
