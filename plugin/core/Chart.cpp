/*
 * Chart.cpp - notes of the practised clip, normalised for display and scoring (no Qt)
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

#include "Chart.h"

#include <algorithm>

namespace kf {

Chart Chart::build(const std::vector<RawNote>& notes, double clipLength, double startTimeOffset, ChartStats* stats)
{
	ChartStats local;
	ChartStats& s = stats ? *stats : local;
	s = {};

	Chart chart;
	chart.m_length = std::max(0.0, clipLength);

	std::vector<ChartNote> kept;
	kept.reserve(notes.size());
	for (const auto& n : notes)
	{
		if (n.key < 0 || n.key > 127 || n.pos < -startTimeOffset || n.pos >= clipLength - startTimeOffset)
		{
			++s.outsideClip;
			continue;
		}
		ChartNote c;
		c.start = n.pos + startTimeOffset;
		c.length = n.length;
		if (c.length < MinLength)
		{
			c.length = MinLength;
			++s.shortExtended;
		}
		c.length = std::min(c.length, std::max(MinLength, chart.m_length - c.start)); // the clip edge cuts notes
		c.key = static_cast<std::uint8_t>(n.key);
		c.velocity = static_cast<std::uint8_t>(std::clamp(n.velocity, 1, 127));
		kept.push_back(c);
	}

	// Same key: drop exact duplicates, cut a note where the next one on that key begins
	std::sort(kept.begin(), kept.end(), [](const ChartNote& a, const ChartNote& b) {
		return a.key != b.key ? a.key < b.key : a.start < b.start;
	});
	for (std::size_t i = 0; i < kept.size(); ++i)
	{
		auto& cur = kept[i];
		if (i + 1 < kept.size() && kept[i + 1].key == cur.key)
		{
			const auto& next = kept[i + 1];
			if (next.start <= cur.start)
			{
				++s.duplicatesDropped;
				continue; // keep the later copy, which carries the same start
			}
			if (next.start < cur.end())
			{
				cur.length = next.start - cur.start;
				++s.overlapsTrimmed;
			}
		}
		chart.m_notes.push_back(cur);
	}

	std::sort(chart.m_notes.begin(), chart.m_notes.end(), [](const ChartNote& a, const ChartNote& b) {
		return a.start != b.start ? a.start < b.start : a.key < b.key;
	});
	for (const auto& n : chart.m_notes) { chart.m_maxNoteLength = std::max(chart.m_maxNoteLength, n.length); }
	return chart;
}

std::pair<int, int> Chart::keyRange() const
{
	if (m_notes.empty()) { return {0, -1}; }
	int lo = 127, hi = 0;
	for (const auto& n : m_notes)
	{
		lo = std::min<int>(lo, n.key);
		hi = std::max<int>(hi, n.key);
	}
	return {lo, hi};
}

std::size_t Chart::firstVisible(double tick) const
{
	// Any note starting before (tick - longest note) has already ended
	const double from = tick - m_maxNoteLength;
	const auto it = std::lower_bound(m_notes.begin(), m_notes.end(), from,
		[](const ChartNote& n, double t) { return n.start < t; });
	return static_cast<std::size_t>(it - m_notes.begin());
}

std::pair<int, int> autoKeyRange(int lowestKey, int highestKey)
{
	if (lowestKey > highestKey) { return {48, 72}; }
	int lo = std::max(0, lowestKey - 2);
	int hi = std::min(127, highestKey + 2);
	lo -= lo % 12;                      // down to a C
	hi += 11 - hi % 12;                 // up to a B
	while (hi - lo + 1 < 24)
	{
		// Grow towards the side with more room, keeping the clip centred
		if (lo >= 12 && (lo - 12 >= 0) && (lowestKey - lo <= hi - highestKey)) { lo -= 12; }
		else { hi += 12; }
	}
	return {std::clamp(lo, 0, 127), std::clamp(hi, 0, 127)};
}

} // namespace kf
