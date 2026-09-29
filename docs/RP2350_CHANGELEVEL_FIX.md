# Normal level transitions and release validation (2026-09-29)

Hardware confirmation: the alignment fix allowed the difficulty portal to work. Entering an episode portal then produced `HOST CHANGE LEVEL at host_cmd.c:362`. This was an explicit Sys_Error, not another HardFault.

Fixes:

- Remove the unconditional legacy debug stop in Host_Changelevel_f, allowing the existing SV_SaveSpawnparms, SV_SpawnServer and client reconnect sequence to execute.
- Allow QMAC_GAME (the complete Mac and RP2350 engines) to display a loading plaque during a transition. The old WIN32 debug assertion incorrectly reported `Screen updated before!`; the new regression reproduced this second failure after the first fix.
- Retain other error checks. Do not indiscriminately remove FIXME calls: invalid resources and entity overflows still need to fail explicitly.

## Why the tools needed improvement

Valid resources do not prove correct engine control flow. Earlier multi-map ARM tests used `map` to start each map independently, missing normal `changelevel`, player parameter preservation and reconnection. Package validation and gameplay validation must be recorded separately.

Existing QRN1 checks cover CRC, image bounds, model types, selected graph references and partition boundaries. New checks validate native alignment for directories, model arrays, BSP data, textures, collision and alias/sprite structures. Corruption tests recompute CRC before validation, proving that structure checks reject misaligned pointers. Ordinary pixel byte streams do not receive inappropriate pointer alignment constraints.

The generator already calls the validator from arm_native.py, so these checks are part of resource generation. UF2 verification also checks reused resource images. No QRN1 format, resource bytes or partition layout changed; resources do not need reflashing.

## New regression paths

`test_arm_firmware.py --episode-test --frames 100` positions the player before the episode portal, faces the portal and moves forward. Original trigger, QuakeC changelevel, command queue, server and client code then execute. Success requires an actual qcc_changelevel call and completed signon in e1m1.

`--changelevel-cycle --cycle 50 --frames 450` uses changelevel across start and e1m1–e1m8. All nine maps must complete client signon. The old map-based test remains a separate path.

Enable release gates with:

```sh
python3 Tools/RP2350Pack/build_game_firmware.py build/pak0.pak \
  -o build-host/rp2350-release \
  --engine-test-python build-host/arm-test-venv/bin/python
```

The selected Python requires unicorn and pyelftools. Tests cover the difficulty portal, episode portal, nine-map transitions and controls. Any failure stops the build before a new success report is written. Reports contain engine_tested, individual results and firmware/resource hashes. Without the option, engine_tested is explicitly false. Hardware timing still requires device validation.

The old firmware fails the episode regression with the reported error; the fixed firmware passes both the episode regression and 450-frame transition test. Hardware retesting remains pending.

Further coverage should include actual level exits, death/restart, save/load capability boundaries and entity stress. Current tests do not establish that every gameplay path works.
