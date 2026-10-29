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

#include <algorithm>

namespace kf {

namespace {
constexpr std::uint8_t NoteOff = 0x80;
constexpr std::uint8_t NoteOn = 0x90;
constexpr std::uint8_t ControlChange = 0xB0;
} // namespace

void InputResolver::setLayout(const layout::KeyboardLayout* layout)
{
	m_layout = layout;
	m_held.clear();
	m_state = layout && layout->state() ? layout->state()->defaultValue : -1;
}

std::optional<PressEvent> InputResolver::resolve(const MidiInputEvent& event)
{
	const auto* state = m_layout && m_layout->state() ? &*m_layout->state() : nullptr;
	if (event.status == ControlChange)
	{
		if (state && state->source == layout::StateVar::Source::Cc && event.data1 == state->cc)
		{
			for (std::size_t i = 0; i < state->ccRanges.size(); ++i)
			{
				const auto [lo, hi] = state->ccRanges[i];
				if (event.data2 >= lo && event.data2 <= hi) { m_state = static_cast<int>(i); }
			}
		}
		return std::nullopt;
	}
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
	if (!m_layout) { return press; }

	const std::pair<int, int> key(event.channel, event.data1);
	if (!press.on)
	{
		const auto held = m_held.find(key);
		if (held != m_held.end())
		{
			press.control = held->second.first;
			press.state = held->second.second;
			m_held.erase(held);
		}
		else { press.control = m_layout->controlForInput(event.channel, event.data1); }
		return press;
	}

	int known = -1;
	if (state)
	{
		switch (state->source)
		{
			case layout::StateVar::Source::Cc:
			case layout::StateVar::Source::Manual: known = m_state; break;
			case layout::StateVar::Source::Channel:
				known = state->channelValue[std::clamp<int>(event.channel, 0, 16)];
				if (known >= 0) { m_state = known; }
				break;
			case layout::StateVar::Source::Inferred: break; // the pitch tells
		}
	}
	if (const auto r = m_layout->resolve(event.channel, event.data1, known))
	{
		press.control = r->control;
		press.state = r->state;
		if (state && state->source == layout::StateVar::Source::Inferred && r->state >= 0) { m_state = r->state; }
	}
	else { press.state = known; }
	m_held[key] = {press.control, press.state};
	return press;
}

} // namespace kf
