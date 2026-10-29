/*
 * FallCanvas.h - falling notes of the practised clip above the instrument
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

#ifndef LMMS_GUI_KEYFALL_FALL_CANVAS_H
#define LMMS_GUI_KEYFALL_FALL_CANVAS_H

#include <QWidget>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "core/Chart.h"
#include "core/Scorer.h"

namespace lmms::gui {

class KeyboardWidget;

//! Synthesia-style waterfall. Time runs top to bottom in chart ticks, so tempo
//! changes only change the scroll speed and the bar grid stays aligned. Note
//! columns come from the instrument view (KeyboardWidget::controlRect), never
//! from the pitch itself, so any layout lines up with its keys.
class FallCanvas : public QWidget
{
public:
	struct Timeline
	{
		double now = 0;            //!< chart tick at the hit line
		double ticksPerSecond = 96;
		int ticksPerBar = 192;
		int beatsPerBar = 4;
		double songTickOfChartZero = 0;
		bool playing = false;
	};

	//! Where a note's column comes from
	enum class LaneMode
	{
		AboveInstrument, //!< straight above its key / button (pianos, pads)
		ButtonLanes      //!< one equal lane per button, labelled under the hit line (2D button layouts)
	};

	FallCanvas(KeyboardWidget* keyboard, QWidget* parent = nullptr);

	void setLaneMode(LaneMode mode);
	LaneMode laneMode() const { return m_laneMode; }

	void setChart(std::shared_ptr<const kf::Chart> chart);
	void setTimeline(const Timeline& timeline);
	void setLookaheadSeconds(double seconds);
	//! Shown instead of the waterfall when there is nothing to play (no clip, no MIDI, ...)
	void setMessage(const QString& message);

	//! User presses become "ghost" bars rising from the hit line (wall-clock based,
	//! so they also work while the song is stopped)
	void notePress(std::uint16_t control, bool on, std::int64_t timeNs);
	void clearPresses();
	//! True while something moves without the transport (ghost bars)
	bool isAnimating(std::int64_t nowNs) const;

	//! Per-note results of the current run (owned by the caller), used to colour notes
	void setGrades(const std::vector<kf::Grade>* grades) { m_grades = grades; }
	//! Result card shown while the song is stopped; nullopt hides it
	void setSummary(std::optional<kf::ScoreSummary> summary);

	//! Called with ctrl+wheel steps (+1 = longer look-ahead)
	std::function<void(int)> onLookaheadStep;
	//! A bar of the result heat map was clicked (1-based song bar number)
	std::function<void(int)> onBarClicked;

protected:
	void paintEvent(QPaintEvent*) override;
	void wheelEvent(QWheelEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;

private:
	struct Ghost
	{
		std::uint16_t control;
		std::int64_t startNs;
		std::int64_t endNs; //!< 0 while the key is held
	};

	void paintGrid(QPainter& p, double lookaheadTicks) const;
	void paintNotes(QPainter& p, double lookaheadTicks) const;
	void paintGhosts(QPainter& p, std::int64_t nowNs) const;
	void paintStatus(QPainter& p) const;
	void paintSummary(QPainter& p) const;
	void paintLaneStrip(QPainter& p) const;
	double yForTick(double tick, double lookaheadTicks) const;
	//! Column of a control: above it on the instrument, or its own lane
	QRectF laneRect(std::uint16_t control) const;
	//! y of the hit line; in button-lane mode a label strip sits below it
	double hitY() const;

	KeyboardWidget* m_keyboard;
	LaneMode m_laneMode = LaneMode::AboveInstrument;
	std::shared_ptr<const kf::Chart> m_chart;
	Timeline m_timeline;
	double m_lookaheadSeconds = 3.0;
	QString m_message;
	std::vector<Ghost> m_ghosts;
	const std::vector<kf::Grade>* m_grades = nullptr;
	std::optional<kf::ScoreSummary> m_summary;
	//! Heat map cells of the visible result card, for clicks
	mutable std::vector<std::pair<QRectF, int>> m_barCells;
};

} // namespace lmms::gui

#endif // LMMS_GUI_KEYFALL_FALL_CANVAS_H
