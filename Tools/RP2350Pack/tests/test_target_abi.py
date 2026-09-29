#!/usr/bin/env python3
"""Check real compiler ABI and conservative accounting against the source image."""
import json
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from target_abi import audit, measure, ROOT

IMAGE = Path(sys.argv.pop(1)) if len(sys.argv)>1 else ROOT/'build-host/complete-resources/quake-assets.qxip'

class TargetABI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data=IMAGE.read_bytes()
        cls.abi=measure(ROOT.parent/'pico8c')
        cls.report=audit(cls.data,cls.abi)

    def test_target_layout(self):
        self.assertEqual(self.abi['pointer_bytes'],4)
        self.assertEqual(self.abi['types']['msurface_t']['size'],32)
        self.assertEqual(self.abi['types']['dclipnode_t']['size'],6)
        self.assertEqual(self.abi['types']['brush_model_data_t']['size'],224)

    def test_all_inline_models_and_hull0_counted(self):
        files, _, directory=struct.unpack_from('<3I', self.data,8)
        nodes=models=0
        for i in range(files):
            _,kind,off,_=struct.unpack_from('<4I',self.data,directory+i*16)
            if kind==1:
                nodes+=struct.unpack_from('<I',self.data,off+16+5*16+8)[0]
                models+=struct.unpack_from('<I',self.data,off+16+14*16+8)[0]
        self.assertEqual(sum(x['submodels'] for x in self.report['levels']),models)
        self.assertEqual(sum(x['native_model_descriptors_bytes'] for x in self.report['levels']),models*252)
        self.assertEqual(self.report['additional_mg24_hull0_bytes'],nodes*6)
        self.assertGreater(models,len(self.report['levels']))

    def test_no_duplicate_arrays_and_explicit_projection(self):
        r=self.report
        removed=sum(row['source_bytes'] for x in r['levels'] for row in x['replacements'])
        added=sum(row['native_bytes'] for x in r['levels'] for row in x['replacements'])
        descriptors=sum(x['native_model_descriptors_bytes'] for x in r['levels'])
        expected=len(self.data)-removed+added+descriptors+r['additional_mg24_hull0_bytes']
        self.assertEqual(expected,r['projected_with_mg24_hull0_bytes'])
        self.assertEqual(r['projected_headroom_bytes'],r['asset_capacity_bytes']-expected)
        self.assertIn('projection_only',r['status'])

    def test_corruption_rejected(self):
        corrupt=bytearray(self.data);corrupt[:4]=b'NOPE'
        with self.assertRaises(ValueError): audit(corrupt,self.abi)

if __name__=='__main__': unittest.main()
