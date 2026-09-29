#!/usr/bin/env python3
"""Exercise complete-package accounting, validation, publication and player IO."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from resource_package import build,validate,write_package
from flash_layout import ASSET,SAVE
p=argparse.ArgumentParser()
p.add_argument('output',type=Path);p.add_argument('--player',type=Path,required=True)
a=p.parse_args();sys.argv=sys.argv[:1]

class PackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source=(a.output/'quake-assets.qxip').read_bytes()
        cls.native=(a.output/'quake-assets-mac.qnat').read_bytes()
        cls.data=(a.output/'quake-resources.qres').read_bytes()
    def test_full_accounting_and_determinism(self):
        data,m=build(self.source,self.native)
        self.assertEqual(data,self.data)
        self.assertEqual(m['package_bytes'],m['qxip_bytes']+m['native_bytes']+m['header_bytes']+m['alignment_padding_bytes'])
        self.assertEqual(m['package_headroom_bytes'],SAVE-ASSET-len(data))
        self.assertEqual(m['bsp_entries'],21)
        self.assertFalse(m['hardware_flash_image'])
    def test_reject_corruption(self):
        n=struct.unpack_from('<I',self.data,24)[0]
        variants=[self.data[:-1],self.data+b'x']
        for offset in (0,4,12,16,24,40,64,16384,n,len(self.data)-1):
            broken=bytearray(self.data);broken[offset]^=1;variants.append(broken)
        for data in variants:
            with self.assertRaises(ValueError):validate(data)
    def test_strict_capacity_preserves_previous_artifact(self):
        with tempfile.TemporaryDirectory() as t:
            t=Path(t);out=t/'test.qres';out.write_bytes(b'previous validated package')
            with self.assertRaises(ValueError):
                write_package(a.output/'quake-assets.qxip',a.output/'quake-assets-mac.qnat',out,t/'size.json',t/'entry.h',True)
            self.assertEqual(out.read_bytes(),b'previous validated package')
            self.assertGreater(json.loads((t/'size.json').read_text())['package_overflow_bytes'],0)
            self.assertFalse((t/'entry.h').exists())
    def test_player_reads_single_package(self):
        with tempfile.TemporaryDirectory() as t:
            report=Path(t)/'run.json'
            command=[str(a.player.resolve()),'--assets',str(a.output/'quake-resources.qres'),'--headless','--uncapped','--frames','180','--cycle','20','--scripted','--report',str(report)]
            r=subprocess.run(command,capture_output=True,text=True,timeout=40)
            self.assertEqual(r.returncode,0,r.stderr+r.stdout)
            m=json.loads(report.read_text())
            self.assertEqual(m['frame_hash'],'3464a1178097bcce')
            self.assertEqual(m['maps_bound'],9)
            self.assertEqual(m['resource_package_bytes'],len(self.data))
            self.assertEqual(m['level_metadata_heap_bytes'],0)
            self.assertEqual(hashlib.sha256((a.output/'quake-resources.qres').read_bytes()).digest(),hashlib.sha256(self.data).digest())
            # Invalid offset with a valid header checksum must still be rejected.
            broken=bytearray(self.data);struct.pack_into('<I',broken,24,0xfffffff0)
            struct.pack_into('<I',broken,40,0);struct.pack_into('<I',broken,40,zlib.crc32(broken[:64]))
            bad=Path(t)/'bad.qres';bad.write_bytes(broken)
            command[2]=str(bad)
            r=subprocess.run(command,capture_output=True,text=True,timeout=10)
            self.assertEqual(r.returncode,1,r.stderr+r.stdout)

unittest.main()
