import sys
from pathlib import Path
p = Path(sys.argv[1])
size = p.stat().st_size
if size > 0xFF000:
    raise SystemExit(f"Firmware overlaps RP2350-E10 guard sector: {size}")
print(f"Firmware partition: {size:,} / 1,044,480 bytes; 4 KiB reserved for E10 guard")
