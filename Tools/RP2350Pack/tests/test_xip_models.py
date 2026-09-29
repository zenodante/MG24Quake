#!/usr/bin/env python3
"""Verify IDPX pixels and packed triangle indices against original MDL data."""
import argparse
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from mcu_pack_converter import read_pak
from mdl_converter import convert_mdl,parse_source
p=argparse.ArgumentParser();p.add_argument('pak',type=Path);a=p.parse_args();sys.argv=sys.argv[:1]
class XipModels(unittest.TestCase):
    def test_all_original_skins_and_triangles(self):
        count=0
        for name,data in read_pak(a.pak).items():
            if not name.endswith('.mdl'):continue
            with self.subTest(model=name):
                source,skins,st,tris,frames=parse_source(data)
                output,meta=convert_mdl(data,xip_skins=True)
                magic,model,stverts,descs,triangles=struct.unpack_from('<5I',output)
                self.assertEqual(magic,0x58504449)
                self.assertEqual(output[model:model+84],data[:84])
                self.assertEqual(meta['format'],'IDPX')
                for i,skin in enumerate(skins):
                    packed,offset=struct.unpack_from('<2I',output,descs+i*8)
                    self.assertEqual(packed&1,0)
                    self.assertEqual(packed>>1,offset)
                    self.assertEqual(output[offset:offset+len(skin)],skin)
                for i,(front,indices) in enumerate(tris):
                    lo,hi=struct.unpack_from('<2I',output,triangles+i*8)
                    self.assertEqual(lo&1,front&1)
                    self.assertEqual([(lo>>14)&511,(lo>>23)&511,hi&511],list(indices))
                    self.assertEqual((lo>>1)&8191,0)
                    self.assertEqual(hi>>9,0)
                count+=1
        self.assertGreater(count,0)
    def test_layout_conflict_rejected(self):
        with self.assertRaises(ValueError):convert_mdl(b'',include_original_skins=True,xip_skins=True)
if __name__=='__main__':unittest.main()
