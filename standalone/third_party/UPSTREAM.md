# Dependency provenance

The source subsets below were vendored from a local dependency snapshot on
15 November 2026. This adaptation builds against the parent n64emu core.

| Component | Source snapshot |
| --- | --- |
| Dear ImGui core | `dbb30112c619990c593ab46f1855ac08d8ff1e52` from ocornut/imgui, as integrated by winweg |
| Dear ImGui Win32 backend | 1.92.8 snapshot; compatible with the bundled core and verified by native UI/core tests |
| Dear ImGui D3D9 backend | Local `wtdemo2/subprojects/imgui-1.92.8/backends` snapshot; dynamic texture and standard sampler callback support, verified against the bundled core |
| ImGuiColorTextEdit | `12a5a01d85e541d63ae92616d7a2d9c6dedc2e9a` from goossens/ImGuiColorTextEdit |
| Libretro headers/DSP | winweg integration of libretro-common `d9597c359a5ca8399a4e244f816e7e975a92a2aa`, including its DSP additions and updated interfaces |
| Librashader C header | `07ac790cdb9826f37cf8e1daf5011f03e3c4daa7` from SnowflakePowered/librashader |
| Roboto Regular | Embedded asset from googlefonts/roboto snapshot `38062f4b4a0be4346d07a928408da21602545e9e` |
| Fork Awesome | Embedded compressed font and IconFontCppHeaders definitions |

The standalone uses native Win32 windows and Dear ImGui's D3D9 backend.
Direct3D 9 loads dynamically; no SDL, OpenGL, Vulkan, or MIDI dependency remains.
See `../THIRD_PARTY.md` for license locations.
