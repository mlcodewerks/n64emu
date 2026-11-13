"""Run the explicit make manifest, retaining logs and a machine-readable result."""
from pathlib import Path
import argparse
import ctypes
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
import subprocess
import time

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=600,
                        help='Per-executable deadline in seconds (default: 600)')
    parser.add_argument('--jobs', type=int, default=1,
                        help='Independent test processes to run concurrently (default: 1)')
    parser.add_argument('executables', nargs='+', type=Path)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if os.name == 'nt':
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    logs = args.out / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    def run(executable):
        start = time.monotonic()
        relative = executable.relative_to(args.out)
        log = logs / (str(relative).replace('\\', '_').replace('/', '_') + '.log')
        with log.open('wb') as stream:
            try:
                proc = subprocess.run([str(executable.resolve())], stdout=stream,
                                      stderr=subprocess.STDOUT, timeout=args.timeout)
                code = proc.returncode
            except subprocess.TimeoutExpired:
                code = 'timeout'
            except OSError as error:
                stream.write(str(error).encode())
                code = 'launch error'
        result = dict(test=str(relative), exit_code=code,
                      seconds=round(time.monotonic()-start, 3), log=str(log))
        return result
    results = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending = [pool.submit(run, executable) for executable in args.executables]
        for completed in as_completed(pending):
            result = completed.result()
            results.append(result)
            print(f"{'PASS' if result['exit_code'] == 0 else 'FAIL'} {result['test']} ({result['seconds']:.2f}s)", flush=True)
            if result['exit_code'] != 0:
                print(Path(result['log']).read_text(errors='replace')[-4000:], flush=True)
    results.sort(key=lambda r: r['test'])
    failed = sum(r['exit_code'] != 0 for r in results)
    report = dict(passed=len(results)-failed, failed=failed, tests=results)
    (args.out / 'results.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f"{report['passed']}/{len(results)} component executables passed; logs: {logs}")
    return bool(failed)

if __name__ == '__main__':
    raise SystemExit(main())
