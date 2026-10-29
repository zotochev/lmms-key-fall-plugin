# KeyFall

A Tool plugin for LMMS: a trainer for playing on a MIDI keyboard. It shows falling notes from the chosen MIDI clip in sync with playback, highlights the keys you press, and scores how accurately you hit the notes. The plan and the decisions behind it are in [PLAN.md](PLAN.md).

The full build guide from scratch, for Windows and Linux, along with solutions to common problems, is in [docs/BUILD.md](docs/BUILD.md).

## Building

### This branch: LMMS 1.2.x (Linux)

Branch `lmms-1.2` is KeyFall ported to the LMMS 1.2 plug-in API (`Pattern`, `MidiTime`, `Mixer`, ...), for the LMMS that distributions ship today. It is temporary: once LMMS 1.3 is released, `main` is the only line. New work happens on `main`; this branch only gets what is ported to it.

Needs CMake, a C++20 compiler, Qt 5 (Widgets, Xml) and the headers of the installed LMMS 1.2.x. LMMS itself is not linked: its symbols come from the running `lmms`.

```bash
cmake -S plugin -B build -DLMMS_INCLUDE_DIR=/usr/include/lmms
cmake --build build
cp build/libkeyfall.so ~/Documents/lmms/plugins/   # LMMS's working folder; or LMMS_PLUGIN_DIR=$PWD/build lmms
```

KeyFall then appears under Tools → KeyFall.

Differences from `main`: no clip start offset (LMMS 1.2 clips cannot be cropped on the left).

### Development (`main` branch): LMMS from source with KeyFall inside

The scripts expect an LMMS clone at the commit pinned in them (`bed447b`), by default in `~/Projects/lmms` (`%USERPROFILE%\Projects\lmms` on Windows; `LMMS_SRC` overrides). The plugin code stays in `plugin/`; `configure` links it into LMMS as `plugins/KeyFall`.

Windows (Visual Studio, Qt 6.8 in `C:\Qt`, vcpkg in `C:\vcpkg`):

```bat
scripts\lmms.cmd configure   :: the first run builds the vcpkg dependencies (20-60 min)
scripts\lmms.cmd build
scripts\lmms.cmd run         :: uses its own config, dev\lmmsrc-dev.xml
scripts\lmms.cmd test        :: core tests and layout checks (no LMMS needed)
```

Linux (Ubuntu 24.04, GCC, Qt 5):

```bash
scripts/lmms.sh deps         # build dependencies, asks for sudo
scripts/lmms.sh fetch        # LMMS at the pinned commit into ~/Projects/lmms
scripts/lmms.sh configure
scripts/lmms.sh build
scripts/lmms.sh run          # its own config, dev/lmmsrc-dev-linux.xml
scripts/lmms.sh test
```

## Scoring and the demo performer

- While the song plays KeyFall judges every press. Afterwards it shows Notes %, Rhythm %, stars, timing feedback and a per-bar heat map.
- **⚙ → Timing tolerance**: how strict the hit windows are (Loose / Normal / Strict).
- **⚙ → Calibrate latency…**: tap along with the falling bars, and KeyFall subtracts the delay of your keyboard and screen.
- **▶ Practise** (the panel under the toolbar): the clip plays on KeyFall's own clock while LMMS stays stopped. It offers:
  - **Wait for me**: time stops at each chord until you play it;
  - **Tempo %**;
  - **Loop bars**;
  - **Count-in** and **Click** (metronome).
- **⚙ → Auto-play (demo)**: KeyFall plays the clip itself, with small mistakes. Use it as a demo and to check KeyFall without a MIDI keyboard.

## Instrument layouts

KeyFall draws your keyboard (or pads) from a `*.kflayout.json` file. To add your own, see [docs/KEYBOARD_LAYOUTS.md](docs/KEYBOARD_LAYOUTS.md).

## Testing without a MIDI keyboard

1. Create a port named `lmms` in [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html).
2. Start LMMS with `scripts\lmms.cmd run`. For the instrument track to sound, click once in its instrument window: LMMS auto-assigns the `lmms` port when the instrument gets focus. Alternatively, use the track's ⚙ → MIDI → Input menu.
3. Play into it:

A ready-made test project with a melody and chords; the track's MIDI input is already connected to `lmms`:

```bat
python scripts\make_test_song.py      :: then in LMMS: File > Open > key-fall\dev\test-song.mmp
```

```bat
python scripts\virtual_player.py --list
python scripts\virtual_player.py --port lmms chords
python scripts\virtual_player.py --port lmms --jitter-ms 30 --wrong-rate 0.1 file song.mid
```
