"""Audit Thar0's captured DIV vectors against the current core arithmetic.

This is an investigation tool, not a substitute for executing the CPU/JIT tests.
Exit status is nonzero while any hardware result differs from the current model.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct


MASK64 = (1 << 64) - 1


def signed(value, bits):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value >> (bits - 1) else value


def current_div(a, b):
    x, y = signed(a, 32), signed(b, 64)
    q = ((abs(x) // abs(y)) * (-1 if (x < 0) != (y < 0) else 1)
         if y else (1 if x < 0 else -1))
    r = x - q * y if y else x
    return signed(q, 32) & MASK64, signed(r, 32) & MASK64


def audit(path):
    blob = path.read_bytes()
    if len(blob) < 32:
        raise ValueError('truncated image header')
    _, _, initial, final, size, _, _, _ = struct.unpack_from('>8I', blob)
    if not size or size % 32 or max(initial, final) + size > len(blob):
        raise ValueError('invalid DIV image memory spans')
    counts = {name: {'cases': 0, 'matches': 0, 'mismatches': 0}
              for name in ('divisor_sign_bits_agree', 'divisor_sign_bits_disagree')}
    seen, patterns, first = {}, Counter(), []
    invariant_failures = {'operands_changed': [], 'remainder_identity': [],
                          'result_sign_extension': [], 'duplicate_conflicts': []}
    for index, (a, b, lo, hi) in enumerate(struct.iter_unpack('>4Q', blob[final:final + size])):
        offset = index * 32
        if (a, b) != struct.unpack_from('>2Q', blob, initial + offset):
            invariant_failures['operands_changed'].append(index)
        if signed(hi, 32) != signed(a - b * lo, 32):
            invariant_failures['remainder_identity'].append(index)
        if lo != signed(lo, 32) & MASK64 or hi != signed(hi, 32) & MASK64:
            invariant_failures['result_sign_extension'].append(index)
        key = (a & 0xffffffff, b)
        if key in seen and seen[key] != (lo, hi):
            invariant_failures['duplicate_conflicts'].append(index)
        seen[key] = lo, hi
        disagree = bool((b >> 31 ^ b >> 63) & 1)
        group = counts['divisor_sign_bits_disagree' if disagree else 'divisor_sign_bits_agree']
        group['cases'] += 1
        actual = current_div(a, b)
        match = actual == (lo, hi)
        group['matches' if match else 'mismatches'] += 1
        if not match and len(first) < 8:
            first.append({'index': index, 'memory_offset': f'0x{offset:x}',
                          'rs': f'{a:016x}', 'rt': f'{b:016x}',
                          'expected_lo_hi': [f'{lo:016x}', f'{hi:016x}'],
                          'current_lo_hi': [f'{v:016x}' for v in actual]})
        if disagree:
            q = lo & 0xffffffff
            if (a >> 31 ^ b >> 63) & 1:
                q = -q & 0xffffffff
            patterns[f'{q:08x}'] += 1
    return {'image': str(path), 'sha256': hashlib.sha256(blob).hexdigest(),
            'purpose': 'corpus audit; does not execute or certify a JIT',
            'cases': size // 32, 'groups': counts,
            'current_arithmetic_matches_hardware': all(not v['mismatches'] for v in counts.values()),
            'hardware_invariant_failures': invariant_failures,
            'malformed_quotient_patterns_after_sign_normalization': dict(patterns.most_common(8)),
            'first_mismatches': first}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = audit(args.image)
    encoded = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end='')
    raise SystemExit(0 if result['current_arithmetic_matches_hardware'] and
                     not any(result['hardware_invariant_failures'].values()) else 1)
