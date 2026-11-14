# USF Decoder (n64emu)

The foobar2000 component plays USF and miniUSF files through this checkout's
`n64emu_usf.dll`. PSF parsing, library resolution, CRC validation and miniz
decompression are integrated in `src/usf/usf.cpp`. The component uses that API
for metadata and playback, without a separate psflib or zlib library.

Metadata editing, fading, silence suppression, resampling and seeking are
supported. RSP audio executes natively.

## Dependencies supplied manually

Visual Studio C++ tools (v145), ATL and the Windows SDK are required.
The foobar2000 SDK and WTL are not bundled or downloaded by the build.
Supply these paths to `build-x64.ps1` or as MSBuild properties:

| Property | Required contents |
| --- | --- |
| `Fb2kSDKRoot` | SDK root containing `foobar2000/SDK`, `foobar2000/helpers`, `foobar2000/shared`, `pfc` and `libPPUI` |
| `Fb2kLibraryDir` | Matching `foobar2000_component_client.lib`, `foobar2000_sdk.lib`, `pfc.lib` and `libppui.lib` |
| `WTLIncludeDir` | WTL headers, including `atlapp.h` |

The SDK root must also contain `foobar2000/shared/shared-x64.lib` for x64
(or `shared-<Platform>.lib` for other project platforms). Use SDK libraries
built for the selected architecture, configuration and MSVC runtime.
The integration has been validated against SDK 20250307 and WTL 10.1.

Default paths are `deps/fb2k`, `deps/fb2k/lib/x64/Release` and
`deps/fb2k/wtl/include`. `deps/fb2k` is ignored by Git for manual installation.
Miniz is bundled under `deps/miniz`; no other third-party sources are needed
for PSF loading.

## Build and install

Build the engine from the repository root in MSYS2:

```sh
make -j8 platform=win64 USF=1
```

Then run from the repository root:

```powershell
.\src\foo_input_usf\build-x64.ps1 `
  -Fb2kSDKRoot C:\deps\fb2k `
  -Fb2kLibraryDir C:\deps\fb2k\lib\x64\Release `
  -WTLIncludeDir C:\deps\wtl\include
```

Use `-Rebuild` for a clean component rebuild. `-N64emuRoot` selects another
engine checkout (`-ParallaxRoot` is an alias). The engine DLL and export file
must already exist under `build/usf/x64`; an MSVC import library is generated
automatically. Other project platforms require matching engine and SDK builds.

Copy both `foo_input_usf.dll` and `n64emu_usf.dll` from
`build/foo_input_usf/Bin/x64/Release` into the same foobar2000 component directory
and restart foobar2000. The host supplies `shared.dll`. Keep referenced
`.usflib` files alongside their miniUSFs. The engine is delay-loaded from the
component directory, independently of the current working directory.

## Validation

`python tests/usf_psf.py build/usf/x64/n64emu_usf.dll` from the repository root
checks PSF library order, nested metadata, decompression growth and malformed
containers without the foobar2000 SDK or music files.

`tests/component_smoke.cpp` loads the actual component through a minimal SDK
host. `tests/build-smoke.cmd` accepts SDK root, library directory, WTL include
directory and a test SDK runtime directory containing `shared.lib` and
`shared.dll`. This test runtime is not part of the component distribution.
It writes the executable under `build/foo_input_usf/smoke`.

Run `component_smoke.exe <absolute-component-path>` to test loading and format
registration. Add the absolute path to `27b Air Force One.miniusf` to check
library resolution, metadata, stereo PCM, native/48 kHz output and backward
seeking.
