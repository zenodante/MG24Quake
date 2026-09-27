import sys
from flash_layout import FIRMWARE_BYTES
from pathlib import Path
p = Path(sys.argv[1])
size = p.stat().st_size
if size > FIRMWARE_BYTES:
    raise SystemExit(f"Firmware overlaps RP2350-E10 guard sector: {size}")
print(f"Firmware partition: {size:,} / {FIRMWARE_BYTES:,} bytes; 4 KiB reserved for E10 guard")
