#!/usr/bin/env python3
"""Combine runtime high-water marks with Mach-O linker section accounting.

Use a QMAC_SANITIZE=OFF build for SRAM estimates: sanitizer metadata otherwise
appears in writable sections. SDL/OS allocations and the main thread stack are
reported separately by the executable and are not target SRAM estimates.
"""
import argparse
import json
import re
from pathlib import Path


def summarize(link_map, runtime):
    sections = []
    symbols = []
    mode = None
    for line in link_map.splitlines():
        if line.startswith('# Sections:'):
            mode = 'sections'
        elif line.startswith('# Symbols:'):
            mode = 'symbols'
        elif line.startswith('# Dead Stripped Symbols:'):
            mode = None
        elif mode == 'sections':
            fields = line.split()
            if len(fields) == 4 and fields[0].startswith('0x'):
                address, size, segment, section = fields
                if segment == '__DATA':
                    sections.append((int(address, 16), int(size, 16), section))
        elif mode == 'symbols':
            match = re.match(r'(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)\s+\[\s*(\d+)\]\s+(.*)', line)
            if match:
                address, size, obj, name = match.groups()
                address, size = int(address, 16), int(size, 16)
                if size and any(start <= address < start + length for start, length, _ in sections):
                    symbols.append({'name': name, 'bytes': size, 'object': int(obj)})
    if not sections or not symbols:
        raise ValueError('Missing writable sections/symbols in Mach-O linker map')
    total = sum(size for _, size, _ in sections)
    runtime.update({
        'linked_writable_sections_bytes': total,
        'linked_writable_sections': {name: size for _, size, name in sections},
        'linked_writable_symbols_largest': sorted(symbols, key=lambda s: -s['bytes'])[:20],
        'static_plus_peak_binding_plus_observed_worker_stack_bytes':
            total + runtime['native_binding_peak_bytes'] + runtime['worker_stack_pattern_highwater_bytes'],
        'budget_note': 'Host ABI measurement, not RP2350 prediction. Includes section padding; excludes SDL, allocator overhead, main-thread stack and OS. Use a non-sanitized build.'
    })
    return runtime


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('link_map', type=Path)
    parser.add_argument('runtime_json', type=Path)
    parser.add_argument('-o', '--output', type=Path, required=True)
    args = parser.parse_args()
    result = summarize(args.link_map.read_text(), json.loads(args.runtime_json.read_text()))
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k:v for k,v in result.items() if k.endswith('_bytes')}, indent=2))
