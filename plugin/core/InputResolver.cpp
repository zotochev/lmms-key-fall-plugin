/*
 * InputResolver.cpp - turns raw MIDI input into presses on layout controls (no Qt)
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

#include "InputResolver.h"

#include "KeyboardLayout.h"

namespace kf {

namespace {
constexpr std::uint8_t NoteOff = 0x80;
constexpr std::uint8_t NoteOn = 0x90;
} // namespace

std::optional<PressEvent> InputResolver::resolve(const MidiInputEvent& event) const
{
	if ((event.status != NoteOn && event.status != NoteOff) || event.data1 >= layout::NumPitches)
	{
		return std::nullopt;
	}
	PressEvent press;
	press.timeNs = event.timeNs;
	press.pitch = event.data1;
	press.channel = event.channel;
	press.on = event.status == NoteOn && event.data2 > 0;
	press.velocity = press.on ? event.data2 : 0;
	if (m_layout) { press.control = m_layout->controlForInput(event.channel, event.data1); }
	return press;
}

} // namespace kf
