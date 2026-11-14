"""Ensure the executable embeds the native core and excludes retired backends."""
from pathlib import Path
import subprocess
import sys

nm, ar, app, archive = sys.argv[1:]
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
