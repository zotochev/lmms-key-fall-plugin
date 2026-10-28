"""Generate dev/test-song.mmp: an LMMS project for testing KeyFall without a real song.

"Ode to Joy" (Beethoven, public domain) in the right hand plus whole-note chords in
the left hand, 100 BPM, one clip on bars 2-5 of the TripleOscillator track. The
track's MIDI input is already connected to the loopMIDI port "lmms", so it plays
what scripts/virtual_player.py sends without clicking anything in LMMS.

The project is based on LMMS's default template, which declares itself as LMMS
1.2.0. When LMMS loads files that old, upgrade_extendedNoteRange() moves every
note up an octave, so the keys are written 12 lower than the MIDI notes meant.

Usage: python scripts/make_test_song.py [path/to/lmms]   (default: ../lmms)
"""

import os
import sys

LMMS_SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "lmms")
OUT = os.path.join(os.path.dirname(__file__), "..", "dev", "test-song.mmp")
OLD_FORMAT_KEY_SHIFT = 12  # see upgrade_extendedNoteRange() in LMMS src/core/DataFile.cpp
Q = 48  # LMMS ticks per quarter note

MELODY = [  # (MIDI note, quarters)
    (64, 1), (64, 1), (65, 1), (67, 1), (67, 1), (65, 1), (64, 1), (62, 1),
    (60, 1), (60, 1), (62, 1), (64, 1), (64, 1.5), (62, 0.5), (62, 2),
]
CHORDS = [(48, 52, 55), (43, 47, 50), (48, 52, 55), (43, 47, 50)]  # C G C G, one per bar


def main():
    with open(os.path.join(LMMS_SRC, "data", "projects", "templates", "default.mpt"), encoding="utf-8") as f:
        src = f.read()
    src = src.replace('bpm="140"', 'bpm="100"')
    # The template keeps the track's MIDI input off (readable="0"); connect it to loopMIDI
    src = src.replace('basevelocity="63" readable="0"/>', 'basevelocity="63" readable="1" inports="lmms"/>', 1)

    notes = []
    t = 0
    for key, quarters in MELODY:
        notes.append((t, int(quarters * Q), key, 100))
        t += int(quarters * Q)
    for bar, chord in enumerate(CHORDS):
        for key in chord:
            notes.append((bar * 4 * Q, 4 * Q - 6, key, 70))

    clip = '<pattern pos="192" len="768" name="Ode to Joy" muted="0" type="1" steps="16">\n'
    clip += "".join(
        f'          <note pos="{p}" len="{l}" key="{k - OLD_FORMAT_KEY_SHIFT}" vol="{v}" pan="0"/>\n'
        for p, l, k, v in sorted(notes))
    clip += "        </pattern>\n"
    src = src.replace("        </instrumenttrack>\n      </track>",
                      "        </instrumenttrack>\n        " + clip + "      </track>", 1)

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(src)
    print(f"{os.path.normpath(OUT)}: {len(notes)} notes")


if __name__ == "__main__":
    main()
