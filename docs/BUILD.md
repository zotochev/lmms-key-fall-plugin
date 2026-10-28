# Building LMMS + KeyFall

This guide sets up the environment from scratch. LMMS is built from source, and the KeyFall plugin will later be built as part of it, from `plugins/KeyFall`.

| Parameter | Value |
|---|---|
| LMMS | `master`, pinned to commit `bed447baab187ea6292449048e0b1aab16082e3b` (2026-10-25), local branch `keyfall` |
| Windows | MSVC (VS 2022 17.11+ or VS 2026) + vcpkg + Qt 6.8 |
| Linux | GCC + Qt 5.15 from the distro packages |
| Disk space | ~15 GB (vcpkg dependencies make up most of it) |

Directory layout: the two repositories live **side by side**.
```
Projects/
  key-fall/   ← this repository: plan, scripts, patches, and later the plugin code
  lmms/       ← LMMS source code (separate git repository)
```

---

## Windows

The steps below were tested on Windows 11, VS 2022 17.11 (MSVC 19.41) and Qt 6.8.3.

### 1. Tools

1. **Visual Studio 2022 or 2026** (Community or Build Tools), with the **Desktop development with C++** workload. It must include:
   - MSVC x64/x86 build tools
   - Windows 11 SDK
   - C++ CMake tools for Windows (this installs CMake and Ninja; you don't need to install them separately)
2. **Git** (with Git Bash).
3. **Python 3.10+**, for installing Qt and for the test scripts.

### 2. Qt 6.8 (prebuilt binaries)

Install Qt through `aqtinstall`, the same way as the LMMS CI does:

```bat
python -m pip install --user aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
```

In Qt 6, `qtsvg` and `qttools` are already part of the base package. If you pass them with `-m`, aqt fails with "packages not found".
Qt builds labelled msvc2022 also work with MSVC 2026.

### 3. vcpkg

```bat
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

You need exactly this standalone clone. The copy of vcpkg bundled with Visual Studio doesn't fit, because it requires `builtin-baseline` in the manifest, and LMMS's `vcpkg.json` doesn't have one.

### 4. LMMS source and the patch

```bat
cd C:\Users\<you>\Projects
git clone https://github.com/LMMS/lmms.git
cd lmms
git checkout -b keyfall bed447baab187ea6292449048e0b1aab16082e3b
git submodule update --init --recursive
```

Apply the KeyFall patches:

```bat
git apply ..\key-fall\patches\lmms-0002-keyfall-plugin-and-midi-exports.patch
git apply ..\key-fall\patches\lmms-0001-allow-msvc-2022.patch
```

| Patch | Why |
|---|---|
| `0002-keyfall-plugin-and-midi-exports` | Adds `KeyFall` to `LMMS_PLUGIN_LIST` and marks `MidiPort`, `MidiClient` and `MidiPortMenu` with `LMMS_EXPORT`. Without the export, the plugin won't link, because symbols in LMMS are hidden by default. **Required on every OS.** |
| `0001-allow-msvc-2022` | **VS 2022 only.** LMMS master requires MSVC 19.51 (VS 2026). The source code doesn't need it; the restriction is a version check in CMake. Skip this patch if you have VS 2026 |

The plugin code itself is not copied into LMMS. On the first run, `scripts\lmms.cmd configure` creates the junction `lmms\plugins\KeyFall → key-fall\plugin`. To keep `git status` in LMMS clean, add it to the exclusions once:

```bat
echo plugins/KeyFall>> .git\info\exclude
```

### 5. Configure and build

All commands go through [`scripts/lmms.cmd`](../scripts/lmms.cmd). It sets up the MSVC environment and the paths to CMake, Ninja, Qt and vcpkg. It can be run from any console.

```bat
cd C:\Users\<you>\Projects\key-fall
scripts\lmms.cmd configure
scripts\lmms.cmd build
scripts\lmms.cmd run
```

| Command | What it does | Time |
|---|---|---|
| `configure` | CMake + `vcpkg install` from the LMMS manifest (fftw, fluidsynth, sndfile, SDL2, lilv, …) | first run takes **20–60 min**, later runs take seconds |
| `build` | Ninja build into `lmms\build` | first build ~10–20 min, incremental builds are fast |
| `run` | starts `lmms\build\lmms.exe` with the vcpkg DLLs on PATH and the separate config `dev\lmmsrc-dev.xml` | — |

Default paths can be overridden with environment variables:

| Variable | Default |
|---|---|
| `LMMS_SRC` | `%USERPROFILE%\Projects\lmms` |
| `LMMS_BUILD` | `%LMMS_SRC%\build` |
| `KEYFALL_VCPKG` | `C:\vcpkg` |
| `QT_DIR` | `C:\Qt\6.8.3\msvc2022_64` |

In `scripts\lmms.cmd` the Visual Studio path is set to `...\2022\Community`. For Build Tools or VS 2026, change `VS_DIR`.

After building, KeyFall opens from the **Tools → KeyFall** menu.

To keep rebuilds fast, `configure` builds only a minimal set of plugins plus `KeyFall`, `TapTempo` and `MidiImport`. For a full set, remove `-DLMMS_MINIMAL=ON` and `-DPLUGIN_LIST=...` from the script. Some plugins are not built anyway: Sid and the SWH LADSPA plugins need Perl, GIG needs libgig, SUIL needs suil. We don't need any of them.

### 6. Separate LMMS config

The development build uses its own config, `dev\lmmsrc-dev.xml`. The first `run` creates it from `dev\lmmsrc-dev.template.xml`. This keeps it from touching `~\.lmmsrc.xml`, which belongs to an installed LMMS. LMMS rewrites the config while it runs, so the config itself is not in git.

The template already sets:
- audio: SDL, buffer of 256 frames;
- MIDI: WinMM;
- `midiautoassign="lmms"`: the selected instrument track automatically subscribes to the MIDI port `lmms`.

---

## Testing without a MIDI keyboard

1. Install **loopMIDI** (Tobias Erichsen) and create a port named **`lmms`** with the **+** button.
   You must create the port **before** starting LMMS: WinMM gives LMMS the list of devices only once, at startup.
2. `scripts\lmms.cmd run`. The TripleOscillator track will subscribe to `lmms` automatically. To check, open the track's ⚙ menu → MIDI → Input; `lmms` should have a check mark.
3. Play into the port:

```bat
python scripts\virtual_player.py --list
python scripts\virtual_player.py --port lmms scale --bpm 120
python scripts\virtual_player.py --port lmms chords --loops 2
python scripts\virtual_player.py --port lmms --jitter-ms 30 --offset-ms 20 --wrong-rate 0.1 --miss-rate 0.05 --seed 1 file song.mid --track 1
python scripts\virtual_player.py --dry-run chords
```

On Windows the script calls `winmm.dll` directly. The only extra package is `mido`, which is needed just to read `.mid` files: `pip install --user mido`. For `python-rtmidi` there is no ready-made build for Python 3.13 on Windows, and you don't need one.

---

## Linux (Ubuntu 24.04) — not yet tested

The package list is taken from the LMMS CI (`.github/workflows/deps-ubuntu-24.04-gcc.txt`). This is the minimum we need:

```bash
sudo apt-get install -y --no-install-recommends \
  build-essential cmake git pkg-config ccache \
  qtbase5-dev qtbase5-private-dev qtbase5-dev-tools qttools5-dev-tools libqt5svg5-dev libqt5x11extras5-dev \
  libasound2-dev libjack-jackd2-dev portaudio19-dev libsdl2-dev \
  libsndfile1-dev libfftw3-dev libsamplerate0-dev libvorbis-dev libogg-dev libmp3lame-dev \
  libfluidsynth-dev liblilv-dev lv2-dev libstk-dev stk \
  libx11-xcb-dev libxcb-keysyms1-dev libxcb-util0-dev libxft-dev libxinerama-dev
```

```bash
cd ~/Projects
git clone https://github.com/LMMS/lmms.git && cd lmms
git checkout -b keyfall bed447baab187ea6292449048e0b1aab16082e3b
git submodule update --init --recursive
git apply ../key-fall/patches/lmms-0002-keyfall-plugin-and-midi-exports.patch
ln -s ../../key-fall/plugin plugins/KeyFall && echo plugins/KeyFall >> .git/info/exclude
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLMMS_MINIMAL=ON -DPLUGIN_LIST="KeyFall TapTempo MidiImport"
cmake --build build -j"$(nproc)"
./build/lmms
```

On Linux you only need patch `0002`; the MSVC patch is not needed.
To test without a keyboard: `pip install mido python-rtmidi`, then `python scripts/virtual_player.py chords`. Without `--port` the script creates its own ALSA port, `KeyFall Virtual Player`; connect it to LMMS in the track's MIDI → Input menu.

---

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `'vswhere.exe' is not recognized`, then `CMAKE_CXX_COMPILER not set` | `vcvars64.bat` can't find vswhere. `lmms.cmd` already adds `...\Microsoft Visual Studio\Installer` to PATH |
| `this vcpkg instance requires a manifest with a specified baseline` | The Visual Studio vcpkg was picked up (`vcvars` overrides `VCPKG_ROOT`). Use the clone in `C:\vcpkg`; the script points the toolchain at it explicitly |
| `Unsupported MSVC version '19.41…'. MSVC 19.51 or higher is required` | Apply `patches/lmms-0001-allow-msvc-2022.patch` or install VS 2026 |
| After a failed `configure`, things keep failing | Delete `lmms\build` (the vcpkg packages stay in the `C:\vcpkg` cache) |
| `lmms.exe` won't start, a DLL is missing | Start it through `scripts\lmms.cmd run`; it adds `build\vcpkg_installed\x64-windows\bin` and Qt to PATH |
| LMMS doesn't see the loopMIDI port | The port was created after LMMS started. Restart LMMS |
| `unresolved external symbol ... MidiPort::MidiPort` when linking `keyfall.dll` | Patch `0002` (`LMMS_EXPORT`) was not applied, or LMMS was not rebuilt after it |
| `add_subdirectory given source "KeyFall" which is not an existing directory` | The `lmms\plugins\KeyFall` junction is missing. Run `scripts\lmms.cmd configure`, which creates it |
| KeyFall is missing from the Tools menu | `keyfall.dll` is not in `build\plugins`: check that `KeyFall` is in `PLUGIN_LIST` |
| Notes arrive (the keys light up) but there is no sound | Edit → Settings → Audio: check the device. Click a key on the instrument's keyboard with the mouse to test |
