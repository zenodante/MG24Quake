# start-map teleport freeze investigation (2026-09-28)

Hardware successfully started the game, but entering the portal after the EASY hallway froze the picture, turning and firing while audio continued. This portal stays within start and moves the player into the episode-selection hall; it does not execute changelevel.

Initially, the released Cortex-M33 ELF passed the same portal in emulation: the original trigger and both particle effects executed, the player arrived at the destination, and rendering continued through 60 frames. A 100 ms simulated timing step also passed. Peripheral interception meant this did not validate actual dual-core, USB, LCD, DMA or exception behavior.

## Diagnostic firmware

- A core 0 HardFault switches to a private 1 KiB emergency stack, clearing MSPLIM first, and records PC, LR, SP, CFSR, HFSR, BFAR and MMFAR.
- Sys_Error publishes its message before writing to USB.
- Core 1 checks for errors in its service loop, during scanline output and while waiting for the mixer lock. It writes diagnostics directly to the LCD without acquiring the engine framebuffer.
- The initial diagnostic build displayed `NO FRAME FOR 15 SECONDS` after a submission timeout. This indicated a timeout, not necessarily a deadlock: slow loading could also trigger it. That diagnostic stopped peripheral services until reboot.

Only the firmware UF2 needed replacement; QRN1 and partition layout remained compatible. Fault addresses must be decoded with the matching diagnostic ELF, not an earlier firmware ELF.

```sh
build-host/arm-test-venv/bin/python Tools/RP2350Pack/test_arm_firmware.py \
  build-host/full-firmware/game/quake_rp2350.elf \
  build-host/rp2350-game-assets/quake-native.qrn --teleport-test easy --frames 60

build-host/arm-test-venv/bin/python Tools/RP2350Pack/test_arm_firmware.py \
  build-host/full-firmware/game/quake_rp2350.elf \
  build-host/rp2350-game-assets/quake-native.qrn --fault-test
```

The teleport test adjusts the initial position and sends movement commands; original game logic performs the teleport. The fault test injects a known exception frame, verifies PC/LR/SP capture and checks that the diagnostic renderer emits 200 RGB565 scanlines. Peripheral output itself requires hardware validation.

## Root cause and fix

The hardware screen reported PC=0x1005aaf4, CFSR=0x01000000 and HFSR=0x40000000. In the matching ELF, the PC is `strd r0, r1, [r1, #-8]` in R_DrawSolidClippedSubmodelPolygons. CFSR indicates UNALIGNED. BFAR/MMFAR validity bits were clear, so their displayed values were not valid fault addresses.

That ELF placed textureCacheBuffer at 0x2005d057. MG24 reuses this byte array for mvertex_t and bedge_t, but the port omitted explicit alignment. Edge storage starts 6,000 bytes later and remained unaligned for STRD. The emulator did not enforce that hardware requirement, allowing the earlier test to pass incorrectly.

All buffer definitions and the shared declaration now use `_Alignas(8)`, covering Mac and ARM native pointers without increasing buffer capacity or changing the QRN1 ABI. ELF validation rejects the old misaligned buffer. Regression reports count clipped-submodel calls to confirm that the rendering path executes.

The fixed firmware retains the HardFault/Sys_Error screen but removes the 15-second timeout to avoid stopping legitimately slow loads. All three difficulty portals passed 60-frame ARM regressions with 44 clipped-submodel calls each. The user subsequently confirmed that the difficulty portal works on hardware; the separate episode-transition issue is documented in [the changelevel report](RP2350_CHANGELEVEL_FIX.md).
