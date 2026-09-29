# Port documentation

Current implementation:

- [RP2350 full firmware: build, partitions, controls and tests](../platform/rp2350/game/README.md)
- [Firmware size, memory and validation results](RP2350_FULL_FIRMWARE_RESULTS.md)
- [Mac SDL game and offline resources](../platform/macos/README.md)
- [Level-transition fixes and release validation](RP2350_CHANGELEVEL_FIX.md)
- [Teleport HardFault investigation](RP2350_TELEPORT_DIAGNOSTICS.md)

Design and historical measurements:

- [QXIP / QLV intermediate format](QXIP_RESOURCE_FORMAT.md): the final RP2350 image uses QRN1.
- [Immutable Flash data audit](FLASH_IMMUTABILITY_AUDIT.md): pre-implementation allocation measurements and rationale.
- [Early RP2350 diagnostics](../platform/rp2350/README.md): separate bring-up targets.
- [Link-size audit](../platform/rp2350/size_audit/README.md): not executable game firmware.
- [Original upstream README](UPSTREAM_README.md).

The full game uses `quake_rp2350` with QRN1 resources. `quake_rp2350_bringup` and old QXIP images are retained for historical diagnostics.
