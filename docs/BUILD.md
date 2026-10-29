# Building LMMS + KeyFall

This guide sets up the environment from scratch. LMMS is built from source, and the KeyFall plugin will later be built as part of it, from `plugins/KeyFall`.

| Parameter | Value |
|---|---|
| LMMS | `master`, pinned to commit `bed447baab187ea6292449048e0b1aab16082e3b` (2026-10-25), local branch `keyfall` |
| Windows | MSVC (VS 2026, as LMMS master requires) + vcpkg + Qt 6.8 |
| Linux | GCC + Qt 5.15 from the distro packages |
| Disk space | ~15 GB (vcpkg dependencies make up most of it) |

Directory layout: the two repositories live **side by side**.
```
Projects/
  key-fall/   ← this repository: plugin code, scripts, docs
  lmms/       ← LMMS source code (separate git repository)
```

---

## Windows

The steps below were tested on Windows 11 with Qt 6.8.3.

### 1. Tools

1. **Visual Studio 2026** (Community or Build Tools), with the **Desktop development with C++** workload. It must include:
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

### 4. LMMS source

```bat
cd C:\Users\<you>\Projects
git clone https://github.com/LMMS/lmms.git
cd lmms
git checkout -b keyfall bed447baab187ea6292449048e0b1aab16082e3b
git submodule update --init --recursive
```

KeyFall needs no changes to LMMS: it uses only classes LMMS already exports, and it gets into the build through `-DPLUGIN_LIST`.

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
| `test` | KeyFall core tests without LMMS and Qt: unit tests, the layout test set through both checkers (C++ `kflayout-lint` and Python), and checks of the built-in layouts. Builds into `key-fall\build-tests` | ~1 min the first time |

Default paths can be overridden with environment variables:

| Variable | Default |
|---|---|
| `LMMS_SRC` | `%USERPROFILE%\Projects\lmms` |
| `LMMS_BUILD` | `%LMMS_SRC%\build` |
| `KEYFALL_VCPKG` | `C:\vcpkg` |
| `QT_DIR` | `C:\Qt\6.8.3\msvc2022_64` |

`scripts\lmms.cmd` finds the newest Visual Studio with the C++ tools through `vswhere`. To use another installation, set `VS_DIR` to its folder.

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

## Linux (Ubuntu 24.04)

To build only the plug-in against an LMMS that is already installed with its headers, no LMMS checkout is needed: see the README, section "Linux: just the plug-in" (`cmake -S plugin -B build -DLMMS_INCLUDE_DIR=...`). The rest of this section builds LMMS itself from source, for development.

Tested 2026-10-29 in WSL 2 (Ubuntu 24.04.4, GCC 13, Qt 5.15):
- the KeyFall core builds with `-Wall -Wextra -Wpedantic` without warnings, and all test sets pass;
- LMMS with the plugin builds, starts and loads `libkeyfall.so`; this was checked by an offscreen launch.

All steps go through [`scripts/lmms.sh`](../scripts/lmms.sh), the Linux counterpart of `lmms.cmd`:

```bash
cd ~/Projects/key-fall
scripts/lmms.sh deps        # build dependencies (Ubuntu/Debian, asks for sudo)
scripts/lmms.sh fetch       # clone LMMS at the pinned commit into ~/Projects/lmms
scripts/lmms.sh configure   # link plugin/ into LMMS as plugins/KeyFall, run CMake
scripts/lmms.sh build
scripts/lmms.sh run         # or: scripts/lmms.sh run dev/test-song.mmp
```

| Command | What it does |
|---|---|
| `deps` | Installs the packages from [`scripts/linux-deps.sh`](../scripts/linux-deps.sh) (the LMMS CI list, trimmed to what we need) |
| `fetch` | Clones LMMS, checks out the pinned commit on branch `keyfall`, fetches submodules. Does nothing if LMMS is already there |
| `configure` | Creates the symlink `plugins/KeyFall → key-fall/plugin` (and hides it from LMMS's `git status`), then CMake with the minimal plug-in set plus KeyFall, `TapTempo`, `MidiImport`. Uses Ninja when installed |
| `build` | Builds LMMS + KeyFall on all cores (`JOBS=4` to limit) |
| `run` | Starts the built LMMS with its own config `dev/lmmsrc-dev-linux.xml` (SDL audio, ALSA sequencer MIDI), created on first run; `~/.lmmsrc.xml` of an installed LMMS stays untouched. Extra arguments go to LMMS |
| `test` | KeyFall core tests without LMMS and Qt, built into `build-tests-linux` |

Paths can be overridden with `LMMS_SRC` (default `~/Projects/lmms`) and `LMMS_BUILD` (default `$LMMS_SRC/build`).

On Linux LMMS needs no patches.

**WSL.** Keep LMMS on the Linux file system (`~/...`), not under `/mnt/c`, because building there is many times faster. The repository itself can stay in the Windows copy: run `/mnt/c/Users/<you>/Projects/key-fall/scripts/lmms.sh ...`, and the symlink points there. WSLg shows the LMMS window. WSL has no sound card or ALSA MIDI, so ALSA messages at startup are expected.

To test without a keyboard: `pip install mido python-rtmidi`, then `python scripts/virtual_player.py chords`. Without `--port` the script creates its own ALSA port, `KeyFall Virtual Player`; connect it to LMMS in the track's MIDI → Input menu. This needs a real Linux machine with ALSA, not WSL.

---

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `'vswhere.exe' is not recognized`, then `CMAKE_CXX_COMPILER not set` | `vcvars64.bat` can't find vswhere. `lmms.cmd` already adds `...\Microsoft Visual Studio\Installer` to PATH |
| `this vcpkg instance requires a manifest with a specified baseline` | The Visual Studio vcpkg was picked up (`vcvars` overrides `VCPKG_ROOT`). Use the clone in `C:\vcpkg`; the script points the toolchain at it explicitly |
| `Unsupported MSVC version '19.41…'. MSVC 19.51 or higher is required` | LMMS master accepts only VS 2026. The check is in LMMS's `cmake/modules/DetectMachine.cmake`; the code itself also builds with VS 2022 if you relax it locally |
| After a failed `configure`, things keep failing | Delete `lmms\build` (the vcpkg packages stay in the `C:\vcpkg` cache) |
| `lmms.exe` won't start, a DLL is missing | Start it through `scripts\lmms.cmd run`; it adds `build\vcpkg_installed\x64-windows\bin` and Qt to PATH |
| LMMS doesn't see the loopMIDI port | The port was created after LMMS started. Restart LMMS |
| `add_subdirectory given source "KeyFall" which is not an existing directory` | The `lmms\plugins\KeyFall` junction is missing. Run `scripts\lmms.cmd configure`, which creates it |
| KeyFall is missing from the Tools menu | `keyfall.dll` is not in `build\plugins`: check that `KeyFall` is in `PLUGIN_LIST` |
| KeyFall shows "MIDI in: no track" and keys don't light up | KeyFall hears the keyboard through instrument tracks, so at least one track must take MIDI input. After LMMS starts this is often off: auto-assign (`midiautoassign`) connects the port when the track is created, but loading the project template then turns its MIDI input off. Auto-assign happens again when the instrument's keyboard gets focus. Click once in the track's instrument window (or in Piano Roll), or tick the port in the track's ⚙ → MIDI → Input |
| A held key stays lit after release | The sustain pedal is down: LMMS reports the release only when the pedal comes up |
| Notes arrive (the keys light up) but there is no sound | Edit → Settings → Audio: check the device. Click a key on the instrument's keyboard with the mouse to test |
