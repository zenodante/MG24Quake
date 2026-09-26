"""Verify artifact layout independently of the C reader, including UF2 guard handling."""
import importlib.util
import struct
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("make_uf2", ROOT / "Tools/RP2350Pack/make_uf2.py")
uf2 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uf2)


class ArtifactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.firmware = (ROOT / "build-rp2350/quake_rp2350_bringup.uf2").read_bytes()
        cls.assets = (ROOT / "assets-local/shareware.qpak").read_bytes()
        cls.output = uf2.combine(cls.firmware, cls.assets)

    def test_combined_image_reconstructs_every_byte(self):
        pages = {}
        numbers = []
        count = None
        guards = 0
        for offset in range(0, len(self.output), 512):
            block = self.output[offset:offset + 512]
            m0, m1, flags, address, size, index, count_here, family = struct.unpack_from("<8I", block)
            self.assertEqual((m0, m1), (uf2.MAGIC0, uf2.MAGIC1))
            self.assertEqual(struct.unpack_from("<I", block, 508)[0], uf2.END)
            self.assertLessEqual(address + size, uf2.SAVE)
            if flags == 0xA000:
                self.assertEqual(address, uf2.GUARD)
                self.assertEqual(family, 0xE48BFF57)
                self.assertEqual(count_here, 2)
                guards += 1
                continue
            self.assertEqual(family, 0xE48BFF59)
            self.assertNotIn(address, pages)
            if count is None:
                count = count_here
            self.assertEqual(count, count_here)
            numbers.append(index)
            pages[address] = block[32:32 + size]
        self.assertEqual(guards, 1)
        self.assertEqual(numbers, list(range(count)))
        reconstructed = b"".join(pages[a] for a in sorted(pages) if a >= uf2.ASSET)
        self.assertEqual(reconstructed[:len(self.assets)], self.assets)
        for offset in range(0, len(self.firmware), 512):
            flags, address = struct.unpack_from("<2I", self.firmware, offset + 8)
            if flags == 0x2000:
                self.assertEqual(pages[address], self.firmware[offset + 32:offset + 288])

    def test_reject_bad_metadata(self):
        broken = bytearray(self.assets)
        broken[64] ^= 1
        with self.assertRaises(ValueError):
            uf2.combine(self.firmware, broken)

    def test_reject_firmware_overlapping_guard(self):
        broken = bytearray(self.firmware)
        struct.pack_into("<I", broken, 512 + 12, uf2.GUARD)
        with self.assertRaises(ValueError):
            uf2.combine(broken, self.assets)


if __name__ == "__main__":
    unittest.main()
