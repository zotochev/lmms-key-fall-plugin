/*
 * InputResolver.h - turns raw MIDI input into presses on layout controls (no Qt)
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

#ifndef KEYFALL_CORE_INPUT_RESOLVER_H
#define KEYFALL_CORE_INPUT_RESOLVER_H

#include <cstdint>
#include <optional>

namespace kf {

namespace layout {
class KeyboardLayout;
}

//! A raw channel message as received from the MIDI driver thread
struct MidiInputEvent
{
	std::int64_t timeNs = 0; //!< steady clock, stamped on arrival (backends don't provide usable timestamps)
	std::uint8_t status = 0; //!< message type without channel: 0x80, 0x90, 0xB0, ...
	std::uint8_t channel = 1; //!< 1..16
	std::uint8_t data1 = 0;
	std::uint8_t data2 = 0;
};

//! A key or button going down or up, already mapped onto the current layout
struct PressEvent
{
	std::int64_t timeNs = 0;
	std::uint8_t pitch = 0;
	std::uint8_t velocity = 0;
	std::uint8_t channel = 1;
	bool on = false;
	//! Control that produced it; empty when the note is outside the layout.
	//! For format-2 piano layouts the control is simply the key of that pitch.
	std::optional<std::uint16_t> control;
};

class InputResolver
{
public:
	explicit InputResolver(const layout::KeyboardLayout* layout = nullptr)
		: m_layout(layout)
	{
	}

	void setLayout(const layout::KeyboardLayout* layout) { m_layout = layout; }

	//! Note on/off become presses (note-on with velocity 0 is a release); other messages are ignored
	std::optional<PressEvent> resolve(const MidiInputEvent& event) const;

private:
	const layout::KeyboardLayout* m_layout;
};

} // namespace kf

#endif // KEYFALL_CORE_INPUT_RESOLVER_H
