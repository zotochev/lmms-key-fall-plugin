/*
 * PlayheadClock.cpp - smooth playhead between the host's per-buffer position updates (no Qt)
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

#include "PlayheadClock.h"

#include <algorithm>
#include <cmath>

namespace kf {

double PlayheadClock::update(double rawTick, std::int64_t nowNs, double ticksPerSecond, bool playing)
{
	const auto snapTo = [&](double tick) {
		m_initialized = true;
		m_lastRaw = tick;
		m_anchorTick = tick;
		m_anchorNs = nowNs;
		m_display = tick;
		return m_display;
	};

	if (!playing || ticksPerSecond <= 0) { return snapTo(rawTick); }
	if (!m_initialized) { return snapTo(rawTick); }

	const double elapsed = std::clamp((nowNs - m_anchorNs) * 1e-9, 0.0, MaxExtrapolationSeconds);
	const double estimate = m_anchorTick + elapsed * ticksPerSecond;

	// Host moved backwards (loop wrap, seek) or far away from where we expected it
	if (rawTick < m_lastRaw - 1e-6 || std::abs(rawTick - estimate) > SnapSeconds * ticksPerSecond)
	{
		return snapTo(rawTick);
	}

	if (rawTick != m_lastRaw)
	{
		// New audio buffer: re-anchor on the authoritative host position
		m_lastRaw = rawTick;
		m_anchorTick = rawTick;
		m_anchorNs = nowNs;
		// Our extrapolation may have run slightly ahead; hold still instead of moving back
		m_display = std::max(m_display, rawTick);
		return m_display;
	}

	m_display = std::max(m_display, estimate);
	return m_display;
}

} // namespace kf
