import sys
import struct
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from xip_image_builder import build
from verify_xip import validate_qxip

class TextResources(unittest.TestCase):
    def test_lengths_and_termination_at_every_alignment(self):
        for length in range(9):
            for suffix in ('.cfg','.rc','.txt','.ent'):
                text=b'x'*length
                image,manifest=build({'first'+suffix:text,'next.bin':b'ABCD'})
                directory=struct.unpack_from('<I',image,16)[0]
                _,_,off,size=struct.unpack_from('<4I',image,directory)
                nextoff=struct.unpack_from('<I',image,directory+16+8)[0]
                self.assertEqual(size,length)
                self.assertEqual(image[off:off+size],text)
                self.assertEqual(image[off+size],0)
                self.assertGreater(nextoff,off+size)
                validate_qxip(image,require_text_terminators=True)
                broken=bytearray(image);broken[off+size]=42
                with self.assertRaisesRegex(ValueError,'NUL'):validate_qxip(broken,require_text_terminators=True)
    def test_binary_unchanged(self):
        image,_=build({'data.bin':b'ABCD'})
        directory=struct.unpack_from('<I',image,16)[0]
        _,_,off,size=struct.unpack_from('<4I',image,directory)
        self.assertEqual(image[off:off+size],b'ABCD')
        self.assertEqual(off+size,struct.unpack_from('<I',image,24)[0])

if __name__=='__main__':unittest.main()
