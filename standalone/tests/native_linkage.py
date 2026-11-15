"""Ensure the executable embeds the native core and excludes retired backends."""
from pathlib import Path
import subprocess
import sys
import struct

nm, ar, app, archive, link_map, mode = sys.argv[1:]
if mode == 'stripped':
    image = Path(app).read_bytes()
    pe_offset = struct.unpack_from('<I', image, 0x3c)[0]
    assert image[pe_offset:pe_offset + 4] == b'PE\0\0'
    symbol_table, symbol_count = struct.unpack_from('<II', image, pe_offset + 12)
    assert symbol_table == 0 and symbol_count == 0, 'Release executable retains COFF symbols'
    assert b'.debug_' not in image, 'Release executable retains debug sections'
    symbols = Path(link_map).read_text(encoding='utf-8')
    print('PASS release executable stripped; native linkage verified against linker map')
else:
    symbols = subprocess.check_output([nm, '--defined-only', app], text=True)
assert 'n64emu_init' in symbols and 'n64emu_run' in symbols
for forbidden in ['retro_load_game', 'retro_run', 'Spessa', 'spessa', 'VulkanRenderer', 'vkCreateInstance', 'wglCreateContext', 'gladLoadGLLoader']:
    assert forbidden not in symbols, forbidden
members = subprocess.check_output([ar, 't', archive], text=True).splitlines()
assert 'runtime.o' in members and 'api.o' in members
assert 'libretro.o' not in members
root = Path(__file__).resolve().parents[1]
for removed in ['third_party/spessasynth', 'third_party/vulkan-headers', 'third_party/graphics/volk.c',
                'third_party/graphics', 'src/shader_gl.cpp', 'src/midi.cpp', 'src/out_vulkan.cpp', 'src/core_catalog.cpp']:
    assert not (root / removed).exists(), removed
print('native core embedded; libretro adapter, MIDI and Vulkan excluded')
