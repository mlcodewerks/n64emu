"""Extract raw DIV observations; this does not certify emulator DIV accuracy."""
import argparse
import json
from pathlib import Path
import re
from parse_nemu import read_log


def sx32(value):
    value &= 0xffffffff
    return value - (1 << 32) if value >> 31 else value


def parse(path, source):
    log = read_log(path)
    records = [tuple(int(v, 16) for v in match) for match in re.findall(
        r'DIVPROBE ([0-9a-f]{16}) ([0-9a-f]{16}) ([0-9a-f]{16}) ([0-9a-f]{16})', log, re.I)]
    end = re.search(r'DIVPROBE END rows=(\d+)', log)
    prid = re.search(r'DIVPROBE PRID ([0-9a-f]{8})', log, re.I)
    canonical_mismatches, identity_failures, extension_failures, duplicate_conflicts = [], [], [], []
    seen = {}
    canonical = 0
    mask = (1 << 64) - 1
    for index, (a, b, lo, hi) in enumerate(records):
        if (a, b) in seen and seen[a, b] != (lo, hi):
            duplicate_conflicts.append(index)
        seen[a, b] = lo, hi
        if sx32(hi) != sx32(a - b * lo):
            identity_failures.append(index)
        if lo != sx32(lo) & mask or hi != sx32(hi) & mask:
            extension_failures.append(index)
        if b == sx32(b) & mask:
            canonical += 1
            x, y = sx32(a), sx32(b)
            q = (abs(x) // abs(y)) * (-1 if (x < 0) != (y < 0) else 1) if y else (1 if x < 0 else -1)
            r = x - q * y if y else x
            if lo != sx32(q) & mask or hi != sx32(r) & mask:
                canonical_mismatches.append(index)
    return {
        'source': source,
        'processor_revision_id': prid[1].lower() if prid else None,
        'purpose': 'raw observations; no malformed-DIV model or conformance pass asserted',
        'capture_complete': bool(end and int(end[1]) == len(records) == 1120),
        'rows': len(records), 'unique_pairs': len(seen),
        'canonical_cases': canonical, 'canonical_mismatches': canonical_mismatches,
        'remainder_identity_failures': identity_failures,
        'sign_extension_failures': extension_failures, 'duplicate_conflicts': duplicate_conflicts,
        'observations': [[f'{v:016x}' for v in row] for row in records],
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--source', choices=['physical-n64', 'emulator'], required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = parse(args.log, args.source)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'observations'}, indent=2))
    raise SystemExit(0 if result['capture_complete'] else 1)
