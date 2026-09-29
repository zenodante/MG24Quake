"""Pointer-free QLV1 immutable brush model compiler (little endian).

The section directory replaces BSP lumps; original expanded records are not
kept a second time. See docs/QXIP_RESOURCE_FORMAT.md for the storage contract.
"""
import math
import struct
from mcu_pack_converter import parse_bsp, LUMP_NAMES

HEADER = struct.Struct('<4sIII')
ENTRY = struct.Struct('<4I')  # offset, byte length, count, stride
INVALID = 0xffffffff
STRIDES = [1, 20, 24, 12, 1, 28, 44, 32, 1, 8, 32, 2, 4, 4, 64]


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def dot(a, b):
    return f32(f32(f32(a[0]*b[0]) + f32(a[1]*b[1])) + f32(a[2]*b[2]))


def length(v):
    return f32(math.sqrt(dot(v, v)))


def records(blob, fmt):
    s = struct.Struct(fmt)
    if len(blob) % s.size:
        raise ValueError(f'incomplete {fmt} record')
    return list(s.iter_unpack(blob))


def animations(textures, ids):
    names = [r[:16].split(b'\0', 1)[0] if r else b'' for r in textures]
    # gid, next local ID, alternate local ID, total/min/max (10 Hz ticks)
    result = [[INVALID if gid is None else gid, INVALID, INVALID, 0, 0, 0] for gid in ids]
    groups = {}
    for i, name in enumerate(names):
        if not name.startswith(b'+'):
            continue
        frame = name[1:2].upper()
        if frame in [bytes([c]) for c in range(48, 58)]:
            bank, n = 0, frame[0]-48
        elif frame in [bytes([c]) for c in range(65, 75)]:
            bank, n = 1, frame[0]-65
        else:
            raise ValueError(f'invalid animated texture {name!r}')
        banks = groups.setdefault(name[2:], [{}, {}])
        if n in banks[bank]:
            raise ValueError(f'duplicate animated texture frame {name!r}')
        banks[bank][n] = i
    for banks in groups.values():
        for bank, frames in enumerate(banks):
            if not frames:
                continue
            count = max(frames)+1
            if len(frames) != count:
                raise ValueError('missing animated texture frame')
            for n, i in frames.items():
                result[i][1:] = [frames[(n+1) % count], banks[1-bank].get(0, INVALID), count*2, n*2, (n+1)*2]
    return b''.join(struct.pack('<6I', *r) for r in result), names


def compile_level(data, texture_ids, textures):
    lumps = [x[2] for x in parse_bsp(data)]
    planes = records(lumps[1], '<4fi')
    vertices = records(lumps[3], '<3f')
    nodes = records(lumps[5], '<i8h2H')
    texinfo = records(lumps[6], '<8fii')
    faces = records(lumps[7], '<HhIHH4Bi')
    clips = records(lumps[9], '<i2h')
    leaves = records(lumps[10], '<ii6h2H4B')
    marks = records(lumps[11], '<H')
    edges = records(lumps[12], '<2H')
    surfedges = [x[0] for x in records(lumps[13], '<i')]
    models = records(lumps[14], '<9f7i')
    if not models or not leaves or not planes:
        raise ValueError('brush model needs planes, leaves and submodels')
    for seq, count in ((planes, 4), (vertices, 3), (texinfo, 8), (models, 9)):
        if any(not math.isfinite(v) for row in seq for v in row[:count]):
            raise ValueError('non-finite BSP geometry')
    for p in planes:
        if not 0 <= p[4] <= 5:
            raise ValueError('invalid plane type')
    if any(v >= len(vertices) for e in edges for v in e):
        raise ValueError('edge vertex out of range')
    if any(abs(e) >= len(edges) for e in surfedges):
        raise ValueError('surfedge out of range')
    if any(m[0] >= len(faces) for m in marks):
        raise ValueError('marksurface out of range')
    node_parent, leaf_parent = [-1]*len(nodes), [-1]*len(leaves)
    owners = [-1]*len(faces)
    for i, n in enumerate(nodes):
        if not 0 <= n[0] < len(planes) or n[-2]+n[-1] > len(faces):
            raise ValueError('node plane/surfaces out of range')
        for face in range(n[-2], n[-2]+n[-1]):
            if owners[face] != -1:
                raise ValueError('surface has multiple owning nodes')
            owners[face] = i
        for child in n[1:3]:
            parents, index = (node_parent, child) if child >= 0 else (leaf_parent, -1-child)
            if not 0 <= index < len(parents):
                raise ValueError('node child out of range')
            # Solid leaf zero is shared by many nodes and never PVS-marked.
            if child == -1:
                continue
            if parents[index] != -1:
                raise ValueError('non-solid BSP child has multiple parents')
            parents[index] = i
    # Walk all components, including submodel/unreachable roots; no recursion.
    def acyclic(tree):
        state = bytearray(len(tree))
        for root in range(len(tree)):
            stack = [(root, False)]
            while stack:
                i, done = stack.pop()
                if done:
                    state[i] = 2
                elif state[i] == 1:
                    raise ValueError('cyclic BSP tree')
                elif state[i] == 0:
                    state[i] = 1
                    stack.append((i, True))
                    stack.extend((c, False) for c in tree[i][1:3] if c >= 0)
    for c in clips:
        if not 0 <= c[0] < len(planes) or any(x >= len(clips) or x < -14 for x in c[1:]):
            raise ValueError('clipnode reference out of range')
    acyclic(nodes)
    acyclic(clips)
    for leaf in leaves:
        if not -14 <= leaf[0] <= -1 or leaf[8]+leaf[9] > len(marks):
            raise ValueError('invalid leaf')
        if lumps[4] and leaf[1] != -1 and not 0 <= leaf[1] < len(lumps[4]):
            raise ValueError('leaf visibility offset out of range')
    for m in models:
        if any(h >= len(nodes if k == 0 else clips) or h < -14 for k, h in enumerate(m[9:13])):
            raise ValueError('submodel headnode out of range')
        if m[13] < 0 or m[13] > len(leaves)-1 or m[14] < 0 or m[15] < 0 or m[14]+m[15] > len(faces):
            raise ValueError('invalid submodel range')
    # Validate compressed PVS rows without materializing a visibility matrix.
    rowbytes = (models[0][13]+7)//8
    for leaf in leaves[1:]:
        pos = leaf[1]
        if not lumps[4] or pos == -1:
            continue
        produced = 0
        while produced < rowbytes:
            if pos >= len(lumps[4]):
                raise ValueError('truncated PVS row')
            value = lumps[4][pos]; pos += 1
            if value:
                produced += 1
            else:
                if pos >= len(lumps[4]) or not lumps[4][pos]:
                    raise ValueError('invalid PVS run')
                produced += lumps[4][pos]; pos += 1
            if produced > rowbytes:
                raise ValueError('PVS run exceeds row')
    out = list(lumps)
    out[1] = b''.join(struct.pack('<4fBBH', *p[:4], p[4], sum(1 << j for j in range(3) if p[j] < 0), 0) for p in planes)
    out[2], names = animations(textures, texture_ids)
    out[5] = b''.join(struct.pack('<i8h2Hi', *n, node_parent[i]) for i, n in enumerate(nodes))
    out[10] = b''.join(struct.pack('<ii6h2H4Bi', leaf[0], leaf[1] if lumps[4] else -1, *leaf[2:], leaf_parent[i]) for i, leaf in enumerate(leaves))
    converted_ti = []
    for t in texinfo:
        if texture_ids and not 0 <= t[8] < len(texture_ids):
            raise ValueError('texinfo local texture out of range')
        gid = texture_ids[t[8]] if texture_ids else None
        avg = f32(f32(length(t[:3])+length(t[4:7]))/2)
        mip = 4 if avg < 0.32 else 3 if avg < 0.49 else 2 if avg < 0.99 else 1
        # Keep LOCAL texture ID: animation topology is level-local after dedup.
        converted_ti.append(struct.pack('<8fIiI', *t[:8], t[8] if texture_ids else INVALID, t[9] if gid is not None else 0, mip))
    out[6] = b''.join(converted_ti)
    surfaces = []
    large_extents = 0
    for fi, face in enumerate(faces):
        plane, side, first, count, ti, *tail = face
        styles, light = tail[:4], tail[4]
        if plane >= len(planes) or side not in (0, 1) or ti >= len(texinfo) or count < 3 or first+count > len(surfedges):
            raise ValueError('invalid face geometry reference')
        if any(s >= 64 and s != 255 for s in styles):
            raise ValueError('invalid light style')
        t = texinfo[ti]
        points = [vertices[edges[abs(e)][0 if e >= 0 else 1]] for e in surfedges[first:first+count]]
        mins, extents = [], []
        for axis in (0, 4):
            values = [f32(dot(v, t[axis:axis+3])+t[axis+3]) for v in points]
            lo, hi = math.floor(min(values)/16), math.ceil(max(values)/16)
            mins.append(lo*16)
            extents.append((hi-lo)*16)
        name = names[t[8]] if names and textures[t[8]] else b''
        flags = side
        if name.startswith(b'sky'):
            flags |= 2 | 16
        elif name.startswith(b'*'):
            flags |= 8 | 16
        if light != -1:
            samples = (extents[0]//16+1)*(extents[1]//16+1)
            style_count = next((i for i, s in enumerate(styles) if s == 255), 4)
            if light < 0 or light+samples*style_count > len(lumps[8]):
                raise ValueError('surface light samples out of range')
        if max(extents) > 256 and not (t[9] & 1):
            large_extents += 1  # MG24 warns, does not reject.
        if flags & 8:
            mins, extents = [-8192, -8192], [16384, 16384]
        if any(not -32768 <= x <= 32767 for x in mins) or any(not 0 <= x <= 65535 for x in extents):
            raise ValueError('surface extents exceed QLV1 representation')
        surfaces.append(struct.pack('<I4H2h2H4Bii', first, plane, ti, count, flags, *mins, *extents, *styles, light, owners[fi]))
    out[7] = b''.join(surfaces)
    strides = list(STRIDES)
    if all(-32768 <= x <= 32767 for x in surfedges):
        out[13] = struct.pack('<'+'h'*len(surfedges), *surfedges)
        strides[13] = 2
    image = bytearray(HEADER.size+15*ENTRY.size)
    stats = []
    for i, blob in enumerate(out):
        image.extend(b'\0'*(-len(image) % 4))
        off = len(image)
        image.extend(blob)
        if i == 0 and not blob.endswith(b"\0"):
            image.append(0)  # outside declared source length, before section alignment
        ENTRY.pack_into(image, HEADER.size+i*ENTRY.size, off, len(blob), len(blob)//strides[i], strides[i])
        stats.append({'name': LUMP_NAMES[i], 'bytes': len(blob), 'source_bytes': len(lumps[i]), 'count': len(blob)//strides[i], 'stride': strides[i]})
    HEADER.pack_into(image, 0, b'QLV1', 1, len(image), 15)
    return image, {'sections': stats, 'bytes': len(image), 'large_surface_extents': large_extents,
                   'hull0': 'view of nodes: negative child resolves leaf contents; no duplicate array',
                   'mutable_state': 'not serialized; engine policy determines sidecar sizes'}
