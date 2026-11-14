"""Keep the frontend independent of compiler runtime DLLs (the N64 core is embedded)."""
import subprocess
import sys
output = subprocess.check_output([sys.argv[1], '-p', sys.argv[2]], text=True)
imports = [line.split(':', 1)[1].strip().lower() for line in output.splitlines() if 'DLL Name:' in line]
for name in imports:
    if any(part in name for part in ('libstdc++', 'libgcc_s', 'libwinpthread', 'libc++', 'libunwind')):
        raise SystemExit('Compiler runtime must be statically linked: ' + name)
print('PASS compiler runtimes, including winpthread, are statically linked')

for name in imports:
    if name in ('d3d9.dll', 'opengl32.dll', 'vulkan-1.dll') or name.startswith(('d3dx', 'd3dcompiler')):
        raise SystemExit('Graphics runtimes must not be imported: ' + name)
print('PASS graphics DLLs are loaded at runtime; no D3D9/D3DX/OpenGL/Vulkan imports')
