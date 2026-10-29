# KeyFall

A Tool plugin for LMMS: a trainer for playing on a MIDI keyboard. It shows falling notes from the chosen MIDI clip in sync with playback, highlights the keys you press, and scores how accurately you hit the notes. The plan and the decisions behind it are in [PLAN.md](PLAN.md).

The full build guide from scratch, for Windows and Linux, along with solutions to common problems, is in [docs/BUILD.md](docs/BUILD.md).

## Environment (Windows)

| What | Where |
|---|---|
| LMMS source | `C:\Users\mikhail\Projects\lmms`, branch `keyfall`, based on commit `bed447b` |
| Patches for LMMS | None are needed for the plugin itself. `patches/lmms-0001-…` only lets LMMS build with VS 2022 |
| Plugin code | `plugin/`. LMMS sees it as `plugins/KeyFall` through a junction, which `scripts\lmms.cmd configure` creates |
| Toolchain | VS 2022, Qt 6.8.3 in `C:\Qt`, vcpkg in `C:\vcpkg` |

```bat
scripts\lmms.cmd configure   :: the first run builds the vcpkg dependencies (20-60 min)
scripts\lmms.cmd build
scripts\lmms.cmd run         :: uses its own config, dev\lmmsrc-dev.xml
scripts\lmms.cmd test        :: core tests and layout checks (no LMMS needed)
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
