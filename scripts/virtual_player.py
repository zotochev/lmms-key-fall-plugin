"""Virtual MIDI performer: plays notes into a MIDI output port instead of a real keyboard.

Used to test LMMS MIDI input and, later, KeyFall scoring without hardware.
It can play deliberately imperfectly (timing jitter, constant offset, wrong and
missed notes), so the scorer has something to judge.

Windows: talks to winmm.dll directly (no extra packages). The target port must be
a virtual loopback port (Windows MIDI Services loopback, loopMIDI, ...) that LMMS
can see as a MIDI input.
Linux: uses mido + python-rtmidi (`pip install mido python-rtmidi`); a virtual
ALSA port named "KeyFall Virtual Player" is created when --port is omitted.

Examples:
  python scripts/virtual_player.py --list
  python scripts/virtual_player.py --port loop scale --bpm 100
  python scripts/virtual_player.py --port loop chords --jitter-ms 40 --wrong-rate 0.1
  python scripts/virtual_player.py --port loop file song.mid --track 1 --miss-rate 0.05
  python scripts/virtual_player.py --dry-run scale        # print events, no MIDI port needed
"""

import argparse
import ctypes
import heapq
import random
import sys
import time
from dataclasses import dataclass


@dataclass(order=True)
class NoteEvent:
    start: float  # seconds from start
    length: float
    key: int
    velocity: int = 90


# ---------------------------------------------------------------- outputs

class WinMMOut:
    """Minimal MIDI output over winmm.dll (midiOutOpen / midiOutShortMsg)."""

    class MIDIOUTCAPSW(ctypes.Structure):
        _fields_ = [("wMid", ctypes.c_ushort), ("wPid", ctypes.c_ushort),
                    ("vDriverVersion", ctypes.c_uint), ("szPname", ctypes.c_wchar * 32),
                    ("wTechnology", ctypes.c_ushort), ("wVoices", ctypes.c_ushort),
                    ("wNotes", ctypes.c_ushort), ("wChannelMask", ctypes.c_ushort),
                    ("dwSupport", ctypes.c_uint)]

    def __init__(self, port_name):
        self.winmm = ctypes.WinDLL("winmm")
        names = self.list_ports()
        matches = [i for i, n in enumerate(names) if port_name.lower() in n.lower()]
        if not matches:
            raise SystemExit(f"No MIDI output matching '{port_name}'. Available: {names}")
        self.name = names[matches[0]]
        self.handle = ctypes.c_void_p()
        rc = self.winmm.midiOutOpen(ctypes.byref(self.handle), matches[0], None, None, 0)
        if rc != 0:
            raise SystemExit(f"midiOutOpen failed for '{self.name}' (code {rc})")

    @classmethod
    def list_ports(cls):
        winmm = ctypes.WinDLL("winmm")
        names = []
        for i in range(winmm.midiOutGetNumDevs()):
            caps = cls.MIDIOUTCAPSW()
            winmm.midiOutGetDevCapsW(i, ctypes.byref(caps), ctypes.sizeof(caps))
            names.append(caps.szPname)
        return names

    def send(self, status, data1, data2):
        self.winmm.midiOutShortMsg(self.handle, status | (data1 << 8) | (data2 << 16))

    def close(self):
        self.winmm.midiOutReset(self.handle)
        self.winmm.midiOutClose(self.handle)


class RtMidiOut:
    def __init__(self, port_name):
        import mido
        if port_name:
            names = mido.get_output_names()
            match = next((n for n in names if port_name.lower() in n.lower()), None)
            if match is None:
                raise SystemExit(f"No MIDI output matching '{port_name}'. Available: {names}")
            self.port = mido.open_output(match)
        else:
            self.port = mido.open_output("KeyFall Virtual Player", virtual=True)
        self.name = self.port.name

    @staticmethod
    def list_ports():
        import mido
        return mido.get_output_names()

    def send(self, status, data1, data2):
        import mido
        self.port.send(mido.Message.from_bytes([status, data1, data2]))

    def close(self):
        self.port.reset()
        self.port.close()


class DryRunOut:
    name = "dry-run"

    def send(self, status, data1, data2):
        kind = {0x90: "on ", 0x80: "off"}.get(status & 0xF0, hex(status))
        print(f"{time.perf_counter() - DryRunOut.t0:8.3f}s  {kind} key={data1:3d} vel={data2}")

    def close(self):
        pass


# ---------------------------------------------------------------- material

def scale(bpm, root=60, octaves=1):
    beat = 60.0 / bpm
    steps = [0, 2, 4, 5, 7, 9, 11]
    keys = [root + 12 * o + s for o in range(octaves) for s in steps] + [root + 12 * octaves]
    keys += keys[-2::-1]
    return [NoteEvent(i * beat, beat * 0.9, k) for i, k in enumerate(keys)]


def chords(bpm, root=60):
    beat = 60.0 / bpm
    progression = [(0, 4, 7), (5, 9, 12), (7, 11, 14), (0, 4, 7)]  # I - IV - V - I
    events = []
    for bar, chord in enumerate(progression):
        for k in chord:
            events.append(NoteEvent(bar * 4 * beat, 4 * beat * 0.95, root + k))
        events.append(NoteEvent(bar * 4 * beat, 4 * beat * 0.95, root - 12 + chord[0]))
    return sorted(events)


def from_file(path, track=None, channel=None, tempo_scale=1.0):
    import mido
    mid = mido.MidiFile(path)
    tracks = [mid.tracks[track]] if track is not None else mid.tracks
    tempo_track = mido.merge_tracks(mid.tracks)  # tempo map lives in any track
    tempo_changes, t = [], 0
    for msg in tempo_track:
        t += msg.time
        if msg.type == "set_tempo":
            tempo_changes.append((t, msg.tempo))

    def tick_to_sec(tick):
        sec, last_tick, tempo = 0.0, 0, 500000
        for change_tick, change_tempo in tempo_changes:
            if change_tick >= tick:
                break
            sec += mido.tick2second(change_tick - last_tick, mid.ticks_per_beat, tempo)
            last_tick, tempo = change_tick, change_tempo
        return sec + mido.tick2second(tick - last_tick, mid.ticks_per_beat, tempo)

    events = []
    for trk in tracks:
        t, open_notes = 0, {}
        for msg in trk:
            t += msg.time
            if msg.type not in ("note_on", "note_off"):
                continue
            if channel is not None and msg.channel != channel:
                continue
            if msg.type == "note_on" and msg.velocity > 0:
                open_notes[msg.note] = (t, msg.velocity)
            elif msg.note in open_notes:
                start, vel = open_notes.pop(msg.note)
                s, e = tick_to_sec(start), tick_to_sec(t)
                events.append(NoteEvent(s / tempo_scale, (e - s) / tempo_scale, msg.note, vel))
    if not events:
        raise SystemExit("No notes found (check --track / --channel)")
    first = min(e.start for e in events)
    return sorted(NoteEvent(e.start - first, e.length, e.key, e.velocity) for e in events)


# ---------------------------------------------------------------- performance

def humanize(events, args, rng):
    """Apply imperfections and return (played events, log of what was changed)."""
    played, log = [], []
    for e in events:
        if rng.random() < args.miss_rate:
            log.append(f"miss   key={e.key} at {e.start:.3f}s")
            continue
        key = e.key
        if rng.random() < args.wrong_rate:
            key = e.key + rng.choice([-2, -1, 1, 2])
            log.append(f"wrong  key={e.key}->{key} at {e.start:.3f}s")
        delta = args.offset_ms / 1000 + rng.gauss(0, args.jitter_ms / 1000) if args.jitter_ms else args.offset_ms / 1000
        played.append(NoteEvent(max(0.0, e.start + delta), e.length, key, e.velocity))
    return sorted(played), log


def perform(out, events, channel, count_in, bpm, loops):
    beat = 60.0 / bpm
    queue = []  # (time, order, status, key, velocity)
    order = 0
    for i in range(count_in):
        print(f"count-in {i + 1}/{count_in}")
        time.sleep(beat)
    for loop in range(loops):
        offset = loop * (max(e.start + e.length for e in events) + beat)
        for e in events:
            heapq.heappush(queue, (offset + e.start, order, 0x90 | channel, e.key, e.velocity))
            heapq.heappush(queue, (offset + e.start + e.length, order - 0.5, 0x80 | channel, e.key, 0))
            order += 1
    t0 = time.perf_counter()
    DryRunOut.t0 = t0
    while queue:
        when, _, status, key, vel = heapq.heappop(queue)
        while (remaining := t0 + when - time.perf_counter()) > 0:
            time.sleep(min(remaining, 0.002) if remaining > 0.004 else 0)
        out.send(status, key, vel)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--list", action="store_true", help="list MIDI output ports and exit")
    p.add_argument("--port", help="substring of the output port name")
    p.add_argument("--dry-run", action="store_true", help="print events instead of sending MIDI")
    p.add_argument("--channel", type=int, default=1, help="MIDI channel 1-16 (default 1)")
    p.add_argument("--bpm", type=float, default=100)
    p.add_argument("--count-in", type=int, default=4, help="beats to wait before playing")
    p.add_argument("--loops", type=int, default=1)
    p.add_argument("--jitter-ms", type=float, default=0, help="random timing error (std dev)")
    p.add_argument("--offset-ms", type=float, default=0, help="constant early(-)/late(+) offset")
    p.add_argument("--wrong-rate", type=float, default=0, help="probability of a wrong neighbouring key")
    p.add_argument("--miss-rate", type=float, default=0, help="probability of skipping a note")
    p.add_argument("--seed", type=int, help="random seed for reproducible runs")
    sub = p.add_subparsers(dest="material")
    sub.add_parser("scale", help="C major scale up and down")
    sub.add_parser("chords", help="I-IV-V-I chords with bass")
    f = sub.add_parser("file", help="notes from a .mid file")
    f.add_argument("path")
    f.add_argument("--track", type=int)
    f.add_argument("--from-channel", type=int, help="only notes from this channel (1-16)")
    f.add_argument("--tempo-scale", type=float, default=1.0, help="0.5 = half speed")
    args = p.parse_args()

    backend = WinMMOut if sys.platform == "win32" else RtMidiOut
    if args.list:
        for name in backend.list_ports():
            print(name)
        return
    if args.material is None:
        p.error("choose material: scale, chords or file")

    if args.material == "scale":
        events = scale(args.bpm)
    elif args.material == "chords":
        events = chords(args.bpm)
    else:
        ch = args.from_channel - 1 if args.from_channel else None
        events = from_file(args.path, args.track, ch, args.tempo_scale)

    rng = random.Random(args.seed)
    played, log = humanize(events, args, rng)
    for line in log:
        print(line)

    if args.dry_run:
        out = DryRunOut()
    elif sys.platform == "win32" and not args.port:
        p.error("--port is required on Windows (see --list)")
    else:
        out = backend(args.port)
    print(f"Playing {len(played)}/{len(events)} notes to '{out.name}', channel {args.channel}")
    try:
        perform(out, played, args.channel - 1, args.count_in, args.bpm, args.loops)
    except KeyboardInterrupt:
        print("stopped")
    finally:
        out.close()


if __name__ == "__main__":
    main()
