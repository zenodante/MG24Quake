#!/usr/bin/env python3
"""Integration corruption checks: pass a generated quake-native.qrn path."""
import sys,struct,zlib,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from verify_arm_native import validate
from flash_layout import ASSET
IMAGE=Path(sys.argv.pop(1)).read_bytes() if len(sys.argv)>1 else None
class NativeGraphTests(unittest.TestCase):
    def changed(self,offset,value,checksum=True):
        b=bytearray(IMAGE);struct.pack_into('<I',b,offset,value)
        if checksum:struct.pack_into('<I',b,12,zlib.crc32(b[32:]))
        return b
    def test_all_models_and_text(self):
        v=validate(IMAGE);self.assertGreater(v['kinds'][1],0);self.assertGreater(v['kinds'][2],0)
        for e in v['entries']:
            if e['name'].endswith(('.cfg','.rc','.txt','.ent')):
                self.assertEqual(IMAGE[e['address']-ASSET+e['bytes']],0)
    def test_bad_crc(self):
        with self.assertRaises(ValueError):validate(self.changed(len(IMAGE)-4,0x12345678,False))
    def test_bad_directory(self):
        for pointer in [0,0x20000000,ASSET+len(IMAGE)-4]:
            with self.assertRaises(ValueError):validate(self.changed(24,pointer))
    def test_unaligned_native_pointers(self):
        directory=struct.unpack_from('<I',IMAGE,24)[0]
        with self.assertRaisesRegex(ValueError,'unaligned'):
            validate(self.changed(24,directory+1))
        e=next(e for e in validate(IMAGE)['entries'] if e['kind']==1)
        model=struct.unpack_from('<I',IMAGE,e['address']-ASSET+8)[0]
        md=struct.unpack_from('<I',IMAGE,model-ASSET+24)[0]
        # Recompute CRC so these prove graph validation, not checksum rejection.
        for field in [model-ASSET+24,md-ASSET+12,md-ASSET+12+6*4]:
            pointer=struct.unpack_from('<I',IMAGE,field)[0]
            with self.subTest(field=field),self.assertRaisesRegex(ValueError,'unaligned'):
                validate(self.changed(field,pointer+1))
    def test_bad_model_entry(self):
        e=next(e for e in validate(IMAGE)['entries'] if e['kind']==1)
        for offset,value in [(e['address']-ASSET+4,256),(e['address']-ASSET+8,0x20000000)]:
            with self.assertRaises(ValueError):validate(self.changed(offset,value))
if __name__=='__main__':
    if IMAGE is None:raise SystemExit('usage: test_arm_native.py quake-native.qrn')
    unittest.main()
