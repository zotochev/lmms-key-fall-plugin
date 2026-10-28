/*
 * KeyboardLayout.cpp - instrument layout model loaded from *.kflayout.json (no Qt)
 *
 * Copyright (c) 2026 Mikhail Zotochev <zotochev@gmail.com>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "KeyboardLayout.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace kf::layout {

namespace {

// Piano geometry in white-key units (see PLAN.md, UI/UX: black keys 0.58 W x 0.63 length)
constexpr float WhiteKeyLength = 5.0f;
constexpr float BlackKeyWidth = 0.58f;
constexpr float BlackKeyLength = WhiteKeyLength * 0.63f;
// Real pianos shift black keys off the white-key boundary: C#/D# apart, F#/A# fan out
constexpr std::array<float, 12> BlackKeyOffset = {0, -0.08f, 0, 0.08f, 0, 0, -0.1f, 0, 0, 0, 0.1f, 0};

constexpr float GridCellSize = 1.0f;
constexpr float GridCellPitch = 1.15f;
constexpr float ZoneGap = 0.5f;
constexpr int MaxGridSide = 32;

bool isInteger(const json::Value& v)
{
	return v.isNumber() && std::floor(v.asNumber()) == v.asNumber()
		&& std::abs(v.asNumber()) < static_cast<double>(std::numeric_limits<int>::max());
}

bool isValidId(const std::string& id)
{
	if (id.empty() || id.size() > 64 || id.front() == '-' || id.back() == '-') { return false; }
	char prev = 0;
	for (char c : id)
	{
		const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
		if (!ok || (c == '-' && prev == '-')) { return false; }
		prev = c;
	}
	return true;
}

std::string at(const std::string& pointer, const std::string& key) { return pointer + "/" + key; }
std::string at(const std::string& pointer, std::size_t index) { return pointer + "/" + std::to_string(index); }

} // namespace

std::optional<int> parseNoteName(std::string_view name)
{
	if (name.empty()) { return std::nullopt; }
	static constexpr std::array<int, 7> LetterPitch = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
	const char letter = name[0];
	if (letter < 'A' || letter > 'G') { return std::nullopt; }
	int pc = LetterPitch[letter - 'A'];
	std::size_t i = 1;
	if (i < name.size() && name[i] == '#') { ++pc; ++i; }
	else if (i < name.size() && name[i] == 'b') { --pc; ++i; }

	bool negative = false;
	if (i < name.size() && name[i] == '-') { negative = true; ++i; }
	if (i >= name.size() || i + 2 < name.size()) { return std::nullopt; }
	int octave = 0;
	for (; i < name.size(); ++i)
	{
		if (name[i] < '0' || name[i] > '9') { return std::nullopt; }
		octave = octave * 10 + (name[i] - '0');
	}
	if (negative) { octave = -octave; }
	const int pitch = (octave + 1) * 12 + pc;
	if (pitch < 0 || pitch >= NumPitches) { return std::nullopt; }
	return pitch;
}

std::string noteName(int pitch)
{
	static constexpr std::array<const char*, 12> Names = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	return std::string(Names[((pitch % 12) + 12) % 12]) + std::to_string(pitch / 12 - 1);
}

bool isBlackKey(int pitch)
{
	switch (((pitch % 12) + 12) % 12)
	{
		case 1: case 3: case 6: case 8: case 10: return true;
		default: return false;
	}
}

//! Turns a validated-as-we-go JSON document into a KeyboardLayout
class Builder
{
public:
	explicit Builder(std::vector<Diagnostic>& diagnostics)
		: m_diagnostics(diagnostics)
	{
	}

	std::optional<KeyboardLayout> build(const json::Value& root)
	{
		if (!root.isObject())
		{
			error("E014", "", std::string("layout must be a JSON object, got ") + root.typeName());
			return std::nullopt;
		}
		checkKeys(root, {"$schema", "format", "id", "name", "author", "license", "description", "match", "units",
			"piano", "zones", "requires", "states"}, "");

		parseMeta(root);
		parseRequires(root);

		const auto* piano = root.find("piano");
		const auto* zones = root.find("zones");
		if (piano && zones) { error("E015", "", "use either \"piano\" or \"zones\", not both"); }
		else if (piano) { parsePianoShorthand(*piano); }
		else if (zones) { parseZones(*zones); }

		if (!m_failed && m_layout.m_controls.empty())
		{
			error("E005", "", "the layout has no controls: add \"piano\" or a non-empty \"zones\" list");
		}
		if (m_layout.m_controls.size() > MaxControls)
		{
			error("E013", "", "too many controls (" + std::to_string(m_layout.m_controls.size()) + " > "
				+ std::to_string(MaxControls) + ")");
		}
		if (m_failed) { return std::nullopt; }
		m_layout.finalize();
		return std::move(m_layout);
	}

	//! Appends a piano zone; used both by the parser and by KeyboardLayout::piano()
	void addPiano(int firstNote, int lastNote, std::uint16_t zone, float originX, float originY)
	{
		float whiteX = originX; // left edge of the next white key
		for (int p = firstNote; p <= lastNote; ++p)
		{
			Control c;
			c.id = m_layout.m_zones[zone].id + "." + noteName(p);
			c.zone = zone;
			c.pitch = static_cast<std::uint8_t>(p);
			if (isBlackKey(p))
			{
				c.shape = Shape::PianoBlack;
				c.rect = {whiteX - BlackKeyWidth / 2 + BlackKeyOffset[p % 12], originY, BlackKeyWidth, BlackKeyLength};
			}
			else
			{
				c.shape = Shape::PianoWhite;
				c.rect = {whiteX, originY, 1.0f, WhiteKeyLength};
				whiteX += 1.0f;
			}
			m_layout.m_controls.push_back(std::move(c));
		}
	}

	KeyboardLayout& layout() { return m_layout; }

private:
	void report(Diagnostic::Severity severity, std::string code, std::string pointer, std::string message)
	{
		m_diagnostics.push_back({severity, std::move(code), std::move(pointer), std::move(message)});
	}
	void error(std::string code, std::string pointer, std::string message)
	{
		m_failed = true;
		report(Diagnostic::Severity::Error, std::move(code), std::move(pointer), std::move(message));
	}
	void warning(std::string code, std::string pointer, std::string message)
	{
		report(Diagnostic::Severity::Warning, std::move(code), std::move(pointer), std::move(message));
	}

	void checkKeys(const json::Value& obj, std::initializer_list<std::string_view> allowed, const std::string& pointer)
	{
		for (const auto& [key, value] : obj.asObject())
		{
			if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
			{
				warning("W001", at(pointer, key), "unknown key \"" + key + "\" is ignored");
			}
		}
	}

	std::optional<std::string> optionalString(const json::Value& obj, std::string_view key, const std::string& pointer)
	{
		const auto* v = obj.find(key);
		if (!v) { return std::nullopt; }
		if (!v->isString())
		{
			error("E014", at(pointer, std::string(key)), "\"" + std::string(key) + "\" must be a string");
			return std::nullopt;
		}
		return v->asString();
	}

	std::optional<int> note(const json::Value& v, const std::string& pointer)
	{
		if (isInteger(v))
		{
			const int n = static_cast<int>(v.asNumber());
			if (n >= 0 && n < NumPitches) { return n; }
		}
		else if (v.isString())
		{
			if (auto n = parseNoteName(v.asString())) { return n; }
		}
		error("E007", pointer, "invalid note: use a MIDI number 0-127 or a name like \"C4\", \"F#3\", \"Bb2\"");
		return std::nullopt;
	}

	std::optional<int> intInRange(const json::Value& obj, std::string_view key, int min, int max,
		const std::string& pointer, const char* code)
	{
		const auto* v = obj.find(key);
		if (!v) { return std::nullopt; }
		if (!isInteger(*v) || v->asNumber() < min || v->asNumber() > max)
		{
			error(code, at(pointer, std::string(key)), "\"" + std::string(key) + "\" must be an integer from "
				+ std::to_string(min) + " to " + std::to_string(max));
			return std::nullopt;
		}
		return static_cast<int>(v->asNumber());
	}

	void parseMeta(const json::Value& root)
	{
		auto& meta = m_layout.m_meta;
		const auto* format = root.find("format");
		if (!format || !isInteger(*format) || format->asNumber() != CurrentFormat)
		{
			error("E002", "/format", "\"format\" must be " + std::to_string(CurrentFormat)
				+ " (format 1 files are migrated automatically)");
		}

		const auto id = optionalString(root, "id", "");
		if (!id || !isValidId(*id))
		{
			error("E003", "/id", "\"id\" is required: lowercase letters, digits and single hyphens, up to 64 chars");
		}
		else { meta.id = *id; }

		const auto name = optionalString(root, "name", "");
		if (!name || name->empty()) { error("E004", "/name", "\"name\" is required and must not be empty"); }
		else { meta.name = *name; }

		meta.author = optionalString(root, "author", "").value_or("");
		meta.license = optionalString(root, "license", "").value_or("");
		meta.description = optionalString(root, "description", "").value_or("");

		if (const auto* match = root.find("match"))
		{
			if (!match->isObject()) { error("E014", "/match", "\"match\" must be an object"); }
			else
			{
				checkKeys(*match, {"midiPort"}, "/match");
				meta.matchMidiPort = optionalString(*match, "midiPort", "/match").value_or("");
			}
		}

		if (const auto* units = root.find("units"))
		{
			if (!units->isString() || units->asString() != "key")
			{
				warning("W003", "/units", "only \"key\" units are supported in this version; the value is ignored");
			}
		}
	}

	void parseRequires(const json::Value& root)
	{
		if (root.find("states"))
		{
			error("E012", "/states", "instrument states (e.g. bellows push/pull) need a newer KeyFall version");
		}
		const auto* required = root.find("requires");
		if (!required) { return; }
		if (!required->isArray()) { error("E014", "/requires", "\"requires\" must be an array of feature names"); return; }
		// No optional features exist yet: anything requested is newer than this build
		for (std::size_t i = 0; i < required->asArray().size(); ++i)
		{
			const auto& feature = required->asArray()[i];
			error("E012", at("/requires", i), "feature \"" + std::string(feature.isString() ? feature.asString() : "?")
				+ "\" needs a newer KeyFall version");
		}
	}

	std::optional<std::uint16_t> newZone(const json::Value* zoneObj, std::size_t index, const std::string& pointer)
	{
		Zone zone;
		zone.id = "z" + std::to_string(index);
		if (zoneObj)
		{
			if (const auto id = optionalString(*zoneObj, "id", pointer))
			{
				if (id->empty()) { error("E014", at(pointer, "id"), "zone \"id\" must not be empty"); }
				zone.id = *id;
			}
			zone.label = optionalString(*zoneObj, "label", pointer).value_or("");
			if (const auto hand = optionalString(*zoneObj, "hand", pointer))
			{
				if (*hand == "left") { zone.hand = Hand::Left; }
				else if (*hand == "right") { zone.hand = Hand::Right; }
				else if (*hand != "none") { error("E014", at(pointer, "hand"), "\"hand\" must be left, right or none"); }
			}
			if (const auto* input = zoneObj->find("input"))
			{
				if (!input->isObject()) { error("E014", at(pointer, "input"), "\"input\" must be an object"); }
				else
				{
					checkKeys(*input, {"channel"}, at(pointer, "input"));
					if (const auto* ch = input->find("channel")) { zone.channel = channel(*ch, at(pointer, "input/channel")); }
				}
			}
		}
		for (const auto& existing : m_layout.m_zones)
		{
			if (existing.id == zone.id)
			{
				error("E011", at(pointer, "id"), "duplicate zone id \"" + zone.id + "\"");
				return std::nullopt;
			}
		}
		m_layout.m_zones.push_back(std::move(zone));
		return static_cast<std::uint16_t>(m_layout.m_zones.size() - 1);
	}

	int channel(const json::Value& v, const std::string& pointer)
	{
		if (v.isString() && v.asString() == "any") { return 0; }
		if (isInteger(v) && v.asNumber() >= 1 && v.asNumber() <= 16) { return static_cast<int>(v.asNumber()); }
		error("E010", pointer, "channel must be \"any\" or a number from 1 to 16");
		return 0;
	}

	//! Where the next zone starts when it has no explicit "position"
	std::pair<float, float> zoneOrigin(const json::Value* zoneObj, const std::string& pointer)
	{
		if (zoneObj)
		{
			if (const auto* pos = zoneObj->find("position"))
			{
				if (pos->isArray() && pos->asArray().size() == 2 && pos->asArray()[0].isNumber()
					&& pos->asArray()[1].isNumber())
				{
					return {static_cast<float>(pos->asArray()[0].asNumber()),
						static_cast<float>(pos->asArray()[1].asNumber())};
				}
				error("E014", at(pointer, "position"), "\"position\" must be [x, y] in key units");
			}
		}
		if (m_layout.m_controls.empty()) { return {0.0f, 0.0f}; }
		float right = std::numeric_limits<float>::lowest();
		for (const auto& c : m_layout.m_controls) { right = std::max(right, c.rect.right()); }
		return {right + ZoneGap, 0.0f};
	}

	void parsePianoShorthand(const json::Value& piano)
	{
		if (!piano.isArray() || piano.asArray().size() != 2)
		{
			error("E014", "/piano", "\"piano\" must be [firstNote, lastNote], e.g. [\"C2\", \"C6\"]");
			return;
		}
		const auto first = note(piano.asArray()[0], "/piano/0");
		const auto last = note(piano.asArray()[1], "/piano/1");
		if (!first || !last) { return; }
		if (*first > *last) { error("E008", "/piano", "first note is higher than last note"); return; }
		json::Object zoneObj;
		zoneObj.emplace("id", json::Value(std::string("keys")));
		const json::Value zone(std::move(zoneObj));
		if (const auto z = newZone(&zone, 0, "/piano")) { addPiano(*first, *last, *z, 0.0f, 0.0f); }
	}

	void parseZones(const json::Value& zones)
	{
		if (!zones.isArray()) { error("E014", "/zones", "\"zones\" must be an array"); return; }
		for (std::size_t i = 0; i < zones.asArray().size(); ++i)
		{
			const auto& zone = zones.asArray()[i];
			const auto pointer = at("/zones", i);
			if (!zone.isObject()) { error("E014", pointer, "each zone must be an object"); continue; }
			const auto type = optionalString(zone, "type", pointer);
			if (type == "piano") { parsePianoZone(zone, i, pointer); }
			else if (type == "grid") { parseGridZone(zone, i, pointer); }
			else if (type == "hex" || type == "rows")
			{
				error("E012", at(pointer, "type"), "zone type \"" + *type + "\" needs a newer KeyFall version");
			}
			else { error("E006", at(pointer, "type"), "zone \"type\" must be \"piano\" or \"grid\""); }
		}
	}

	void parsePianoZone(const json::Value& zone, std::size_t index, const std::string& pointer)
	{
		checkKeys(zone, {"type", "id", "label", "hand", "input", "position", "firstNote", "lastNote"}, pointer);
		const auto* firstV = zone.find("firstNote");
		const auto* lastV = zone.find("lastNote");
		if (!firstV || !lastV)
		{
			error("E014", pointer, "a piano zone needs \"firstNote\" and \"lastNote\"");
			return;
		}
		const auto first = note(*firstV, at(pointer, "firstNote"));
		const auto last = note(*lastV, at(pointer, "lastNote"));
		if (!first || !last) { return; }
		if (*first > *last) { error("E008", pointer, "\"firstNote\" is higher than \"lastNote\""); return; }
		const auto [ox, oy] = zoneOrigin(&zone, pointer);
		if (const auto z = newZone(&zone, index, pointer)) { addPiano(*first, *last, *z, ox, oy); }
	}

	void parseGridZone(const json::Value& zone, std::size_t index, const std::string& pointer)
	{
		checkKeys(zone, {"type", "id", "label", "hand", "input", "position", "rows", "cols", "notes", "startNote",
			"colStep", "rowStep", "rowOrder", "shape"}, pointer);
		const auto rows = intInRange(zone, "rows", 1, MaxGridSide, pointer, "E009");
		const auto cols = intInRange(zone, "cols", 1, MaxGridSide, pointer, "E009");
		if (!rows || !cols)
		{
			if (!zone.find("rows") || !zone.find("cols"))
			{
				error("E009", pointer, "a grid zone needs \"rows\" and \"cols\" (1-32)");
			}
			return;
		}

		Shape shape = Shape::RoundRect;
		if (const auto s = optionalString(zone, "shape", pointer))
		{
			if (*s == "rect") { shape = Shape::Rect; }
			else if (*s == "circle") { shape = Shape::Circle; }
			else if (*s != "roundRect") { error("E014", at(pointer, "shape"), "\"shape\" must be rect, roundRect or circle"); }
		}

		bool bottomUp = true;
		if (const auto order = optionalString(zone, "rowOrder", pointer))
		{
			if (*order == "topDown") { bottomUp = false; }
			else if (*order != "bottomUp") { error("E014", at(pointer, "rowOrder"), "\"rowOrder\" must be bottomUp or topDown"); }
		}

		// Pitches in visual order: top row first, left to right
		std::vector<int> pitches;
		const auto* notes = zone.find("notes");
		if (notes)
		{
			if (!notes->isArray() || notes->asArray().size() != static_cast<std::size_t>(*rows * *cols))
			{
				error("E009", at(pointer, "notes"), "\"notes\" must list exactly rows x cols = "
					+ std::to_string(*rows * *cols) + " notes, top row first");
				return;
			}
			for (std::size_t i = 0; i < notes->asArray().size(); ++i)
			{
				const auto& n = notes->asArray()[i];
				if (n.isArray())
				{
					error("E012", at(at(pointer, "notes"), i), "chord buttons need a newer KeyFall version");
					return;
				}
				const auto p = note(n, at(at(pointer, "notes"), i));
				if (!p) { return; }
				pitches.push_back(*p);
			}
		}
		else
		{
			const auto* startV = zone.find("startNote");
			if (!startV)
			{
				error("E009", pointer, "a grid zone needs either \"notes\" or \"startNote\"");
				return;
			}
			const auto start = note(*startV, at(pointer, "startNote"));
			const auto colStep = zone.find("colStep") ? intInRange(zone, "colStep", -127, 127, pointer, "E014") : 1;
			const auto rowStep = zone.find("rowStep") ? intInRange(zone, "rowStep", -127, 127, pointer, "E014") : *cols;
			if (!start || !colStep || !rowStep) { return; }
			for (int rv = 0; rv < *rows; ++rv)
			{
				const int r = bottomUp ? *rows - 1 - rv : rv;
				for (int c = 0; c < *cols; ++c)
				{
					const int p = *start + c * *colStep + r * *rowStep;
					if (p < 0 || p >= NumPitches)
					{
						error("E007", pointer, "the grid formula produces note " + std::to_string(p)
							+ " outside 0-127 (row " + std::to_string(rv) + ", column " + std::to_string(c) + ")");
						return;
					}
					pitches.push_back(p);
				}
			}
		}

		const auto [ox, oy] = zoneOrigin(&zone, pointer);
		const auto z = newZone(&zone, index, pointer);
		if (!z) { return; }
		for (int rv = 0; rv < *rows; ++rv)
		{
			for (int c = 0; c < *cols; ++c)
			{
				Control ctl;
				ctl.id = m_layout.m_zones[*z].id + ".r" + std::to_string(rv) + "c" + std::to_string(c);
				ctl.zone = *z;
				ctl.shape = shape;
				ctl.pitch = static_cast<std::uint8_t>(pitches[rv * *cols + c]);
				ctl.rect = {ox + c * GridCellPitch, oy + rv * GridCellPitch, GridCellSize, GridCellSize};
				m_layout.m_controls.push_back(std::move(ctl));
			}
		}
	}

	std::vector<Diagnostic>& m_diagnostics;
	KeyboardLayout m_layout;
	bool m_failed = false;
};

std::optional<KeyboardLayout> KeyboardLayout::fromJson(const json::Value& root, std::vector<Diagnostic>& diagnostics)
{
	const json::Value migrated = migrateV1(root, diagnostics);
	return Builder(diagnostics).build(migrated);
}

std::optional<KeyboardLayout> KeyboardLayout::fromText(std::string_view text, std::vector<Diagnostic>& diagnostics)
{
	json::Value root;
	json::ParseError err;
	if (!json::parse(text, root, err))
	{
		diagnostics.push_back({Diagnostic::Severity::Error, "E001", "",
			"JSON syntax error at line " + std::to_string(err.line) + ", column " + std::to_string(err.column) + ": "
				+ err.message});
		return std::nullopt;
	}
	return fromJson(root, diagnostics);
}

KeyboardLayout KeyboardLayout::piano(int firstNote, int lastNote, std::string id, std::string name)
{
	std::vector<Diagnostic> unused;
	Builder builder(unused);
	auto& layout = builder.layout();
	layout.m_meta.id = std::move(id);
	layout.m_meta.name = std::move(name);
	layout.m_zones.push_back({"keys", "", Hand::None, 0});
	builder.addPiano(std::clamp(firstNote, 0, NumPitches - 1), std::clamp(lastNote, 0, NumPitches - 1), 0, 0, 0);
	layout.finalize();
	return std::move(layout);
}

void KeyboardLayout::finalize()
{
	for (auto& list : m_byPitch) { list.clear(); }
	for (std::size_t i = 0; i < m_controls.size(); ++i)
	{
		m_byPitch[m_controls[i].pitch].push_back(static_cast<std::uint16_t>(i));
	}
	if (m_controls.empty()) { m_bounds = {}; return; }
	float left = std::numeric_limits<float>::max(), top = left;
	float right = std::numeric_limits<float>::lowest(), bottom = right;
	for (const auto& c : m_controls)
	{
		left = std::min(left, c.rect.x);
		top = std::min(top, c.rect.y);
		right = std::max(right, c.rect.right());
		bottom = std::max(bottom, c.rect.bottom());
	}
	m_bounds = {left, top, right - left, bottom - top};
}

std::span<const std::uint16_t> KeyboardLayout::candidates(int pitch) const
{
	if (pitch < 0 || pitch >= NumPitches) { return {}; }
	return m_byPitch[pitch];
}

std::optional<Rect> KeyboardLayout::lane(int pitch) const
{
	const auto c = candidates(pitch);
	if (c.empty()) { return std::nullopt; }
	return m_controls[c.front()].rect;
}

std::optional<std::uint16_t> KeyboardLayout::controlForInput(int channel, int pitch) const
{
	for (const auto idx : candidates(pitch))
	{
		const int zoneChannel = m_zones[m_controls[idx].zone].channel;
		if (zoneChannel == 0 || zoneChannel == channel) { return idx; }
	}
	return std::nullopt;
}

std::pair<int, int> KeyboardLayout::pitchRange() const
{
	int lo = NumPitches, hi = -1;
	for (int p = 0; p < NumPitches; ++p)
	{
		if (!m_byPitch[p].empty()) { lo = std::min(lo, p); hi = std::max(hi, p); }
	}
	return hi < 0 ? std::pair{0, -1} : std::pair{lo, hi};
}

json::Value migrateV1(const json::Value& root, std::vector<Diagnostic>& diagnostics)
{
	const auto* format = root.find("format");
	if (!format || !format->isNumber() || format->asNumber() != 1) { return root; }

	json::Object out = root.asObject();
	out["format"] = json::Value(static_cast<double>(CurrentFormat));
	if (out.erase("octaveShift"))
	{
		diagnostics.push_back({Diagnostic::Severity::Warning, "W002", "/octaveShift",
			"format 1 \"octaveShift\" is not supported yet and is ignored"});
	}
	if (const auto* zones = root.find("zones"); zones && zones->isArray())
	{
		json::Array newZones;
		for (const auto& zone : zones->asArray())
		{
			if (!zone.isObject() || !zone.find("channel")) { newZones.push_back(zone); continue; }
			json::Object z = zone.asObject();
			json::Object input;
			input.emplace("channel", z.at("channel"));
			z.erase("channel");
			z["input"] = json::Value(std::move(input));
			newZones.push_back(json::Value(std::move(z)));
		}
		out["zones"] = json::Value(std::move(newZones));
	}
	return json::Value(std::move(out));
}

} // namespace kf::layout
