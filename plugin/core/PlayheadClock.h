/*
 * PlayheadClock.h - smooth playhead between the host's per-buffer position updates (no Qt)
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

#ifndef KEYFALL_CORE_PLAYHEAD_CLOCK_H
#define KEYFALL_CORE_PLAYHEAD_CLOCK_H

#include <cstdint>

namespace kf {

//! LMMS advances the song position once per audio buffer (~5-20 ms), so drawing
//! that value directly makes falling notes stutter. This clock extrapolates with
//! the wall clock between updates, never runs backwards by small amounts, and
//! snaps on real jumps (seek, loop wrap, tempo glitches).
class PlayheadClock
{
public:
	//! Longest extrapolation past the last host update; protects against a stalled engine
	static constexpr double MaxExtrapolationSeconds = 0.1;
	//! A difference larger than this between host and estimate is a jump, not jitter
	static constexpr double SnapSeconds = 0.15;

	//! rawTick: host position (fractional ticks); ticksPerSecond: current tempo.
	//! Returns the smoothed position to draw.
	double update(double rawTick, std::int64_t nowNs, double ticksPerSecond, bool playing);

	//! Forget history, e.g. after the host reported a jump
	void reset() { m_initialized = false; }

	double position() const { return m_display; }

private:
	bool m_initialized = false;
	double m_lastRaw = 0;
	double m_anchorTick = 0;
	std::int64_t m_anchorNs = 0;
	double m_display = 0;
};

} // namespace kf

#endif // KEYFALL_CORE_PLAYHEAD_CLOCK_H
