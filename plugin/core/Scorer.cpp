/*
 * Scorer.cpp - judges played notes against the chart (no Qt)
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

#include "Scorer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace kf {

namespace {

struct WindowRule
{
	double beatFraction, minMs, maxMs;
};

// PLAN.md §5: [preset][perfect, good, ok]
constexpr std::array<std::array<WindowRule, 3>, 3> WindowTable = {{
	{{{1.0 / 16, 50, 90}, {1.0 / 8, 90, 160}, {1.0 / 4, 160, 250}}},  // Loose
	{{{1.0 / 24, 35, 60}, {1.0 / 12, 60, 110}, {1.0 / 6, 110, 180}}}, // Normal
	{{{1.0 / 32, 20, 40}, {1.0 / 16, 40, 75}, {1.0 / 8, 75, 120}}},   // Strict
}};

double credit(Grade g)
{
	switch (g)
	{
		case Grade::Perfect: return 1.0;
		case Grade::Good: return 0.8;
		case Grade::Ok: return 0.5;
		default: return 0.0;
	}
}

} // namespace

TimingWindows timingWindows(TolerancePreset preset, double beatMs)
{
	const auto& rules = WindowTable[static_cast<std::size_t>(preset)];
	const auto window = [&](const WindowRule& r) { return std::clamp(r.beatFraction * beatMs, r.minMs, r.maxMs); };
	return {window(rules[0]), window(rules[1]), window(rules[2])};
}

void Scorer::setChart(std::shared_ptr<const Chart> chart)
{
	m_chart = chart ? std::move(chart) : std::make_shared<const Chart>();
	m_running = false;
	m_grades.assign(m_chart->notes().size(), Grade::Pending);
	m_deltaMs.assign(m_chart->notes().size(), 0.0);
	m_extras = m_brushes = m_streak = m_bestStreak = 0;
}

void Scorer::setTempo(double ticksPerSecond, double ticksPerBeat)
{
	m_ticksPerSecond = std::max(1e-3, ticksPerSecond);
	m_ticksPerBeat = std::max(1.0, ticksPerBeat);
}

void Scorer::start(double fromTick, double ticksPerSecond, double ticksPerBeat)
{
	setTempo(ticksPerSecond, ticksPerBeat);
	const auto& notes = m_chart->notes();
	m_grades.assign(notes.size(), Grade::Pending);
	m_deltaMs.assign(notes.size(), 0.0);
	// A note that is already sounding when playback starts cannot be played in time
	const double tolerance = msToTicks(windows().okMs) * 0.5;
	for (std::size_t i = 0; i < notes.size(); ++i)
	{
		if (notes[i].start < fromTick - tolerance) { m_grades[i] = Grade::Skipped; }
	}
	m_extras = m_brushes = m_streak = m_bestStreak = 0;
	buildChords();
	m_running = true;
}

void Scorer::buildChords()
{
	const auto& notes = m_chart->notes();
	m_chordOf.assign(notes.size(), 0);
	m_chordCount = 0;
	const double chordTicks = msToTicks(ChordMs);
	double chordStart = 0;
	for (std::size_t i = 0; i < notes.size(); ++i)
	{
		if (i == 0 || notes[i].start - chordStart > chordTicks)
		{
			chordStart = notes[i].start;
			if (i > 0) { ++m_chordCount; }
		}
		m_chordOf[i] = m_chordCount;
	}
	if (!notes.empty()) { ++m_chordCount; }
}

Grade Scorer::gradeFor(double deltaMs) const
{
	const auto w = windows();
	const double d = std::abs(deltaMs);
	if (d <= w.perfectMs) { return Grade::Perfect; }
	if (d <= w.goodMs) { return Grade::Good; }
	if (d <= w.okMs) { return Grade::Ok; }
	return Grade::Miss;
}

PressResult Scorer::press(int key, double tick)
{
	PressResult result;
	if (!m_running) { return result; }
	const auto& notes = m_chart->notes();
	const double okTicks = msToTicks(windows().okMs);

	const auto from = std::lower_bound(notes.begin(), notes.end(), tick - okTicks,
		[](const ChartNote& n, double t) { return n.start < t; });
	// Earliest unjudged note of this key in the window: repeated notes are taken in order
	for (auto it = from; it != notes.end() && it->start <= tick + okTicks; ++it)
	{
		const auto i = static_cast<std::size_t>(it - notes.begin());
		if (it->key != key || m_grades[i] != Grade::Pending) { continue; }
		result.kind = PressResult::Kind::Hit;
		result.note = i;
		result.deltaMs = ticksToMs(tick - it->start);
		result.grade = gradeFor(result.deltaMs);
		m_grades[i] = result.grade;
		m_deltaMs[i] = result.deltaMs;
		m_bestStreak = std::max(m_bestStreak, ++m_streak);
		return result;
	}

	// A neighbouring key grazed together with a written note is not a real mistake
	const double brushTicks = msToTicks(BrushMs);
	const auto near = std::lower_bound(notes.begin(), notes.end(), tick - brushTicks,
		[](const ChartNote& n, double t) { return n.start < t; });
	for (auto it = near; it != notes.end() && it->start <= tick + brushTicks; ++it)
	{
		const auto i = static_cast<std::size_t>(it - notes.begin());
		if (std::abs(it->key - key) == 1 && m_grades[i] != Grade::Skipped)
		{
			result.kind = PressResult::Kind::Brush;
			++m_brushes;
			return result;
		}
	}

	result.kind = PressResult::Kind::Extra;
	++m_extras;
	m_streak = 0;
	return result;
}

std::vector<std::size_t> Scorer::advance(double tick)
{
	std::vector<std::size_t> missed;
	if (!m_running) { return missed; }
	const auto& notes = m_chart->notes();
	const double okTicks = msToTicks(windows().okMs);
	for (std::size_t i = 0; i < notes.size() && notes[i].start + okTicks < tick; ++i)
	{
		if (m_grades[i] == Grade::Pending)
		{
			m_grades[i] = Grade::Miss;
			m_streak = 0;
			missed.push_back(i);
		}
	}
	return missed;
}

bool Scorer::finished() const
{
	return std::none_of(m_grades.begin(), m_grades.end(), [](Grade g) { return g == Grade::Pending; });
}

double Scorer::liveAccuracy() const
{
	int hits = 0, misses = 0;
	for (const auto g : m_grades)
	{
		hits += isHit(g) ? 1 : 0;
		misses += g == Grade::Miss ? 1 : 0;
	}
	const double denominator = hits + misses + 0.5 * m_extras;
	return denominator > 0 ? 100.0 * hits / denominator : 0.0;
}

ScoreSummary Scorer::summary(int ticksPerBar, double songTickOfChartZero) const
{
	ScoreSummary s;
	const auto& notes = m_chart->notes();
	s.extras = m_extras;
	s.brushes = m_brushes;
	s.streak = m_streak;
	s.bestStreak = m_bestStreak;

	std::vector<double> hitDeltas;
	std::vector<std::vector<double>> chordDeltas(m_chordCount);
	for (std::size_t i = 0; i < notes.size(); ++i)
	{
		switch (m_grades[i])
		{
			case Grade::Perfect: ++s.perfect; break;
			case Grade::Good: ++s.good; break;
			case Grade::Ok: ++s.ok; break;
			case Grade::Miss: ++s.misses; break;
			case Grade::Skipped: ++s.skipped; break;
			default: break;
		}
		if (isHit(m_grades[i]))
		{
			hitDeltas.push_back(m_deltaMs[i]);
			if (i < m_chordOf.size()) { chordDeltas[m_chordOf[i]].push_back(m_deltaMs[i]); }
		}
	}
	s.hits = s.perfect + s.good + s.ok;
	s.expected = s.hits + s.misses;

	const double notesDenominator = s.expected + 0.5 * s.extras;
	s.notesPercent = notesDenominator > 0 ? 100.0 * s.hits / notesDenominator : 0;

	// Rhythm: a chord counts once, judged by the mean onset of the notes that were hit
	double creditSum = 0;
	int chordsHit = 0;
	for (const auto& deltas : chordDeltas)
	{
		if (deltas.empty()) { continue; }
		const double mean = std::accumulate(deltas.begin(), deltas.end(), 0.0) / deltas.size();
		creditSum += credit(gradeFor(mean));
		++chordsHit;
	}
	s.rhythmPercent = chordsHit > 0 ? 100.0 * creditSum / chordsHit : 0;
	s.overallPercent = 0.6 * s.notesPercent + 0.4 * s.rhythmPercent;
	s.stars = s.expected == 0 ? 0 : s.overallPercent >= 90 ? 3 : s.overallPercent >= 75 ? 2 : s.overallPercent >= 50 ? 1 : 0;

	if (!hitDeltas.empty())
	{
		s.meanDeltaMs = std::accumulate(hitDeltas.begin(), hitDeltas.end(), 0.0) / hitDeltas.size();
		double var = 0;
		for (const double d : hitDeltas) { var += (d - s.meanDeltaMs) * (d - s.meanDeltaMs); }
		s.stddevDeltaMs = std::sqrt(var / hitDeltas.size());
	}

	// Per-bar heat map over the bars that had judged notes
	const int tpb = std::max(1, ticksPerBar);
	int firstBar = 0, lastBar = -1;
	for (std::size_t i = 0; i < notes.size(); ++i)
	{
		if (m_grades[i] == Grade::Skipped || m_grades[i] == Grade::Pending) { continue; }
		const int bar = static_cast<int>(std::floor((songTickOfChartZero + notes[i].start) / tpb));
		if (lastBar < firstBar) { firstBar = lastBar = bar; }
		firstBar = std::min(firstBar, bar);
		lastBar = std::max(lastBar, bar);
	}
	if (lastBar >= firstBar)
	{
		std::vector<int> hits(lastBar - firstBar + 1, 0), total(lastBar - firstBar + 1, 0);
		for (std::size_t i = 0; i < notes.size(); ++i)
		{
			if (m_grades[i] == Grade::Skipped || m_grades[i] == Grade::Pending) { continue; }
			const int bar = static_cast<int>(std::floor((songTickOfChartZero + notes[i].start) / tpb)) - firstBar;
			++total[bar];
			hits[bar] += isHit(m_grades[i]) ? 1 : 0;
		}
		s.firstBar = firstBar + 1;
		for (std::size_t b = 0; b < total.size(); ++b)
		{
			s.barAccuracy.push_back(total[b] ? static_cast<double>(hits[b]) / total[b] : -1.0);
		}
	}
	return s;
}

std::optional<double> calibrationOffsetMs(std::vector<double> taps)
{
	if (taps.size() < 4) { return std::nullopt; }
	const double mean = std::accumulate(taps.begin(), taps.end(), 0.0) / taps.size();
	double var = 0;
	for (const double t : taps) { var += (t - mean) * (t - mean); }
	const double sigma = std::sqrt(var / taps.size());
	if (sigma > 0)
	{
		taps.erase(std::remove_if(taps.begin(), taps.end(), [&](double t) { return std::abs(t - mean) > 2 * sigma; }),
			taps.end());
	}
	std::sort(taps.begin(), taps.end());
	const std::size_t n = taps.size();
	return n % 2 ? taps[n / 2] : (taps[n / 2 - 1] + taps[n / 2]) / 2;
}

} // namespace kf
