/*
 * Chart.h - notes of the practised clip, normalised for display and scoring (no Qt)
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

#ifndef KEYFALL_CORE_CHART_H
#define KEYFALL_CORE_CHART_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace kf {

//! LMMS ticks per quarter note (TimePos: 192 ticks per 4/4 bar)
constexpr double TicksPerQuarter = 48.0;

//! A note as stored in the clip: position relative to the clip's note origin
struct RawNote
{
	double pos = 0;    //!< ticks
	double length = 0; //!< ticks; LMMS step notes may have zero or negative length
	int key = 60;
	int velocity = 100;
};

//! A note on the chart timeline: tick 0 is the left edge of the clip
struct ChartNote
{
	double start = 0;
	double length = 0;
	std::uint8_t key = 60;
	std::uint8_t velocity = 100;

	double end() const { return start + length; }
};

struct ChartStats
{
	int outsideClip = 0;     //!< hidden by the clip's left/right edges, not played by LMMS either
	int overlapsTrimmed = 0; //!< same-key notes overlapping: the earlier one was shortened
	int duplicatesDropped = 0;
	int shortExtended = 0;   //!< zero-length (step) or tiny notes given a minimal length
};

class Chart
{
public:
	//! Notes shorter than this are drawn and scored with this length (1/8 of a quarter)
	static constexpr double MinLength = TicksPerQuarter / 8;

	Chart() = default;

	//! Mirrors InstrumentTrack::play(): a note sounds if
	//! -startTimeOffset <= pos < clipLength - startTimeOffset; its chart tick is pos + startTimeOffset.
	static Chart build(const std::vector<RawNote>& notes, double clipLength, double startTimeOffset,
		ChartStats* stats = nullptr);

	const std::vector<ChartNote>& notes() const { return m_notes; }
	bool empty() const { return m_notes.empty(); }
	double length() const { return m_length; }

	//! Lowest and highest key; {0, -1} when empty
	std::pair<int, int> keyRange() const;

	//! Index of the first note that may still be sounding at `tick` (notes are sorted by start)
	std::size_t firstVisible(double tick) const;

private:
	std::vector<ChartNote> m_notes;
	double m_length = 0;
	double m_maxNoteLength = 0;
};

//! Keyboard range fitted to a clip (PLAN.md UI/UX): +-2 semitones, widened to whole
//! octaves C..B, at least two octaves, clamped to MIDI 0..127. Empty clip: C3..C5.
std::pair<int, int> autoKeyRange(int lowestKey, int highestKey);

} // namespace kf

#endif // KEYFALL_CORE_CHART_H
