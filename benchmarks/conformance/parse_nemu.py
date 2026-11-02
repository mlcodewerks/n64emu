"""Check nemu64's self-reported assertions; an emulator exit alone is not a pass."""
import argparse
import json
from pathlib import Path
import re


def read_log(path):
    blob = Path(path).read_bytes()
    return blob.decode('utf-16' if blob.startswith(b'\xff\xfe') else 'utf-8', errors='replace')


def parse(path, full=False):
    text = read_log(path)
    results = {name: {'failed': int(failed), 'total': int(total)}
               for name, failed, total in re.findall(r'([\w-]+): Failed (\d+) of (\d+) tests', text)}
    core_errors = [line for line in text.splitlines() if '[libretro ERROR]' in line or '[core 3]' in line]
    # Nemu deliberately writes the read-only PIF ROM, then checks it was ignored.
    expected_errors = [line for line in core_errors if line.endswith('Invalid write to PIF ROM: 1FC00000')]
    unexpected_errors = [line for line in core_errors if line not in expected_errors]
    observation_only = 'DIVPROBE BEGIN' in text
    complete = 'Finished in ' in text and bool(results) and not observation_only
    if full:
        complete &= {name: value['total'] for name, value in results.items()} == {
            'Base': 4637, 'Timing': 1604, 'Cycle': 13, 'CP0-hazards': 5}
    passed = complete and not unexpected_errors and all(value['failed'] == 0 for value in results.values())
    return {'log': str(path), 'complete': bool(complete), 'passed': bool(passed), 'observation_only': observation_only, 'categories': results,
            'total': sum(value['total'] for value in results.values()),
            'failed': sum(value['failed'] for value in results.values()),
            'expected_pif_rom_probe_messages': expected_errors, 'unexpected_core_errors': unexpected_errors}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--full', action='store_true', help='Require all 6,259 cases at the pinned revision')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = parse(args.log, args.full)
    encoded = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end='')
    raise SystemExit(0 if result['passed'] else 1)
