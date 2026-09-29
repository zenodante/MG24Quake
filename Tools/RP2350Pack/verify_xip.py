#!/usr/bin/env python3
"""Validate QXIP1/2/3 bounds, texture hashes, maps and QLV1 records on host."""
import argparse
import hashlib
import math
from pathlib import Path
import struct
from flash_layout import ASSET, SAVE
from level_image import STRIDES
from resource_text import is_text_resource


def require(ok, message):
    if not ok:
        raise ValueError(message)


def u32(b, off):
    require(0 <= off <= len(b)-4, 'truncated uint32')
    return struct.unpack_from('<I', b, off)[0]


def region(b, off, size):
    require(0 <= off <= len(b) and 0 <= size <= len(b)-off, 'section out of bounds')
    return b[off:off+size]


def validate_level(b, ids, textures, require_text_terminators=False):
    require(len(b) >= 256 and b[:4] == b'QLV1' and u32(b, 4) == 1 and u32(b, 8) == len(b) and u32(b, 12) == 15, 'invalid QLV1 header')
    sections, counts = [], []
    end = 256
    for i, expected in enumerate(STRIDES):
        off, size, count, stride = struct.unpack_from('<4I', b, 16+i*16)
        require(stride == expected or (i == 13 and stride == 2), 'invalid QLV1 stride')
        require(off % 4 == 0 and off >= end and size == count*stride, 'invalid QLV1 section')
        sections.append(region(b, off, size)); counts.append(count); end = off+size
    require(end == len(b) and counts[2] == len(ids), 'QLV1 length/map mismatch')
    if require_text_terminators:
        off, size = struct.unpack_from("<2I", b, 16)
        require((size and b[off+size-1] == 0) or (off+size < u32(b,32) and b[off+size] == 0), "missing entity text NUL")
    for i, rec in enumerate(struct.iter_unpack('<6I', sections[2])):
        gid, nxt, alt, total, lo, hi = rec
        require(gid == ids[i], 'QLV1 texture/LMAP mismatch')
        for ref in (nxt, alt):
            require(ref == 0xffffffff or ref < counts[2], 'animation ID out of bounds')
        require((nxt == 0xffffffff and total == lo == hi == 0) or (total in range(2,21,2) and lo % 2 == 0 and hi == lo+2 and hi <= total), 'invalid animation timing')
        if nxt != 0xffffffff:
            nr = struct.unpack_from('<6I', sections[2], nxt*24)
            require(nr[3] == total and nr[4] == hi % total, 'broken animation cycle')
    for p in struct.iter_unpack('<4fBBH', sections[1]):
        require(all(math.isfinite(x) for x in p[:4]) and p[4] <= 5 and p[5] == sum(1 << j for j in range(3) if p[j] < 0) and p[6] == 0, 'invalid plane')
    for t in struct.iter_unpack('<8fIiI', sections[6]):
        require(all(math.isfinite(x) for x in t[:8]) and (t[8] < len(ids) or (not ids and t[8] == 0xffffffff)) and 1 <= t[10] <= 4, 'invalid texinfo')
    for v in struct.iter_unpack('<3f', sections[3]):
        require(all(math.isfinite(x) for x in v), 'invalid vertex')
    edges = list(struct.iter_unpack('<2H', sections[12]))
    require(all(v < counts[3] for e in edges for v in e), 'invalid edge')
    stride = u32(b, 16+13*16+12)
    require(all(abs(e[0]) < len(edges) for e in struct.iter_unpack('<h' if stride == 2 else '<i', sections[13])), 'invalid surfedge')
    require(all(x[0] < counts[7] for x in struct.iter_unpack('<H', sections[11])), 'invalid marksurface')
    for n in struct.iter_unpack('<i8h2Hi', sections[5]):
        require(0 <= n[0] < counts[1] and n[9]+n[10] <= counts[7] and -1 <= n[11] < counts[5], 'invalid node')
        require(all(0 <= c < counts[5] if c >= 0 else -1-c < counts[10] for c in n[1:3]), 'invalid node child')
    for leaf in struct.iter_unpack('<ii6h2H4Bi', sections[10]):
        require(-14 <= leaf[0] <= -1 and (leaf[1] == -1 or 0 <= leaf[1] < counts[4]) and leaf[8]+leaf[9] <= counts[11] and -1 <= leaf[14] < counts[5], 'invalid leaf')
    for c in struct.iter_unpack('<i2h', sections[9]):
        require(0 <= c[0] < counts[1] and all(-14 <= ch < counts[9] for ch in c[1:]), 'invalid clipnode')
    for s in struct.iter_unpack('<I4H2h2H4Bii', sections[7]):
        require(s[0]+s[3] <= counts[13] and s[1] < counts[1] and s[2] < counts[6] and s[3] >= 3 and s[4] & ~27 == 0 and -1 <= s[14] < counts[5], 'invalid surface')
        require(s[13] == -1 or 0 <= s[13] < counts[8], 'invalid surface lighting')
    return counts


def validate_qxip(b, check_hashes=True, require_text_terminators=False, check_capacity=True):
    require(len(b) >= 40 and b[:4] == b'QXIP', 'invalid QXIP magic/header')
    version = u32(b, 4)
    require(version in (1, 2, 3), 'unsupported QXIP version')
    hsize = 40 if version == 1 else 48
    require(len(b) >= hsize, 'truncated QXIP header')
    files, strings, directory, data, tex, texbytes, nt = struct.unpack_from('<7I', b, 8)
    size = u32(b, hsize-4)
    require(size == len(b) and (not check_capacity or size <= SAVE-ASSET), 'QXIP size/partition mismatch')
    require(hsize <= strings <= directory <= data <= tex <= size and directory+files*16 <= data, 'invalid section ordering')
    store = region(b, tex, texbytes)
    require(len(store) >= 12 and store[:4] == b'TEX1' and u32(store,4) == nt and u32(store,8) == 12 and 12+nt*40 <= len(store), 'invalid TEX1')
    textures = []
    last = 12+nt*40
    for i in range(nt):
        off, length, digest = struct.unpack_from('<II32s', store, 12+i*40)
        require(off % 4 == 0 and off >= last and length >= 40, 'invalid texture range')
        rec = region(store, off, length); last = off+length
        if check_hashes:
            require(hashlib.sha256(rec).digest() == digest, 'texture hash mismatch')
        w, h, *mips = struct.unpack_from('<6I', rec, 16)
        require(w > 0 and h > 0 and w % 16 == h % 16 == 0, 'invalid texture dimensions')
        mipend = 40
        for level, mip in enumerate(mips):
            require(mip >= mipend, 'overlapping mip data')
            region(rec, mip, (w >> level)*(h >> level)); mipend = mip+(w >> level)*(h >> level)
        textures.append(rec)
    entries, names = [], set()
    end = data
    for i in range(files):
        name, kind, off, length = struct.unpack_from('<4I', b, directory+i*16)
        require(name < directory-strings and kind in (0,1) and off % 4 == 0 and off >= end and off+length <= tex, 'invalid file entry')
        tail = b[strings+name:directory]
        require(b'\0' in tail and tail[0] != 0, 'unterminated file name')
        name = bytes(tail.split(b'\0',1)[0])
        require(name not in names, 'duplicate file name'); names.add(name)
        entries.append((name, kind, off, length)); end = off+length
        if require_text_terminators and kind == 0 and is_text_resource(name):
            require(end < tex and b[end] == 0, "missing text resource NUL")
            end += 1
    maps = {}
    if version == 1:
        require(tex+texbytes == size, 'invalid QXIP1 end')
    else:
        mo, ms = struct.unpack_from('<2I', b, 36)
        require(mo % 4 == 0 and mo >= tex+texbytes and mo+ms == size, 'invalid LMAP range')
        lm = region(b, mo, ms)
        require(len(lm) >= 8 and lm[:4] == b'LMAP', 'invalid LMAP')
        nl = u32(lm,4); last = 8+nl*16
        require(last <= len(lm), 'truncated LMAP directory')
        for i in range(nl):
            fi, count, off, reserved = struct.unpack_from('<4I', lm, 8+i*16)
            require(fi < files and entries[fi][1] == 1 and fi not in maps and reserved == 0 and off % 4 == 0 and off >= last, 'invalid LMAP entry')
            raw = region(lm,off,count*4); last = off+count*4
            ids = [x[0] for x in struct.iter_unpack('<I',raw)]
            require(all(x == 0xffffffff or x < nt for x in ids), 'invalid global texture ID')
            maps[fi] = ids
        require(len(maps) == sum(e[1] for e in entries) and last == len(lm), 'missing level map/trailing bytes')
        for fi, ids in maps.items():
            _, _, off, length = entries[fi]
            level = region(b,off,length)
            if version == 3:
                validate_level(level,ids,textures,require_text_terminators)
            else:
                require(len(level) >= 124 and u32(level,0) == 29, 'invalid BSP29 payload')
                for i in range(15):
                    lo, size = struct.unpack_from('<2I',level,4+i*8); region(level,lo,size)
                td = u32(level,20)
                require(u32(level,td) == len(ids), 'BSP texture count mismatch')
                for i, tid in enumerate(ids):
                    rel = struct.unpack_from('<i',level,td+4+4*i)[0]
                    if tid == 0xffffffff:
                        require(rel == -1, 'missing BSP texture mismatch')
                    else:
                        expected = tex+u32(store,12+tid*40)
                        require(off+td+rel == expected, 'BSP compatibility pointer mismatch')
    return {'version':version,'files':files,'textures':nt,'bytes':len(b),'levels':len(maps),'headroom':SAVE-ASSET-len(b)}


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('image',type=Path);ap.add_argument('--allow-oversize-host',action='store_true');a=ap.parse_args()
    print(validate_qxip(a.image.read_bytes(),check_capacity=not a.allow_oversize_host,require_text_terminators=True))

if __name__ == '__main__':
    main()
