#!/usr/bin/env python3
"""Verify independent firmware/assets UF2 writes and reconstruct source bytes."""
import argparse,hashlib,json,struct
from pathlib import Path
from flash_layout import BASE,ASSET,GUARD,SAVE,FIRMWARE_BYTES
from make_uf2 import parse_firmware,MAGIC0,MAGIC1,END
from verify_xip import validate_qxip

def verify(firmware,assets,program,qxip):
    prefix,records,family=parse_firmware(firmware)
    if [x[0] for x in records] != list(range(BASE,BASE+256*len(records),256)):
        raise ValueError('Firmware pages must be contiguous from XIP base')
    reconstructed=b''.join(x[1] for x in records)
    if reconstructed[:len(program)]!=program or len(reconstructed)-len(program) not in range(256):
        raise ValueError('Firmware UF2 differs from linked binary')
    if len(program)>FIRMWARE_BYTES:raise ValueError('Firmware exceeds partition')
    if qxip[:4]==b'QRN1':
        from verify_arm_native import validate
        info=validate(qxip);info.pop('entries')
    else:info=validate_qxip(qxip,require_text_terminators=True)
    if not assets or len(assets)%512:raise ValueError('Invalid asset UF2 length')
    count=len(assets)//512;parts=[]
    for i in range(count):
        block=assets[512*i:512*(i+1)];m0,m1,flags,address,size,index,total,fid=struct.unpack_from('<8I',block)
        if (m0,m1,flags,size,index,total,fid)!=(MAGIC0,MAGIC1,0x2000,256,i,count,family):raise ValueError('Invalid asset record')
        if struct.unpack_from('<I',block,508)[0]!=END:raise ValueError('Invalid asset end magic')
        if address!=ASSET+256*i or address+size>SAVE:raise ValueError('Asset address outside resource partition')
        parts.append(block[32:288])
    reconstructed=b''.join(parts)
    if reconstructed!=qxip+b'\xff'*((-len(qxip))%256):raise ValueError('Asset UF2 differs from QXIP')
    if prefix and struct.unpack_from('<I',firmware,12)[0]!=GUARD:raise ValueError('Guard address not migrated')
    return dict(layout_abi=3,firmware_reservation_bytes=ASSET-BASE,firmware_capacity_bytes=FIRMWARE_BYTES,
                guard_address=hex(GUARD),asset_address=hex(ASSET),asset_capacity_bytes=SAVE-ASSET,
                firmware_bytes=len(program),firmware_headroom_bytes=FIRMWARE_BYTES-len(program),
                asset_bytes=len(qxip),asset_headroom_bytes=SAVE-ASSET-len(qxip),
                asset_programmed_end_exclusive=hex(ASSET+len(reconstructed)),
                firmware_uf2_bytes=len(firmware),asset_uf2_bytes=len(assets),
                firmware_sha256=hashlib.sha256(program).hexdigest(),asset_sha256=hashlib.sha256(qxip).hexdigest(),
                firmware_uf2_sha256=hashlib.sha256(firmware).hexdigest(),asset_uf2_sha256=hashlib.sha256(assets).hexdigest(),
                byte_reconstruction_verified=True,disjoint_write_regions=True,qxip=info)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('firmware',type=Path);p.add_argument('assets',type=Path)
    p.add_argument('--firmware-bin',required=True,type=Path);p.add_argument('--qxip',required=True,type=Path)
    p.add_argument('--json',type=Path);a=p.parse_args()
    r=verify(a.firmware.read_bytes(),a.assets.read_bytes(),a.firmware_bin.read_bytes(),a.qxip.read_bytes())
    if a.json:a.json.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps(r,indent=2))
if __name__=='__main__':main()
