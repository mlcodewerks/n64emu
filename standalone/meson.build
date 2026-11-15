project('n64emu-standalone', 'c', 'cpp', version: '0.1.0', license: 'BSD-3-Clause', default_options: ['cpp_std=c++17', 'c_std=gnu11', 'buildtype=release', 'warning_level=2'])
if host_machine.system() != 'windows' or host_machine.cpu_family() != 'x86_64'
  error('winweg supports Windows x64 only.')
endif
cpp = meson.get_compiler('cpp')
add_project_arguments('-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN', '-DUNICODE', '-D_UNICODE', '-D_WIN32_WINNT=0x0A00', language: ['c', 'cpp'])
if cpp.get_id() == 'gcc'
  add_project_link_arguments('-static', '-static-libgcc', '-static-libstdc++', language: 'cpp')
endif
inc = include_directories('src', 'third_party/imgui', 'third_party/ImGuiColorTextEdit', 'third_party/libretro', 'third_party/librashader', 'third_party/fonts', '../src', 'third_party/cjson')
deps = [dependency('threads')]
foreach lib : ['dinput8', 'dxguid', 'xinput1_4', 'ole32', 'uuid', 'user32', 'gdi32', 'shell32', 'dwmapi', 'imm32', 'avrt']
  deps += cpp.find_library(lib)
endforeach
if cpp.get_id() == 'gcc'
  deps += cpp.find_library('winpthread', static: true)
endif
subdir('third_party/libretro-dsp')
deps += dsp_dep
platform = static_library('platform', ['src/common.cpp', 'src/config_store.cpp', 'third_party/cjson/cJSON.c', 'src/command_line.cpp', 'src/log.cpp', 'src/log_window.cpp', 'src/core_options_window.cpp', 'src/rom_browser.cpp', 'src/ui_fonts.cpp', 'src/video.cpp', 'src/audio.cpp', 'src/input.cpp', 'src/input_bindings.cpp', 'src/input_model.cpp', 'src/input_window.cpp', 
  'src/shader_chain.cpp', 'src/shader_chain_window.cpp', 'src/shader_source_editor.cpp', 'third_party/ImGuiColorTextEdit/TextEditor.cpp',
  'src/shader_library.cpp', 'src/shader_runtime.cpp', 'src/shader_diagnostics.cpp', 'src/shader_source_io.cpp', 'src/dsp_chain.cpp', 'src/dsp_chain_window.cpp',
  'third_party/imgui/imgui.cpp', 'third_party/imgui/imgui_draw.cpp', 'third_party/imgui/imgui_tables.cpp', 'third_party/imgui/imgui_widgets.cpp',
  'third_party/imgui/imgui_impl_win32.cpp', 'third_party/imgui/imgui_impl_dx9.cpp'],
  include_directories: inc, dependencies: deps)

# The core is built with its own native build rules, including scale variants and
# runtime SIMD dispatch. build_core.py creates an archive without libretro.c.
python = find_program('python3')
core_archive = custom_target('n64emu-core', output: 'n64emu_core.a',
  command: [python, files('tools/build_core.py'), '@OUTPUT@'],
  build_always_stale: true)
strip_release = get_option('strip_release') and get_option('buildtype') in ['release', 'minsize']
app_link_args = [core_archive.full_path(), '-Wl,-Map=' + meson.current_build_dir() / 'n64emu.map']
if strip_release
  # Strip during linking so the build-directory executable is ready to ship.
  # Meson's built-in strip option applies only at installation time.
  if not cpp.has_link_argument('-Wl,--strip-all')
    error('strip_release requires a linker supporting --strip-all.')
  endif
  app_link_args += ['-Wl,--strip-all']
endif
app = executable('n64emu', ['src/main.cpp', 'src/core.cpp'], include_directories: inc,
  link_with: platform, dependencies: deps, link_args: app_link_args,
  link_depends: core_archive, win_subsystem: 'windows', install: true)
font_tests = executable('font-tests', 'tests/font_tests.cpp', include_directories: inc, link_with: platform, dependencies: deps)
test('embedded-fonts', font_tests, timeout: 15)
dialog_tests = executable('config-dialog-tests', 'tests/config_dialog_tests.cpp', include_directories: inc, link_with: platform, dependencies: deps)
test('configuration-dialogs', dialog_tests, timeout: 15)
config_tests = executable('config-store-tests', 'tests/config_store_tests.cpp', include_directories: inc, link_with: platform, dependencies: deps)
test('unified-json-configuration', config_tests, args: [meson.current_build_dir() / 'config-test'], timeout: 30)
input_tests = executable('input-tests', 'tests/input_tests.cpp', include_directories: inc, link_with: platform, dependencies: deps)
test('native-input-bindings', input_tests, args: [meson.current_build_dir() / 'input-test'], timeout: 30)
browser_tests = executable('browser-tests', 'tests/rom_browser_smoke.cpp', include_directories: inc, link_with: platform, dependencies: deps)
test('rom-browser-open-save', browser_tests, args: [meson.current_build_dir() / 'browser-test'], timeout: 30)
api_tests = executable('core-api-tests', 'tests/core_api_tests.c', include_directories: inc,
  link_args: [core_archive.full_path(), '-static', '-lstdc++', '-lwinpthread'], link_depends: core_archive)
test('native-core-api', api_tests, timeout: 30)
test('standalone-cli', python, args: [files('tests/standalone_cli.py'), app], timeout: 30)
test('static-compiler-runtimes', python,
  args: [files('tests/verify_static_runtime.py'), find_program('objdump'), app], timeout: 15)
test('native-static-linkage', python,
  args: [files('tests/native_linkage.py'), find_program('nm'), find_program('ar'), app, core_archive,
    meson.current_build_dir() / 'n64emu.map', strip_release ? 'stripped' : 'symbols'], timeout: 15)
d3d9_tests = executable('d3d9-tests', 'tests/d3d9_tests.cpp', include_directories: inc,
  link_with: platform, dependencies: deps)
test('d3d9-gpu-reset', d3d9_tests, args: [meson.current_build_dir() / 'd3d9-test.bmp'], timeout: 30)
