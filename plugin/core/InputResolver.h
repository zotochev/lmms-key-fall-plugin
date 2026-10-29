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
#include <map>
#include <optional>
#include <utility>

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
	//! Instrument state (e.g. bellows push/pull index) the note was played in; -1 when unknown/stateless
	int state = -1;
};

class InputResolver
{
public:
	explicit InputResolver(const layout::KeyboardLayout* layout = nullptr) { setLayout(layout); }

	void setLayout(const layout::KeyboardLayout* layout);

	//! Note on/off become presses (note-on with velocity 0 is a release). Controller messages
	//! may change the instrument state (bellows sensor) and are otherwise ignored.
	std::optional<PressEvent> resolve(const MidiInputEvent& event);

	//! Current instrument state index (bellows direction), -1 when unknown or the layout has none.
	//! For layouts that infer the state from the notes, the state of the last note played.
	int state() const { return m_state; }
	//! For layouts whose state is chosen by hand in the window
	void setState(int state) { m_state = state; }

private:
	const layout::KeyboardLayout* m_layout = nullptr;
	int m_state = -1;
	//! (channel, pitch) -> control and state it went down on, so the release lights off the
	//! same button even if the bellows changed direction in between
	std::map<std::pair<int, int>, std::pair<std::optional<std::uint16_t>, int>> m_held;
};

} // namespace kf

#endif // KEYFALL_CORE_INPUT_RESOLVER_H
