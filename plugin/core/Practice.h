/*
 * Practice.h - KeyFall's own practice transport and wait mode (no Qt)
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

#ifndef KEYFALL_CORE_PRACTICE_H
#define KEYFALL_CORE_PRACTICE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "Chart.h"
#include "Scorer.h"

namespace kf {

//! Practice modes run on KeyFall's own clock while the LMMS transport stays stopped:
//! pausing LMMS from a tool plugin is unreliable and would change what the user hears
//! (PLAN.md §0). The clock supports slower tempo, an A-B loop, a count-in and a "gate"
//! that holds time at a chord until it is played (wait mode).
class PracticeClock
{
public:
	struct Loop
	{
		double begin = 0; //!< chart ticks
		double end = 0;
	};

	//! ticksPerSecond: song tempo at 100 %; tempoFactor: 0.25 .. 1.0
	void setTempo(double ticksPerSecond, double tempoFactor);
	void setLoop(std::optional<Loop> loop) { m_loop = loop; }

	//! Starts at `fromTick` (a count-in starts earlier, i.e. at a negative distance to the music)
	void start(double fromTick, std::int64_t nowNs);
	void stop() { m_running = false; }
	bool running() const { return m_running; }

	//! Advances to `nowNs`. Time never passes `gateTick` (wait mode). Returns true when the
	//! loop wrapped back to its beginning during this update.
	bool update(std::int64_t nowNs, std::optional<double> gateTick = std::nullopt);

	double position() const { return m_position; }
	double ticksPerSecond() const { return m_ticksPerSecond * m_tempoFactor; }
	bool holding() const { return m_holding; } //!< true while time stands at the gate

private:
	bool m_running = false;
	bool m_holding = false;
	double m_position = 0;
	std::int64_t m_lastNs = 0;
	double m_ticksPerSecond = 96;
	double m_tempoFactor = 1;
	std::optional<Loop> m_loop;
};

//! Wait mode (the pedagogy review's most useful mode): time stops at each chord until
//! all of its keys have been pressed. No timing is scored; what counts is completing
//! the chords and how many wrong keys it took.
class WaitGate
{
public:
	struct Stats
	{
		int chords = 0;     //!< chords in the run
		int completed = 0;
		int clean = 0;      //!< completed without any wrong key
		int wrongPresses = 0;
	};

	//! chordTicks: notes starting closer than this form one chord
	void setChart(std::shared_ptr<const Chart> chart, double chordTicks);
	//! Beginner option: any one key of a chord is enough
	void setAnyNoteCompletes(bool any) { m_anyNote = any; }

	//! Restarts at the first chord at or after fromTick (and before untilTick, e.g. a loop end).
	//! `active` (one flag per chart note) limits the chords to some notes, e.g. one hand.
	void reset(double fromTick, double untilTick = 1e300, const std::vector<bool>* active = nullptr);

	//! Start of the chord the user has to play, or nothing when the run is complete
	std::optional<double> gateTick() const;
	//! Chart notes of the current chord
	std::vector<std::size_t> currentNotes() const;

	//! A key went down. Returns true if it belongs to the current chord.
	//! Completes the chord (and moves on) once all its keys have been pressed.
	bool press(int key);

	bool finished() const { return m_current >= m_chords.size() || m_chords[m_current].first >= m_until; }
	const std::vector<Grade>& grades() const { return m_grades; } //!< Perfect = completed, Miss = wrong key used
	Stats stats() const;

private:
	struct Chord
	{
		double first;                   //!< start tick
		std::vector<std::size_t> notes; //!< chart indices
	};

	void buildChords(const std::vector<bool>* active);

	std::shared_ptr<const Chart> m_chart = std::make_shared<const Chart>();
	double m_chordTicks = 0;
	std::vector<Chord> m_chords;
	std::size_t m_current = 0;
	double m_until = 1e300;
	std::set<int> m_pressedKeys; //!< keys of the current chord already played
	bool m_currentHadWrong = false;
	bool m_anyNote = false;
	std::vector<Grade> m_grades;
	Stats m_stats;
};

//! Automatic tempo for loop practice (pedagogy review): two clean passes in a row
//! (>= 90 %) speed up by 5 %, two poor ones (< 70 %) slow down by 5 %, within 25..100 %.
class TempoCoach
{
public:
	static constexpr int Step = 5;
	static constexpr int MinPercent = 25;
	static constexpr int MaxPercent = 100;
	static constexpr double GoodPass = 90;
	static constexpr double PoorPass = 70;

	//! Feeds the overall score of a finished pass; returns the tempo for the next one
	int onPass(double overallPercent, int tempoPercent);
	void reset() { m_good = m_poor = 0; }

private:
	int m_good = 0;
	int m_poor = 0;
};

} // namespace kf

#endif // KEYFALL_CORE_PRACTICE_H
