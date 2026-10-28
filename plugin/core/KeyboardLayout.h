/*
 * KeyboardLayout.h - instrument layout model loaded from *.kflayout.json (no Qt)
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

#ifndef KEYFALL_CORE_KEYBOARD_LAYOUT_H
#define KEYFALL_CORE_KEYBOARD_LAYOUT_H

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Json.h"

//! Layout model, see PLAN.md §3a and docs/KEYBOARD_LAYOUTS.md.
//! A piano is just a generated layout: every key is a control with one pitch,
//! so nothing outside this module may assume "pitch == x position".
namespace kf::layout {

constexpr int NumPitches = 128;
constexpr int CurrentFormat = 2;
constexpr std::size_t MaxControls = 1024;

enum class Shape : std::uint8_t
{
	Rect,
	RoundRect,
	Circle,
	PianoWhite,
	PianoBlack
};

enum class Hand : std::uint8_t
{
	None,
	Left,
	Right
};

//! Geometry in layout units: one white piano key is 1 unit wide
struct Rect
{
	float x = 0, y = 0, w = 0, h = 0;
	float right() const { return x + w; }
	float bottom() const { return y + h; }
	float centerX() const { return x + w / 2; }
};

struct Control
{
	std::string id;
	std::uint16_t zone = 0;
	Shape shape = Shape::Rect;
	Rect rect;
	std::uint8_t pitch = 0; //!< sounding pitch (state-dependent pitches arrive with format features in v2)
};

struct Zone
{
	std::string id;
	std::string label;
	Hand hand = Hand::None;
	int channel = 0; //!< 0 = any, otherwise 1..16
};

struct Meta
{
	int format = CurrentFormat; //!< format of the source file (1 is migrated on load)
	std::string id;
	std::string name;
	std::string author;
	std::string license;
	std::string description;
	std::string matchMidiPort; //!< substring of a MIDI port name, used for auto-selection later
};

struct Diagnostic
{
	enum class Severity : std::uint8_t
	{
		Warning,
		Error
	};
	Severity severity = Severity::Error;
	std::string code;    //!< stable code, e.g. "E007"; documented in docs/KEYBOARD_LAYOUTS.md
	std::string pointer; //!< JSON pointer to the offending value, e.g. "/zones/0/firstNote"
	std::string message;

	bool isError() const { return severity == Severity::Error; }
};

class KeyboardLayout
{
public:
	//! Parses and validates a layout. Returns nothing if any error was found;
	//! warnings and errors are appended to diagnostics either way.
	static std::optional<KeyboardLayout> fromJson(const json::Value& root, std::vector<Diagnostic>& diagnostics);
	static std::optional<KeyboardLayout> fromText(std::string_view text, std::vector<Diagnostic>& diagnostics);

	//! Plain piano keyboard; used for built-in fallbacks and the clip-fitted "Auto" layout
	static KeyboardLayout piano(int firstNote, int lastNote, std::string id, std::string name);

	const Meta& meta() const { return m_meta; }
	const std::vector<Zone>& zones() const { return m_zones; }
	const std::vector<Control>& controls() const { return m_controls; }

	//! Controls that can sound this pitch (many-to-many in general)
	std::span<const std::uint16_t> candidates(int pitch) const;
	bool covers(int pitch) const { return !candidates(pitch).empty(); }

	//! Horizontal lane used by the falling-notes view: the first candidate's rect
	std::optional<Rect> lane(int pitch) const;

	//! Control a received note maps to, honouring zone channel filters (channel is 1..16)
	std::optional<std::uint16_t> controlForInput(int channel, int pitch) const;

	Rect bounds() const { return m_bounds; }
	//! Lowest and highest playable pitch; {0, -1} for an empty layout
	std::pair<int, int> pitchRange() const;

private:
	KeyboardLayout() = default;
	void finalize();

	Meta m_meta;
	std::vector<Zone> m_zones;
	std::vector<Control> m_controls;
	std::array<std::vector<std::uint16_t>, NumPitches> m_byPitch;
	Rect m_bounds;

	friend class Builder;
};

//! "C4" = 60, "C#4"/"Db4" = 61, "C-1" = 0
std::optional<int> parseNoteName(std::string_view name);
//! 60 -> "C4" (sharps)
std::string noteName(int pitch);
bool isBlackKey(int pitch);

//! Converts a format-1 document into format 2 (zone "channel" -> "input.channel",
//! "octaveShift" dropped with a warning). Other documents are returned unchanged.
json::Value migrateV1(const json::Value& root, std::vector<Diagnostic>& diagnostics);

} // namespace kf::layout

#endif // KEYFALL_CORE_KEYBOARD_LAYOUT_H
