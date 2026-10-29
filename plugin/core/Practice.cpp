/*
 * Practice.cpp - KeyFall's own practice transport and wait mode (no Qt)
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

#include "Practice.h"

#include <algorithm>

namespace kf {

// --- PracticeClock --------------------------------------------------------

void PracticeClock::setTempo(double ticksPerSecond, double tempoFactor)
{
	m_ticksPerSecond = std::max(1e-3, ticksPerSecond);
	m_tempoFactor = std::clamp(tempoFactor, 0.1, 2.0);
}

void PracticeClock::start(double fromTick, std::int64_t nowNs)
{
	m_running = true;
	m_holding = false;
	m_position = fromTick;
	m_lastNs = nowNs;
}

bool PracticeClock::update(std::int64_t nowNs, std::optional<double> gateTick)
{
	if (!m_running) { return false; }
	const double elapsed = std::max<std::int64_t>(0, nowNs - m_lastNs) * 1e-9;
	m_lastNs = nowNs;
	double next = m_position + elapsed * ticksPerSecond();

	m_holding = false;
	if (gateTick && next >= *gateTick)
	{
		// Wait mode: time stands at the chord; it moves on smoothly once released
		next = std::max(m_position, *gateTick);
		m_holding = true;
	}
	if (m_loop && m_loop->end > m_loop->begin && next >= m_loop->end)
	{
		m_position = m_loop->begin;
		m_holding = false;
		return true;
	}
	m_position = next;
	return false;
}

// --- WaitGate --------------------------------------------------------------

void WaitGate::setChart(std::shared_ptr<const Chart> chart, double chordTicks)
{
	m_chart = chart ? std::move(chart) : std::make_shared<const Chart>();
	m_chordTicks = chordTicks;
	reset(0);
}

void WaitGate::buildChords(const std::vector<bool>* active)
{
	m_chords.clear();
	const auto& notes = m_chart->notes();
	const bool filtered = active && active->size() == notes.size();
	for (std::size_t i = 0; i < notes.size(); ++i)
	{
		if (filtered && !(*active)[i]) { continue; }
		if (m_chords.empty() || notes[i].start - m_chords.back().first > m_chordTicks)
		{
			m_chords.push_back({notes[i].start, {}});
		}
		m_chords.back().notes.push_back(i);
	}
}

void WaitGate::reset(double fromTick, double untilTick, const std::vector<bool>* active)
{
	buildChords(active);
	// Notes outside the run (other hand, before the start, after the loop) are Skipped
	m_grades.assign(m_chart->notes().size(), Grade::Skipped);
	m_current = static_cast<std::size_t>(std::lower_bound(m_chords.begin(), m_chords.end(), fromTick,
		[](const Chord& c, double t) { return c.first < t; }) - m_chords.begin());
	m_until = untilTick;
	m_pressedKeys.clear();
	m_currentHadWrong = false;
	m_stats = {};
	for (std::size_t c = m_current; c < m_chords.size() && m_chords[c].first < m_until; ++c) { ++m_stats.chords; }
	for (std::size_t c = m_current; c < m_chords.size() && m_chords[c].first < m_until; ++c)
	{
		for (const auto i : m_chords[c].notes) { m_grades[i] = Grade::Pending; }
	}
}

std::optional<double> WaitGate::gateTick() const
{
	if (finished()) { return std::nullopt; }
	return m_chords[m_current].first;
}

std::vector<std::size_t> WaitGate::currentNotes() const
{
	return finished() ? std::vector<std::size_t>{} : m_chords[m_current].notes;
}

bool WaitGate::press(int key)
{
	if (finished()) { return false; }
	const auto& chord = m_chords[m_current];
	const auto& notes = m_chart->notes();
	const bool belongs = std::any_of(chord.notes.begin(), chord.notes.end(), [&](std::size_t i) { return notes[i].key == key; });
	if (!belongs)
	{
		++m_stats.wrongPresses;
		m_currentHadWrong = true;
		return false;
	}
	m_pressedKeys.insert(key);
	const bool complete = m_anyNote || std::all_of(chord.notes.begin(), chord.notes.end(),
		[&](std::size_t i) { return m_pressedKeys.count(notes[i].key) > 0; });
	if (complete)
	{
		for (const auto i : chord.notes) { m_grades[i] = m_currentHadWrong ? Grade::Ok : Grade::Perfect; }
		++m_stats.completed;
		m_stats.clean += m_currentHadWrong ? 0 : 1;
		++m_current;
		m_pressedKeys.clear();
		m_currentHadWrong = false;
	}
	return true;
}

WaitGate::Stats WaitGate::stats() const
{
	return m_stats;
}

// --- TempoCoach -------------------------------------------------------------

int TempoCoach::onPass(double overallPercent, int tempoPercent)
{
	if (overallPercent >= GoodPass) { ++m_good; m_poor = 0; }
	else if (overallPercent < PoorPass) { ++m_poor; m_good = 0; }
	else { m_good = m_poor = 0; }

	if (m_good >= 2)
	{
		m_good = 0;
		return std::min(MaxPercent, tempoPercent + Step);
	}
	if (m_poor >= 2)
	{
		m_poor = 0;
		return std::max(MinPercent, tempoPercent - Step);
	}
	return tempoPercent;
}

} // namespace kf
