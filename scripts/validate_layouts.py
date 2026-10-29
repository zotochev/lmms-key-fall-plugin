"""Validate KeyFall layout files (*.kflayout.json) without building anything.

Mirrors the rules of plugin/core/KeyboardLayout.cpp; both are checked against the
same corpus (tests/layouts), so they must report the same error codes.
Only the Python standard library is needed.

Usage:
  python scripts/validate_layouts.py my-keyboard.kflayout.json [more files...]
  python scripts/validate_layouts.py --strict plugin/layouts/*.kflayout.json   # warnings fail too
  python scripts/validate_layouts.py --corpus tests/layouts                     # self-test
"""

import glob
import json
import os
import re
import sys

CURRENT_FORMAT = 2
NUM_PITCHES = 128
MAX_CONTROLS = 1024
MAX_GRID_SIDE = 32
SUPPORTED_FEATURES = ("states", "rows")
ID_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
NOTE_RE = re.compile(r"^([A-G])([#b]?)(-?\d{1,2})$")
LETTER_PITCH = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


class DuplicateKey(ValueError):
    pass


def _no_duplicates(pairs):
    seen = {}
    for key, value in pairs:
        if key in seen:
            raise DuplicateKey(f'duplicate key "{key}"')
        seen[key] = value
    return seen


def is_int(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and float(v).is_integer()


def parse_note_name(name):
    m = NOTE_RE.match(name)
    if not m:
        return None
    pc = LETTER_PITCH[m.group(1)] + {"#": 1, "b": -1, "": 0}[m.group(2)]
    pitch = (int(m.group(3)) + 1) * 12 + pc
    return pitch if 0 <= pitch < NUM_PITCHES else None


class Checker:
    def __init__(self):
        self.diagnostics = []  # (severity, code, pointer, message)
        self.failed = False
        self.controls = 0
        self.zone_ids = set()
        self.state_values = None  # list of state names when the layout declares "states"
        self.default_state = 0

    def error(self, code, pointer, message):
        self.failed = True
        self.diagnostics.append(("error", code, pointer, message))

    def warning(self, code, pointer, message):
        self.diagnostics.append(("warning", code, pointer, message))

    def check_keys(self, obj, allowed, pointer):
        for key in obj:
            if key not in allowed:
                self.warning("W001", f"{pointer}/{key}", f'unknown key "{key}" is ignored')

    def opt_string(self, obj, key, pointer):
        if key not in obj:
            return None
        if not isinstance(obj[key], str):
            self.error("E014", f"{pointer}/{key}", f'"{key}" must be a string')
            return None
        return obj[key]

    def note(self, v, pointer):
        if is_int(v) and 0 <= v < NUM_PITCHES:
            return int(v)
        if isinstance(v, str) and parse_note_name(v) is not None:
            return parse_note_name(v)
        self.error("E007", pointer, 'invalid note: use a MIDI number 0-127 or a name like "C4", "F#3", "Bb2"')
        return None

    def int_in_range(self, obj, key, lo, hi, pointer, code):
        if key not in obj:
            return None
        v = obj[key]
        if not is_int(v) or v < lo or v > hi:
            self.error(code, f"{pointer}/{key}", f'"{key}" must be an integer from {lo} to {hi}')
            return None
        return int(v)

    def channel(self, v, pointer):
        if v == "any" or (is_int(v) and 1 <= v <= 16):
            return
        self.error("E010", pointer, 'channel must be "any" or a number from 1 to 16')

    # ------------------------------------------------------------------ document

    def migrate_v1(self, root):
        if not isinstance(root, dict) or root.get("format") != 1 or isinstance(root.get("format"), bool):
            return root
        out = dict(root)
        out["format"] = CURRENT_FORMAT
        if "octaveShift" in out:
            del out["octaveShift"]
            self.warning("W002", "/octaveShift", 'format 1 "octaveShift" is not supported yet and is ignored')
        if isinstance(out.get("zones"), list):
            zones = []
            for z in out["zones"]:
                if isinstance(z, dict) and "channel" in z:
                    z = dict(z)
                    z["input"] = {"channel": z.pop("channel")}
                zones.append(z)
            out["zones"] = zones
        return out

    def check(self, root):
        root = self.migrate_v1(root)
        if not isinstance(root, dict):
            self.error("E014", "", "layout must be a JSON object")
            return
        self.check_keys(root, {"$schema", "format", "id", "name", "author", "license", "description", "match",
                               "units", "piano", "zones", "requires", "states"}, "")
        self.check_meta(root)
        if "requires" in root:
            if not isinstance(root["requires"], list):
                self.error("E014", "/requires", '"requires" must be an array of feature names')
            else:
                for i, feature in enumerate(root["requires"]):
                    if feature not in SUPPORTED_FEATURES:
                        name = feature if isinstance(feature, str) else "?"
                        self.error("E012", f"/requires/{i}", f'feature "{name}" needs a newer KeyFall version')
        if "states" in root:
            self.check_states(root["states"])

        if "piano" in root and "zones" in root:
            self.error("E015", "", 'use either "piano" or "zones", not both')
        elif "piano" in root:
            self.check_piano_shorthand(root["piano"])
        elif "zones" in root:
            self.check_zones(root["zones"])

        if not self.failed and self.controls == 0:
            self.error("E005", "", 'the layout has no controls: add "piano" or a non-empty "zones" list')
        if self.controls > MAX_CONTROLS:
            self.error("E013", "", f"too many controls ({self.controls} > {MAX_CONTROLS})")

    def check_states(self, states):
        if not isinstance(states, dict) or not states:
            self.error("E017", "/states", '"states" must be an object with one state, e.g. {"bellows": {...}}')
            return
        if len(states) > 1:
            self.error("E012", "/states", "more than one state variable needs a newer KeyFall version")
            return
        name, d = next(iter(states.items()))
        ptr = f"/states/{name}"
        if not isinstance(d, dict):
            self.error("E017", ptr, "a state must be an object")
            return
        self.check_keys(d, {"values", "cc", "ranges", "channel", "inferred", "manual", "default"}, ptr)
        values = d.get("values")
        if not isinstance(values, list) or len(values) < 2:
            self.error("E017", f"{ptr}/values", '"values" must list at least two state names, e.g. ["push", "pull"]')
            return
        seen = []
        for i, v in enumerate(values):
            if not isinstance(v, str) or not v or v in seen:
                self.error("E017", f"{ptr}/values/{i}", "state names must be unique, non-empty strings")
                return
            seen.append(v)
        inferred = d.get("inferred") is True
        manual = d.get("manual") is True
        sources = ("cc" in d) + ("channel" in d) + inferred + manual
        if sources != 1:
            self.error("E017", ptr, 'a state needs exactly one source: "cc" (+ "ranges"), "channel", '
                                    '"inferred": true or "manual": true')
            return
        if "cc" in d:
            cc = d["cc"]
            if not is_int(cc) or not 0 <= cc <= 127:
                self.error("E017", f"{ptr}/cc", '"cc" must be a controller number 0-127')
                return
            ranges = d.get("ranges")
            if not isinstance(ranges, dict):
                self.error("E017", f"{ptr}/ranges", '"ranges" must map each state to [low, high], e.g. {"push": [0, 63]}')
                return
            for value, r in ranges.items():
                ok = (value in seen and isinstance(r, list) and len(r) == 2 and all(is_int(x) for x in r)
                      and r[0] >= 0 and r[1] <= 127 and r[0] <= r[1])
                if not ok:
                    self.error("E017", f"{ptr}/ranges/{value}", "each range must be a known state and [low, high] within 0-127")
                    return
        elif "channel" in d:
            channels = d["channel"]
            if not isinstance(channels, dict):
                self.error("E017", f"{ptr}/channel", '"channel" must map MIDI channels to states, e.g. {"1": "push", "2": "pull"}')
                return
            for ch, value in channels.items():
                if not (ch.isdigit() and str(int(ch)) == ch and 1 <= int(ch) <= 16 and value in seen):
                    self.error("E017", f"{ptr}/channel/{ch}", "keys must be channels 1-16 and values known states")
                    return
        default = self.opt_string(d, "default", ptr)
        if default is not None and default not in seen:
            self.error("E017", f"{ptr}/default", '"default" must be one of the state names')
            return
        self.state_values = seen
        self.default_state = seen.index(default) if default is not None else 0

    def state_notes(self, v, pointer):
        """Plain list or {state: list}; returns one list per state, or None on error."""
        def check_list(arr, ptr):
            if not isinstance(arr, list) or not arr:
                self.error("E019", ptr, "a row needs a non-empty list of notes")
                return None
            pitches = []
            for i, n in enumerate(arr):
                if n is None:
                    pitches.append(None)
                    continue
                if isinstance(n, list):
                    self.error("E012", f"{ptr}/{i}", "chord buttons need a newer KeyFall version")
                    return None
                p = self.note(n, f"{ptr}/{i}")
                if p is None:
                    return None
                pitches.append(p)
            return pitches
        if not isinstance(v, dict):
            single = check_list(v, pointer)
            return None if single is None else [single]
        if self.state_values is None:
            self.error("E016", pointer, 'notes keyed by state need a "states" declaration at the top level')
            return None
        per_state = [None] * len(self.state_values)
        for value, arr in v.items():
            if value not in self.state_values:
                self.error("E016", f"{pointer}/{value}", f'unknown state "{value}"')
                return None
            notes = check_list(arr, f"{pointer}/{value}")
            if notes is None:
                return None
            per_state[self.state_values.index(value)] = notes
        for s_index, notes in enumerate(per_state):
            if notes is None:
                self.error("E016", pointer, f'notes for state "{self.state_values[s_index]}" are missing')
                return None
            if len(notes) != len(per_state[0]):
                self.error("E019", pointer, "every state must list the same number of buttons")
                return None
        return per_state

    def check_rows_zone(self, zone, index, pointer):
        self.check_keys(zone, {"type", "id", "label", "hand", "input", "position", "rows", "shape"}, pointer)
        shape = self.opt_string(zone, "shape", pointer)
        if shape is not None and shape not in ("rect", "roundRect", "circle"):
            self.error("E014", f"{pointer}/shape", '"shape" must be rect, roundRect or circle')
        rows = zone.get("rows")
        if not isinstance(rows, list) or not rows:
            self.error("E019", f"{pointer}/rows", 'a rows zone needs "rows": [{"notes": [...]}, ...]')
            return
        parsed = []
        for r, row in enumerate(rows):
            row_ptr = f"{pointer}/rows/{r}"
            if not isinstance(row, dict) or "notes" not in row:
                self.error("E019", row_ptr, 'each row must be an object with "notes"')
                return
            self.check_keys(row, {"notes", "offset", "label"}, row_ptr)
            notes = self.state_notes(row["notes"], f"{row_ptr}/notes")
            if notes is None:
                return
            if "offset" in row and (not isinstance(row["offset"], (int, float)) or isinstance(row["offset"], bool)):
                self.error("E019", f"{row_ptr}/offset", '"offset" must be a number of buttons')
                return
            parsed.append(notes)
        self.check_position(zone, pointer)
        if not self.new_zone(zone, index, pointer):
            return
        for r, notes in enumerate(parsed):
            for b in range(len(notes[0])):
                if all(per_state[b] is None for per_state in notes):
                    self.error("E019", f"{pointer}/rows/{r}", f"button {b} has no note in any state")
                    return
                self.controls += 1

    def check_meta(self, root):
        fmt = root.get("format")
        if not is_int(fmt) or fmt != CURRENT_FORMAT:
            self.error("E002", "/format", f'"format" must be {CURRENT_FORMAT} (format 1 files are migrated automatically)')
        layout_id = self.opt_string(root, "id", "")
        if layout_id is None or len(layout_id) > 64 or not ID_RE.match(layout_id):
            self.error("E003", "/id", '"id" is required: lowercase letters, digits and single hyphens, up to 64 chars')
        name = self.opt_string(root, "name", "")
        if not name:
            self.error("E004", "/name", '"name" is required and must not be empty')
        for key in ("author", "license", "description"):
            self.opt_string(root, key, "")
        if "match" in root:
            if not isinstance(root["match"], dict):
                self.error("E014", "/match", '"match" must be an object')
            else:
                self.check_keys(root["match"], {"midiPort"}, "/match")
                self.opt_string(root["match"], "midiPort", "/match")
        if "units" in root and root["units"] != "key":
            self.warning("W003", "/units", 'only "key" units are supported in this version; the value is ignored')

    def new_zone(self, zone, index, pointer):
        zone_id = f"z{index}"
        if "id" in zone:
            s = self.opt_string(zone, "id", pointer)
            if s is not None:
                if not s:
                    self.error("E014", f"{pointer}/id", 'zone "id" must not be empty')
                zone_id = s
        self.opt_string(zone, "label", pointer)
        hand = self.opt_string(zone, "hand", pointer)
        if hand is not None and hand not in ("left", "right", "none"):
            self.error("E014", f"{pointer}/hand", '"hand" must be left, right or none')
        if "input" in zone:
            if not isinstance(zone["input"], dict):
                self.error("E014", f"{pointer}/input", '"input" must be an object')
            else:
                self.check_keys(zone["input"], {"channel"}, f"{pointer}/input")
                if "channel" in zone["input"]:
                    self.channel(zone["input"]["channel"], f"{pointer}/input/channel")
        if zone_id in self.zone_ids:
            self.error("E011", f"{pointer}/id", f'duplicate zone id "{zone_id}"')
            return False
        self.zone_ids.add(zone_id)
        return True

    def check_position(self, zone, pointer):
        if "position" in zone:
            p = zone["position"]
            if not (isinstance(p, list) and len(p) == 2 and all(
                    isinstance(x, (int, float)) and not isinstance(x, bool) for x in p)):
                self.error("E014", f"{pointer}/position", '"position" must be [x, y] in key units')

    def check_piano_shorthand(self, piano):
        if not isinstance(piano, list) or len(piano) != 2:
            self.error("E014", "/piano", '"piano" must be [firstNote, lastNote], e.g. ["C2", "C6"]')
            return
        first, last = self.note(piano[0], "/piano/0"), self.note(piano[1], "/piano/1")
        if first is None or last is None:
            return
        if first > last:
            self.error("E008", "/piano", "first note is higher than last note")
            return
        if self.new_zone({"id": "keys"}, 0, "/piano"):
            self.controls += last - first + 1

    def check_zones(self, zones):
        if not isinstance(zones, list):
            self.error("E014", "/zones", '"zones" must be an array')
            return
        for i, zone in enumerate(zones):
            pointer = f"/zones/{i}"
            if not isinstance(zone, dict):
                self.error("E014", pointer, "each zone must be an object")
                continue
            ztype = self.opt_string(zone, "type", pointer)
            if ztype == "piano":
                self.check_piano_zone(zone, i, pointer)
            elif ztype == "grid":
                self.check_grid_zone(zone, i, pointer)
            elif ztype == "rows":
                self.check_rows_zone(zone, i, pointer)
            elif ztype == "hex":
                self.error("E012", f"{pointer}/type", f'zone type "{ztype}" needs a newer KeyFall version')
            else:
                self.error("E006", f"{pointer}/type", 'zone "type" must be "piano", "grid" or "rows"')

    def check_piano_zone(self, zone, index, pointer):
        self.check_keys(zone, {"type", "id", "label", "hand", "input", "position", "firstNote", "lastNote"}, pointer)
        if "firstNote" not in zone or "lastNote" not in zone:
            self.error("E014", pointer, 'a piano zone needs "firstNote" and "lastNote"')
            return
        first = self.note(zone["firstNote"], f"{pointer}/firstNote")
        last = self.note(zone["lastNote"], f"{pointer}/lastNote")
        if first is None or last is None:
            return
        if first > last:
            self.error("E008", pointer, '"firstNote" is higher than "lastNote"')
            return
        self.check_position(zone, pointer)
        if self.new_zone(zone, index, pointer):
            self.controls += last - first + 1

    def check_grid_zone(self, zone, index, pointer):
        self.check_keys(zone, {"type", "id", "label", "hand", "input", "position", "rows", "cols", "notes",
                               "startNote", "colStep", "rowStep", "rowOrder", "shape"}, pointer)
        rows = self.int_in_range(zone, "rows", 1, MAX_GRID_SIDE, pointer, "E009")
        cols = self.int_in_range(zone, "cols", 1, MAX_GRID_SIDE, pointer, "E009")
        if rows is None or cols is None:
            if "rows" not in zone or "cols" not in zone:
                self.error("E009", pointer, 'a grid zone needs "rows" and "cols" (1-32)')
            return
        shape = self.opt_string(zone, "shape", pointer)
        if shape is not None and shape not in ("rect", "roundRect", "circle"):
            self.error("E014", f"{pointer}/shape", '"shape" must be rect, roundRect or circle')
        order = self.opt_string(zone, "rowOrder", pointer)
        if order is not None and order not in ("bottomUp", "topDown"):
            self.error("E014", f"{pointer}/rowOrder", '"rowOrder" must be bottomUp or topDown')

        if "notes" in zone:
            notes = zone["notes"]
            if not isinstance(notes, list) or len(notes) != rows * cols:
                self.error("E009", f"{pointer}/notes",
                           f'"notes" must list exactly rows x cols = {rows * cols} notes, top row first')
                return
            for i, n in enumerate(notes):
                if isinstance(n, list):
                    self.error("E012", f"{pointer}/notes/{i}", "chord buttons need a newer KeyFall version")
                    return
                if self.note(n, f"{pointer}/notes/{i}") is None:
                    return
        else:
            if "startNote" not in zone:
                self.error("E009", pointer, 'a grid zone needs either "notes" or "startNote"')
                return
            start = self.note(zone["startNote"], f"{pointer}/startNote")
            col_step = self.int_in_range(zone, "colStep", -127, 127, pointer, "E014") if "colStep" in zone else 1
            row_step = self.int_in_range(zone, "rowStep", -127, 127, pointer, "E014") if "rowStep" in zone else cols
            if start is None or col_step is None or row_step is None:
                return
            pitches = [start + c * col_step + r * row_step for r in range(rows) for c in range(cols)]
            bad = [p for p in pitches if not 0 <= p < NUM_PITCHES]
            if bad:
                self.error("E007", pointer, f"the grid formula produces note {bad[0]} outside 0-127")
                return
        self.check_position(zone, pointer)
        if self.new_zone(zone, index, pointer):
            self.controls += rows * cols


def check_file(path):
    """Returns (loaded, diagnostics)."""
    checker = Checker()
    try:
        with open(path, encoding="utf-8-sig") as f:
            root = json.load(f, object_pairs_hook=_no_duplicates)
    except OSError as e:
        return False, [("error", "E000", "", f"cannot read file: {e}")]
    except (json.JSONDecodeError, DuplicateKey) as e:
        where = f"line {e.lineno}, column {e.colno}: {e.msg}" if isinstance(e, json.JSONDecodeError) else str(e)
        return False, [("error", "E001", "", f"JSON syntax error at {where}")]
    checker.check(root)
    return not checker.failed, checker.diagnostics


def print_diagnostics(path, diagnostics):
    for severity, code, pointer, message in diagnostics:
        where = f" at {pointer}" if pointer else ""
        print(f"{path}: {severity} {code}{where}: {message}")


def run_corpus(directory):
    failures = total = 0
    for path in sorted(glob.glob(os.path.join(directory, "valid", "*.json"))):
        total += 1
        loaded, diagnostics = check_file(path)
        if not loaded:
            failures += 1
            print("FAIL (expected to load):")
            print_diagnostics(path, diagnostics)
    for path in sorted(glob.glob(os.path.join(directory, "invalid", "*.json"))):
        total += 1
        expected = os.path.basename(path)[:4]
        loaded, diagnostics = check_file(path)
        if loaded or not any(s == "error" and c == expected for s, c, _, _ in diagnostics):
            failures += 1
            print(f"FAIL (expected {expected}): {path}")
            print_diagnostics(path, diagnostics)
    print(f"{total - failures}/{total} corpus files behave as expected")
    return 0 if failures == 0 else 1


def main(argv):
    if len(argv) == 2 and argv[0] == "--corpus":
        return run_corpus(argv[1])
    strict = "--strict" in argv
    files = [a for a in argv if a != "--strict"]
    if not files:
        print(__doc__)
        return 2
    failures = 0
    for path in files:
        loaded, diagnostics = check_file(path)
        print_diagnostics(path, diagnostics)
        if not loaded or (strict and diagnostics):
            failures += 1
        else:
            print(f"{path}: ok")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
