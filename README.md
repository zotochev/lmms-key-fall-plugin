# KeyFall

A Tool plugin for LMMS: a trainer for playing on a MIDI keyboard. It shows falling notes from the chosen MIDI clip in sync with playback, highlights the keys you press, and scores how accurately you hit the notes. The plan and the decisions behind it are in [PLAN.md](PLAN.md).

## Environment (Windows)

| What | Where |
|---|---|
| LMMS source | `C:\Users\mikhail\Projects\lmms`, branch `keyfall`, based on commit `bed447b` |
| Patch for LMMS | `patches/lmms-0001-allow-msvc-2022.patch`, which allows building with VS 2022. Apply it with `git -C ..\lmms apply ..\key-fall\patches\lmms-0001-allow-msvc-2022.patch` |
| Toolchain | VS 2022, Qt 6.8.3 in `C:\Qt`, vcpkg in `C:\vcpkg` |

```bat
scripts\lmms.cmd configure   :: the first run builds the vcpkg dependencies (20-60 min)
scripts\lmms.cmd build
scripts\lmms.cmd run         :: uses its own config, dev\lmmsrc-dev.xml
```

## Testing without a MIDI keyboard

1. Create a port named `lmms` in [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html).
2. Start LMMS with `scripts\lmms.cmd run`. The `lmms` port gets connected to the instrument track automatically.
3. Play into it:

```bat
python scripts\virtual_player.py --list
python scripts\virtual_player.py --port lmms chords
python scripts\virtual_player.py --port lmms --jitter-ms 30 --wrong-rate 0.1 file song.mid
```
