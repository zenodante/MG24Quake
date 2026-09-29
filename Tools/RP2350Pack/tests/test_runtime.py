#!/usr/bin/env python3
"""Run with python3 .../test_runtime.py [converted.pak]. No asset downloads."""
import ctypes
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from xip_image_builder import build, miptex_records, mapping_header
from mcu_pack_converter import parse_bsp, read_pak
from level_image import compile_level, animations
from verify_xip import validate_qxip

PAK = Path(sys.argv.pop(1)) if len(sys.argv)>1 else ROOT.parents[1]/'build_rp2350/pak0conv-python.pak'


class RuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = read_pak(PAK)
        cls.image, cls.manifest = build(cls.files)
        cls.temp = tempfile.TemporaryDirectory()
        lib = Path(cls.temp.name)/'reference.so'
        subprocess.run([os.environ.get('CC','clang'),'-shared','-fPIC','-O0','-ffp-contract=off',str(ROOT/'tests/loader_math.c'),'-lm','-o',str(lib)],check=True)
        cls.ref = ctypes.CDLL(str(lib))
        cls.ref.reference_mip.argtypes = [ctypes.POINTER(ctypes.c_float)]
        cls.ref.reference_extents.argtypes = [ctypes.POINTER(ctypes.c_float),ctypes.c_int,ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_int)]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_every_level_against_source_and_c_loader_math(self):
        total_faces = total_texinfo = 0
        for name, data in self.files.items():
            if not name.endswith('.bsp'):
                continue
            with self.subTest(level=name):
                lumps = [v[2] for v in parse_bsp(data)]
                _, textures = miptex_records(lumps[2])
                ids = [None if t is None else i for i,t in enumerate(textures)]
                image, _ = compile_level(data,ids,textures)
                sections=[]
                for i in range(15):
                    off,size,_,_=struct.unpack_from('<4I',image,16+i*16)
                    sections.append(image[off:off+size])
                for i in (0,3,4,8,9,11,12,14):
                    self.assertEqual(sections[i],lumps[i])
                texinfo=list(struct.iter_unpack('<8fii',lumps[6]))
                for i,t in enumerate(texinfo):
                    vecs=(ctypes.c_float*8)(*t[:8])
                    self.assertEqual(struct.unpack_from('<I',sections[6],i*44+40)[0],self.ref.reference_mip(vecs))
                    total_texinfo+=1
                vertices=list(struct.iter_unpack('<3f',lumps[3]))
                edges=list(struct.iter_unpack('<2H',lumps[12]))
                surfedges=[r[0] for r in struct.iter_unpack('<i',lumps[13])]
                for i,f in enumerate(struct.iter_unpack('<HhIHH4Bi',lumps[7])):
                    t=texinfo[f[4]]
                    points=[vertices[edges[abs(e)][0 if e>=0 else 1]] for e in surfedges[f[2]:f[2]+f[3]]]
                    cpoints=(ctypes.c_float*(len(points)*3))(*(v for p in points for v in p))
                    out=(ctypes.c_int*4)()
                    self.ref.reference_extents(cpoints,len(points),(ctypes.c_float*8)(*t[:8]),out)
                    actual=struct.unpack_from('<I4H2h2H4Bii',sections[7],i*32)
                    expected=list(out)
                    if actual[4]&8: expected=[-8192,-8192,16384,16384]
                    self.assertEqual(list(actual[5:9]),expected)
                    self.assertEqual(actual[9:13],f[5:9]);self.assertEqual(actual[13],f[9])
                    total_faces+=1
        print(f'Independent C comparison: {total_texinfo} texinfo, {total_faces} surfaces')

    def test_deterministic_and_header(self):
        self.assertEqual(build(self.files)[0],self.image)
        header=mapping_header(self.image,self.manifest)
        self.assertIn('_LOCAL_TEX_0 UINT32_C(',header)
        self.assertNotIn('static const',header)
        p=Path(self.temp.name)/'header.c';p.write_text(header+'\nint main(void) { return QLV_SECTION_COUNT != 15; }\n')
        subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Werror','-fsyntax-only',str(p)],check=True)

    def test_texture_maps_and_exact_dedup(self):
        b=self.image;tex=struct.unpack_from('<I',b,24)[0]
        for name,entry in self.manifest['level_maps'].items():
            _,records=miptex_records(parse_bsp(self.files[name])[2][2])
            off=self.manifest['level_map_offset']+entry['map_offset']
            for i,record in enumerate(records):
                gid=struct.unpack_from('<I',b,off+4*i)[0]
                if record is None:self.assertEqual(gid,0xffffffff)
                else:
                    ro,size=struct.unpack_from('<II',b,tex+12+40*gid)
                    self.assertEqual(b[tex+ro:tex+ro+size],record)
        self.assertGreaterEqual(validate_qxip(b)['headroom'],0)

    def test_legacy_qxip2(self):
        b,m=build(self.files,'bsp29')
        self.assertEqual(validate_qxip(b)['version'],2)
        self.assertLess(len(b),len(self.image))

    def test_corrupt_images_rejected(self):
        for size in (0,39,47,len(self.image)-1):
            with self.assertRaises(ValueError):validate_qxip(self.image[:size])
        for off,value in ((4,99),(16,0xffffffff),(40,0xffffffff)):
            b=bytearray(self.image);struct.pack_into('<I',b,off,value)
            with self.assertRaises(ValueError):validate_qxip(b)
        b=bytearray(self.image);mo=self.manifest['level_map_offset'];arr=struct.unpack_from('<I',b,mo+16)[0]
        struct.pack_into('<I',b,mo+arr,0xfffffffe)
        with self.assertRaises(ValueError):validate_qxip(b)
        b=bytearray(self.image);tex=struct.unpack_from('<I',b,24)[0];off=struct.unpack_from('<I',b,tex+12)[0];b[tex+off+40]^=1
        with self.assertRaises(ValueError):validate_qxip(b)

    def test_uf2_roundtrip_and_partition(self):
        import make_asset_uf2 as asset
        import make_uf2 as combined
        from flash_layout import ASSET, SAVE, BASE
        self.assertEqual(asset.validate_qxip(self.image), (len(self.files),self.manifest['unique_textures']))
        firmware=combined.encode(b'', [(BASE, b'x'*256)], 0xE48BFF59)
        packed,info=combined.combine(firmware,self.image)
        self.assertEqual(info['version'],3)
        for result in (asset.encode_assets(self.image),packed):
            payload=[]
            for off in range(0,len(result),512):
                address,size=struct.unpack_from('<2I',result,off+12)
                self.assertLessEqual(address+size,SAVE)
                if address>=ASSET:payload.append(result[off+32:off+32+size])
            self.assertEqual(b''.join(payload)[:len(self.image)],self.image)

    def test_invalid_sources_and_animation(self):
        bsp=next(data for name,data in self.files.items() if name.endswith('.bsp'))
        b=bytearray(bsp);struct.pack_into('<I',b,4,0xffffffff)
        with self.assertRaises(ValueError):parse_bsp(b)
        _,textures=miptex_records(parse_bsp(bsp)[2][2])
        b=bytearray(bsp);o,_=struct.unpack_from('<II',b,4+6*8);struct.pack_into('<i',b,o+32,-1)
        with self.assertRaises(ValueError):compile_level(b,list(range(len(textures))),textures)
        with self.assertRaises(ValueError):miptex_records(struct.pack('<ii',1,8)+b'x'*39)
        def texture(name):return name.ljust(16,b'\0')+bytes(24)
        a=[texture(b'+0foo'),texture(b'+1foo'),texture(b'+Afoo')]
        anim,_=animations(a,[17,19,20]);rows=list(struct.iter_unpack('<6I',anim))
        self.assertEqual(rows[0],(17,1,2,4,0,2))
        self.assertEqual(rows[2],(20,2,0,2,0,2))
        other,_=animations([a[0]],[17]);self.assertEqual(struct.unpack('<6I',other),(17,0,0xffffffff,2,0,2))
        with self.assertRaises(ValueError):animations([a[1]],[19])
        missing,_=animations([None],[None]);self.assertEqual(struct.unpack('<6I',missing)[0],0xffffffff)

if __name__=='__main__':unittest.main()
