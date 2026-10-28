/*
 * Scorer.h - judges played notes against the chart (no Qt)
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

#ifndef KEYFALL_CORE_SCORER_H
#define KEYFALL_CORE_SCORER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "Chart.h"

//! Scoring rules from PLAN.md §5 (pedagogy review):
//! - hit windows are a fraction of the beat, clamped to ms bounds, per tolerance preset;
//! - a press matches the earliest unjudged note of that key inside the window (FIFO,
//!   so one press never "steals" the next repeated note);
//! - notes starting within 30 ms form a chord: pitch is scored per note, rhythm by the
//!   chord's mean onset;
//! - a wrong neighbouring key within 30 ms of a written note is a brushed key: counted, not penalised;
//! - Notes % = hits / (expected + 0.5 x extra notes); Rhythm % = mean credit of hit chords
//!   (Perfect 1, Good 0.8, OK 0.5); a miss only hurts Notes %.
namespace kf {

enum class TolerancePreset : std::uint8_t
{
	Loose,
	Normal,
	Strict
};

struct TimingWindows
{
	double perfectMs = 0;
	double goodMs = 0;
	double okMs = 0; //!< beyond this a press does not count for the note
};

TimingWindows timingWindows(TolerancePreset preset, double beatMs);

enum class Grade : std::uint8_t
{
	Pending,
	Perfect,
	Good,
	Ok,
	Miss,
	Skipped //!< before the point where playback started: not scored
};

inline bool isHit(Grade g) { return g == Grade::Perfect || g == Grade::Good || g == Grade::Ok; }

struct PressResult
{
	enum class Kind : std::uint8_t
	{
		Hit,
		Extra, //!< no written note of that key nearby
		Brush  //!< a neighbouring key grazed together with a written note
	};
	Kind kind = Kind::Extra;
	std::size_t note = 0; //!< chart note index for Hit
	Grade grade = Grade::Pending;
	double deltaMs = 0;   //!< + late, - early
};

struct ScoreSummary
{
	int expected = 0; //!< judged notes (hits + misses)
	int hits = 0, misses = 0, extras = 0, brushes = 0, skipped = 0;
	int perfect = 0, good = 0, ok = 0;
	double notesPercent = 0;
	double rhythmPercent = 0;
	double overallPercent = 0; //!< 0.6 notes + 0.4 rhythm
	int stars = 0;             //!< 0..3
	double meanDeltaMs = 0;    //!< + late, - early (input offset already applied)
	double stddevDeltaMs = 0;
	int streak = 0, bestStreak = 0;
	//! Per song bar (index 0 = firstBar): fraction of hit notes, or -1 without judged notes
	std::vector<double> barAccuracy;
	int firstBar = 0;          //!< 1-based song bar number of barAccuracy[0]
};

class Scorer
{
public:
	static constexpr double ChordMs = 30;
	static constexpr double BrushMs = 30;

	void setChart(std::shared_ptr<const Chart> chart);
	void setTolerance(TolerancePreset preset) { m_preset = preset; }

	//! Begins a new run at `fromTick`: notes that started before it are Skipped.
	//! ticksPerSecond/ticksPerBeat give the tempo for converting windows to ticks.
	void start(double fromTick, double ticksPerSecond, double ticksPerBeat);
	bool running() const { return m_running; }
	void stop() { m_running = false; }
	//! Follow tempo automation during a run
	void setTempo(double ticksPerSecond, double ticksPerBeat);

	//! A key went down at chart tick `tick` (input offset already subtracted)
	PressResult press(int key, double tick);
	//! Notes whose window has fully passed become Miss; returns their indices
	std::vector<std::size_t> advance(double tick);
	//! True when every note of the run has been judged
	bool finished() const;

	const std::vector<Grade>& grades() const { return m_grades; }
	int streak() const { return m_streak; }
	//! Accuracy so far: hits / (judged + 0.5 x extras), 0..100
	double liveAccuracy() const;
	ScoreSummary summary(int ticksPerBar, double songTickOfChartZero) const;

private:
	double msToTicks(double ms) const { return ms * m_ticksPerSecond / 1000.0; }
	double ticksToMs(double ticks) const { return ticks * 1000.0 / m_ticksPerSecond; }
	TimingWindows windows() const { return timingWindows(m_preset, ticksToMs(m_ticksPerBeat)); }
	Grade gradeFor(double deltaMs) const;
	void buildChords();

	std::shared_ptr<const Chart> m_chart = std::make_shared<const Chart>();
	TolerancePreset m_preset = TolerancePreset::Normal;
	double m_ticksPerSecond = 96;
	double m_ticksPerBeat = TicksPerQuarter;
	bool m_running = false;

	std::vector<Grade> m_grades;
	std::vector<double> m_deltaMs;     //!< per note, valid for hits
	std::vector<std::size_t> m_chordOf; //!< chord index per note
	std::size_t m_chordCount = 0;
	int m_extras = 0, m_brushes = 0;
	int m_streak = 0, m_bestStreak = 0;
};

//! Latency calibration: median of tap offsets after dropping outliers beyond 2 sigma.
//! Needs at least 4 taps.
std::optional<double> calibrationOffsetMs(std::vector<double> tapDeltasMs);

} // namespace kf

#endif // KEYFALL_CORE_SCORER_H
