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
        if "states" in root:
            self.error("E012", "/states", "instrument states (e.g. bellows push/pull) need a newer KeyFall version")
        if "requires" in root:
            if not isinstance(root["requires"], list):
                self.error("E014", "/requires", '"requires" must be an array of feature names')
            else:
                for i, feature in enumerate(root["requires"]):
                    self.error("E012", f"/requires/{i}", f'feature "{feature}" needs a newer KeyFall version')

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
            elif ztype in ("hex", "rows"):
                self.error("E012", f"{pointer}/type", f'zone type "{ztype}" needs a newer KeyFall version')
            else:
                self.error("E006", f"{pointer}/type", 'zone "type" must be "piano" or "grid"')

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
