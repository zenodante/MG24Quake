#!/usr/bin/env python3
"""Integration checks for offline images, relocation and the actual renderer."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/RP2350Pack'))
from native_image import validate
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('build',type=Path);p.add_argument('qxip',type=Path);p.add_argument('native',type=Path)
a=p.parse_args();exe=a.build.resolve()/'quake_mac';packer=a.build.resolve()/'qnative_pack'
source=a.qxip.read_bytes();native=a.native.read_bytes();manifest=validate(native,source)
original=(hashlib.sha256(source).digest(),hashlib.sha256(native).digest())
with tempfile.TemporaryDirectory() as td:
    td=Path(td)
    repeated=td/'repeat.qnat'
    subprocess.run([str(packer),str(a.qxip),str(repeated)],check=True)
    assert repeated.read_bytes()==native,'ASLR-dependent offline output'
    args=[str(exe),'--assets',str(a.qxip),'--native',str(a.native),'--headless','--uncapped']
    hashes=[]
    for n in range(2):
        report=td/f'run{n}.json'
        result=subprocess.run(args+['--frames','180','--cycle','20','--scripted','--report',str(report)],capture_output=True,text=True,timeout=30)
        assert result.returncode==0,(result.stdout,result.stderr)
        data=json.loads(report.read_text())
        assert data['maps_bound']==9 and data['frames']==180 and not data['failed']
        assert data['renderer']=='mg24_edge_surface_span_c'
        assert data['native_binding_peak_bytes']==data['level_metadata_heap_bytes']==0
        assert data['startup_pointer_relocations']==manifest['relocation_count']
        hashes.append(data['frame_hash'])
    assert hashes[0]==hashes[1],'Relocated pointer bases changed rendered pixels'
    # Also compare to the verified transitional native-binding renderer.
    assert hashes[0]=='3464a1178097bcce',hashes
    # Reject corrupt image, stale source pairing, incompatible ABI, and invalid
    # relocations even when the payload checksum has been recomputed.
    for case in ('crc','source','abi','slot','target','truncated'):
        broken=bytearray(native)
        if case=='crc':broken[-1]^=1
        elif case=='source':broken[20]^=1
        elif case=='abi':broken[24]^=1
        elif case in ('slot','target'):
            rel=struct.unpack_from('<I',broken,64+56)[0]
            struct.pack_into('<I',broken,rel+(4 if case=='target' else 0),0x7fffffff)
            struct.pack_into('<I',broken,32,zlib.crc32(broken[64:]))
        else:broken=broken[:-1]
        bad=td/f'{case}.qnat';bad.write_bytes(broken)
        result=subprocess.run([str(exe),'--assets',str(a.qxip),'--native',str(bad),'--headless','--frames','1'],capture_output=True,text=True,timeout=10)
        assert result.returncode==1,(case,result.stdout,result.stderr)
    sweep=td/'sweep.json'
    result=subprocess.run(args+['--frames','6480','--cycle','720','--scripted','--report',str(sweep)],capture_output=True,text=True,timeout=120)
    assert result.returncode==0,(result.stdout,result.stderr)
    data=json.loads(sweep.read_text());assert data['frames']==6480 and data['maps_bound']==9
    assert data['frame_hash']=='7f7f4d7e22a96d12',data
    (a.build/'native-test-results.json').write_text(json.dumps(data,indent=2)+'\n')
assert original==(hashlib.sha256(a.qxip.read_bytes()).digest(),hashlib.sha256(a.native.read_bytes()).digest())
print('PASS: deterministic offline image; relocation-independent pixels; 6 invalid images rejected; 9 maps / 6480 frames; source images unchanged; per-level metadata heap = 0')
