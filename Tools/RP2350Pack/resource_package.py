#!/usr/bin/env python3
"""QRES1: one uncompressed, mmap-ready Mac resource package (QXIP3 + QNAT1).

This is a Mac validation package, not a 32-bit fixed-address Flash image.
All bytes, including duplicated ABI metadata and relocation data, count toward
its reported size. No compression is used to disguise resident/Flash cost.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from flash_layout import ASSET, SAVE
from native_image import validate as validate_native
from verify_xip import validate_qxip

ALIGN=16384
HEADER=struct.Struct('<4s15I')

def aligned(n):
    return (n+ALIGN-1)&-ALIGN

def validate(data):
    if len(data)<HEADER.size:
        raise ValueError('truncated QRES header')
    magic,version,total,target,qo,qs,no,ns,qcrc,ncrc,hcrc,*reserved=HEADER.unpack_from(data)
    if magic!=b'QRES' or version!=1 or total!=len(data) or target!=1 or any(reserved):
        raise ValueError('invalid QRES1 header (target must be mac-native-64)')
    header=bytearray(data[:64]);struct.pack_into('<I',header,40,0)
    if zlib.crc32(header)!=hcrc:
        raise ValueError('QRES header CRC mismatch')
    if qo!=ALIGN or not qs or no!=aligned(qo+qs) or not ns or no+ns!=total:
        raise ValueError('invalid QRES section ranges')
    if any(data[64:qo]) or any(data[qo+qs:no]):
        raise ValueError('nonzero QRES alignment padding')
    source=data[qo:qo+qs];native=data[no:no+ns]
    if zlib.crc32(source)!=qcrc or zlib.crc32(native)!=ncrc:
        raise ValueError('QRES section CRC mismatch')
    q=validate_qxip(source,check_capacity=False);n=validate_native(native,source)
    if q['version']!=3:
        raise ValueError('QRES requires QXIP3')
    # Every BSP must have exactly one offline native model entry.
    files,strings,do=struct.unpack_from('<3I',source,8)
    expected=[]
    for i in range(files):
        name,kind,_,_=struct.unpack_from('<4I',source,do+16*i)
        if kind==1:
            end=source.index(b'\0',strings+name)
            expected.append(source[strings+name:end].decode('ascii'))
    if expected!=[entry['name'] for entry in n['levels']]:
        raise ValueError('native model coverage/order differs from QXIP')
    budget=SAVE-ASSET
    return dict(format='QRES1',target='mac-native-64',hardware_flash_image=False,
                package_bytes=total,package_mib=total/1048576,sha256=hashlib.sha256(data).hexdigest(),
                qxip_offset=qo,qxip_bytes=qs,native_offset=no,native_bytes=ns,
                header_bytes=64,alignment_padding_bytes=total-qs-ns-64,
                native_immutable_model_bytes=sum(x['immutable_bytes'] for x in n['levels']),
                native_relocation_bytes=n['relocation_count']*8,
                files=q['files'],textures=q['textures'],bsp_entries=q['levels'],
                relocation_count=n['relocation_count'],
                reference_asset_capacity_bytes=budget,package_fits_reference_capacity=total<=budget,
                package_headroom_bytes=budget-total,package_overflow_bytes=max(0,total-budget),
                qxip_only_headroom_bytes=budget-qs,
                size_note='Mac 64-bit package includes both QLV1 and native ABI metadata. A 32-bit target must replace corresponding records and be measured independently.')

def build(source,native):
    qo=ALIGN;no=aligned(qo+len(source));total=no+len(native)
    if total>=2**32:
        raise ValueError('QRES exceeds uint32 address space')
    data=bytearray(total)
    HEADER.pack_into(data,0,b'QRES',1,total,1,qo,len(source),no,len(native),
                     zlib.crc32(source),zlib.crc32(native),0,*([0]*5))
    struct.pack_into('<I',data,40,zlib.crc32(data[:64]))
    data[qo:qo+len(source)]=source;data[no:]=native
    return bytes(data),validate(data)

def write_package(qxip,native,output,report,header,require_fit=False):
    data,manifest=build(qxip.read_bytes(),native.read_bytes())
    report.write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Complete Mac QRES: {len(data):,} bytes ({len(data)/1048576:.4f} MiB)')
    print(f'Reference Flash capacity: {SAVE-ASSET:,} bytes; headroom {manifest["package_headroom_bytes"]:+,} bytes')
    if require_fit and not manifest['package_fits_reference_capacity']:
        raise ValueError(f'Complete package exceeds reference Flash capacity by {manifest["package_overflow_bytes"]:,} bytes; package not published')
    tmp=output.with_suffix(output.suffix+'.tmp');tmp.write_bytes(data);tmp.replace(output)
    header.write_text('\n'.join(['/* Generated QRES1 package entries; Mac only. */',
        '#ifndef QRESOURCE_PACKAGE_H','#define QRESOURCE_PACKAGE_H',
        f'#define QRESOURCE_PACKAGE_BYTES {len(data)}u',
        f'#define QRESOURCE_QXIP_OFFSET {manifest["qxip_offset"]}u',
        f'#define QRESOURCE_NATIVE_OFFSET {manifest["native_offset"]}u',
        f'#define QRESOURCE_SHA256 "{manifest["sha256"]}"','#endif','']))
    return manifest

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('package',type=Path)
    a=p.parse_args();print(json.dumps(validate(a.package.read_bytes()),indent=2))
