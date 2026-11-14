"""Build the native static core using the repository's existing SIMD/JIT rules."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
output = Path(sys.argv[1]).resolve()
msys = Path(os.environ.get('MSYS2_ROOT', r'C:\msys64'))
bash = msys / 'usr/bin/bash.exe'
if not bash.exists():
    raise SystemExit('Set MSYS2_ROOT to your MSYS2 installation (requires UCRT64 GCC and make).')
# Convert absolute Windows paths without shell interpolation.
def unix(path):
    path = str(path).replace('\\', '/')
    return '/' + path[0].lower() + path[2:] if len(path) > 1 and path[1] == ':' else path

command = ['make', '-j' + os.environ.get('N64EMU_JOBS', '8'),
           'platform=win', 'ARCH=x86_64', 'STANDALONE_CORE=1', 'STATIC_LINKING=1',
           'CC=/ucrt64/bin/gcc', 'CXX=/ucrt64/bin/g++', 'AR=/ucrt64/bin/ar',
           'TARGET=' + unix(output), 'all']
env = os.environ.copy()
env['PATH'] = str(msys / 'ucrt64/bin') + os.pathsep + str(msys / 'usr/bin') + os.pathsep + env.get('PATH', '')
# Bash runs a fixed command; user paths travel as individual positional arguments.
subprocess.run([str(bash), '-c', 'export PATH=/ucrt64/bin:/usr/bin:$PATH; exec "$@"', 'build-core', *command],
               cwd=root, env=env, check=True)
