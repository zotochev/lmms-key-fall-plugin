# Instrument layouts for KeyFall

KeyFall draws your instrument from a layout file, not from settings hard-coded in C++. If your MIDI keyboard or controller isn't in the list, you can describe it yourself in a small JSON file and, if you like, send it to the project.

The bigger plan (concertinas, accordions, hex grids) is in [PLAN.md §3a](../PLAN.md). Format version 2 currently supports **keyboards** (`piano`) and **pads/grids** (`grid`).

## Quick start

1. In KeyFall, open **⚙ → Open layouts folder**. This is `<LMMS working folder>/keyfall/layouts/`, for example `Documents/lmms/keyfall/layouts/`.
2. Create a file named `<id>.kflayout.json` there:
   ```json
   {
     "format": 2,
     "id": "my-keystation-49",
     "name": "M-Audio Keystation 49",
     "author": "Your Name",
     "piano": ["C2", "C6"]
   }
   ```
3. **⚙ → Reload layouts**, then pick it in the layout list in the toolbar.
4. Before sending it to the project, check it:
   ```bash
   python scripts/validate_layouts.py my-keystation-49.kflayout.json
   ```

For completion and hints in your editor (VS Code, JetBrains), add `"$schema"` with the path to [`keyboard-layout-2.schema.json`](keyboard-layout-2.schema.json) in your copy of the repository.

If a file has errors, KeyFall doesn't crash. The layout shows up greyed out in the list, and hovering over it shows what's wrong.

## How to find your notes and channels

Press keys on your instrument with the KeyFall window open. The toolbar shows the **last MIDI message**, for example `C2 vel 87  ch 1` or `CC 64 = 127  ch 1`.
- Press the leftmost and rightmost keys: that gives `firstNote` and `lastNote`.
- Press the pads in order, row by row from the top: that gives the `notes` list and the channel.
- `(outside layout)` next to a message means the current layout has no such note on that channel.

Notes can be written as numbers (`60`) or names. `C4` = 60 (middle C, the LMMS convention), `C#4` and `Db4` = 61, `C-1` = 0, `G9` = 127.

## Format

### Top level

| Field | Required | Description |
|---|---|---|
| `format` | yes | `2`. Files with `1` are migrated automatically |
| `id` | yes | Lowercase letters, digits and single hyphens, up to 64 characters. It is also the file name. A user layout with the `id` of a built-in one **replaces** it |
| `name` | yes | Name shown in the list |
| `author`, `license`, `description` | no | Built-in layouts use the `CC0-1.0` license |
| `match.midiPort` | no | Substring of the MIDI port name, for automatic selection (planned) |
| `piano` | one of the two | Shorthand for a plain keyboard: `["C2", "C6"]` |
| `zones` | one of the two | List of the instrument's physical zones, placed left to right |

Unknown fields produce a W001 warning and are ignored. This lets new versions add fields without breaking older ones.

### Zones: common fields

| Field | Description |
|---|---|
| `type` | `"piano"` or `"grid"`. `hex` and `rows` (concertinas, accordions) come in later versions |
| `id` | Unique within the file. Default: `z0`, `z1`, … |
| `label` | Zone caption |
| `hand` | `"left"`, `"right"`, `"none"`: which hand plays the zone |
| `input.channel` | `"any"` (default) or 1–16. A zone only accepts notes on this channel; this is how pads on channel 10 are kept apart from the keys |
| `position` | `[x, y]` of the top-left corner, in "white key" units. By default each zone goes to the right of the previous one |

### `piano` zone

```json
{ "type": "piano", "id": "keys", "firstNote": "C2", "lastNote": "C6" }
```

### `grid` zone

Either list the notes explicitly, **top row first, left to right**:

```json
{ "type": "grid", "id": "pads", "rows": 2, "cols": 8, "input": { "channel": 10 },
  "notes": [40, 41, 42, 43, 48, 49, 50, 51,
            36, 37, 38, 39, 44, 45, 46, 47] }
```

Or use a formula, `note = startNote + col × colStep + row × rowStep`. Here `row` is counted from the bottom (`"rowOrder": "bottomUp"`, the default) or from the top (`"topDown"`):

```json
{ "type": "grid", "rows": 8, "cols": 8, "startNote": 11, "colStep": 1, "rowStep": 10 }
```

Fourths-based isomorphic layouts (Linnstrument-like) work the same way: `"rowStep": 5`. The same note may sit on several pads; KeyFall treats them all as correct.

`shape` is `"roundRect"` (the default), `"rect"` or `"circle"`.

### Example: keys plus pads

```json
{
  "format": 2,
  "id": "launchkey-49-mk3",
  "name": "Novation Launchkey 49 MK3",
  "author": "Your Name",
  "match": { "midiPort": "Launchkey 49" },
  "zones": [
    { "id": "keys", "type": "piano", "firstNote": "C2", "lastNote": "C6" },
    { "id": "pads", "type": "grid", "rows": 2, "cols": 8, "input": { "channel": 10 },
      "notes": [40, 41, 42, 43, 48, 49, 50, 51, 36, 37, 38, 39, 44, 45, 46, 47] }
  ]
}
```
(The pad notes here are illustrative: check your own with the "last message" readout.)

## Error and warning codes

Both checkers report the same codes: the plugin and `kflayout-lint` (C++), and `scripts/validate_layouts.py` (Python). Each diagnostic also gives the place in the file as a JSON pointer, for example `/zones/1/notes/3`.

| Code | Meaning |
|---|---|
| E000 | The file can't be read |
| E001 | JSON syntax error (with line and column) |
| E002 | `format` is missing or isn't 2 (1 is migrated automatically) |
| E003 | `id` is missing or invalid |
| E004 | `name` is missing or empty |
| E005 | The layout has no controls |
| E006 | Unknown zone `type` |
| E007 | Invalid note (not 0–127 and not a name like `C#4`), or a grid formula produces a note outside 0–127 |
| E008 | First note is higher than the last one |
| E009 | Grid: `rows`/`cols` outside 1–32, `notes` doesn't have rows×cols entries, or neither `notes` nor `startNote` is given |
| E010 | Channel is not `"any"` and not 1–16 |
| E011 | Duplicate zone `id` |
| E012 | The feature needs a newer KeyFall version (`states`, `requires`, `hex`/`rows` zones, chord buttons) |
| E013 | More than 1024 controls |
| E014 | Wrong value type or a value that isn't allowed |
| E015 | Both `piano` and `zones` are given |
| W001 | Unknown field, ignored |
| W002 | `octaveShift` from format 1 is not supported yet, ignored |
| W003 | `units` other than `"key"`, ignored |

## Sending your layout to the project

1. The file goes in `plugin/layouts/<id>.kflayout.json`, and the `id` matches the file name.
2. `python scripts/validate_layouts.py --strict plugin/layouts/<id>.kflayout.json` passes with no errors or warnings.
3. `author` is filled in, and `license` is set to `CC0-1.0` so others can freely reuse the layout.
4. You have checked the layout on the real device: every key and pad lights up in the right place.
5. The PR description names the device model and, if possible, includes a photo or a link to its specifications.

For developers: a rule change goes in **both** checkers (`plugin/core/KeyboardLayout.cpp` and `scripts/validate_layouts.py`), plus a file in `tests/layouts/valid` or `tests/layouts/invalid/<CODE>-*.json`. `scripts\lmms.cmd test` runs the whole test set through both checkers.
