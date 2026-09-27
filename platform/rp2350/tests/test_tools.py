"""Verify artifact layout independently of the C reader, including UF2 guard handling."""
import importlib.util
import struct
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "Tools/RP2350Pack"))
spec = importlib.util.spec_from_file_location("make_uf2", ROOT / "Tools/RP2350Pack/make_uf2.py")
uf2 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uf2)


class FirmwareUpdateTests(unittest.TestCase):
    def firmware(self, size=256, guard=True):
        records = [(uf2.BASE + i, b"x" * 256) for i in range(0, size, 256)]
        prefix = b""
        if guard:
            prefix = bytearray(512)
            struct.pack_into("<8I", prefix, 0, uf2.MAGIC0, uf2.MAGIC1,
                             0xA000, 0x10FFFF00, 256, 0, 2, 0xE48BFF57)
            prefix[32:288] = b"\xef" * 256
            struct.pack_into("<I", prefix, 288, 0x9957E304)
            struct.pack_into("<I", prefix, 508, uf2.END)
        return uf2.encode(prefix, records, 0xE48BFF59)

    def test_layout_abi(self):
        self.assertEqual((uf2.BASE, uf2.GUARD, uf2.ASSET, uf2.SAVE),
                         (0x10000000, 0x100FF000, 0x10100000, 0x10FC0000))

    def test_firmware_growth_never_erases_resources_or_saves(self):
        # Simulate sector erase + programming of every record, even the E10
        # ignore record. Include both small and maximum allowed firmware.
        for size in (256, 65536, uf2.GUARD-uf2.BASE):
            raw = self.firmware(size)
            output = uf2.firmware_only(raw)
            self.assertEqual(uf2.firmware_only(output), output)
            flash = bytearray(b"\xa5" * 0x1000000)
            for pos in range(0, len(output), 512):
                address, length = struct.unpack_from("<2I", output, pos+12)
                offset = address-uf2.BASE
                self.assertLessEqual(offset+length, uf2.ASSET-uf2.BASE)
                sector = offset & ~4095
                flash[sector:sector+4096] = b"\xff" * 4096
                flash[offset:offset+length] = output[pos+32:pos+32+length]
            self.assertEqual(flash[0x100000:], b"\xa5" * 0xf00000)

    def test_without_guard(self):
        raw = self.firmware(guard=False)
        self.assertEqual(uf2.firmware_only(raw), raw)

    def test_reject_invalid_firmware(self):
        raw = self.firmware()
        for offset, value in ((512+12, uf2.GUARD), (512+12, uf2.ASSET),
                              (512+12, uf2.SAVE), (512+28, 0xE48BFF56),
                              (512+20, 9), (512+24, 99), (512+16, 512),
                              (0, 0), (288, 0)):
            broken = bytearray(raw)
            struct.pack_into("<I", broken, offset, value)
            with self.assertRaises(ValueError):
                uf2.firmware_only(broken)
        for broken in (b"", raw[:-1], raw[:512]):
            with self.assertRaises(ValueError):
                uf2.firmware_only(broken)


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
