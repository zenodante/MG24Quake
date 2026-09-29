"""Read the persistent layout shared with the target and linker configuration."""
from pathlib import Path
import re

HEADER = Path(__file__).resolve().parents[2] / "platform/rp2350/flash_layout.h"
VALUES = {name: int(value, 16) for name, value in re.findall(
    r"^#define QRP_(\w+)\s+(0x[0-9a-fA-F]+)$", HEADER.read_text(), re.MULTILINE)}
BASE = VALUES["XIP_BASE"]
FIRMWARE_BYTES = VALUES["FIRMWARE_BYTES"]
GUARD = BASE + VALUES["GUARD_OFFSET"]
ASSET = BASE + VALUES["ASSET_OFFSET"]
SAVE = BASE + VALUES["SAVE_OFFSET"]
assert BASE + FIRMWARE_BYTES == GUARD and GUARD + 4096 == ASSET
assert ASSET < SAVE <= BASE + VALUES["FLASH_BYTES"]
