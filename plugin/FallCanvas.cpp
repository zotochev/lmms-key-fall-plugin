/*
 * FallCanvas.cpp - falling notes of the practised clip above the instrument
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

#include "FallCanvas.h"

#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <chrono>
#include <cmath>

#include "KeyboardWidget.h"
#include "core/KeyboardLayout.h"

namespace lmms::gui {

namespace {

// Palette from PLAN.md (UI/UX)
const QColor Background{0x1e, 0x22, 0x26};
const QColor BarLine{0x3a, 0x40, 0x48};
const QColor BeatLine{0x2a, 0x2f, 0x35};
const QColor BarNumber{0x8a, 0x8f, 0x98};
const QColor HitLine{0xf0, 0xe4, 0x42};
const QColor NoteFill{0x56, 0xb4, 0xe9};
const QColor NoteSounding{0x9f, 0xd6, 0xf4};
const QColor MessageText{0x8a, 0x8f, 0x98};
const QColor GhostColor{0x56, 0xb4, 0xe9};
const QColor HitColor{0x00, 0x9e, 0x73};
const QColor MissColor{0x8a, 0x8f, 0x98};
const QColor WorstBar{0xd5, 0x5e, 0x00};
const QColor MiddleBar{0xf0, 0xe4, 0x42};
const QColor CardBackground{0x16, 0x19, 0x1c, 235};
const QColor CardText{0xe6, 0xe8, 0xeb};

QColor mix(const QColor& a, const QColor& b, double t)
{
	return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
		a.blueF() + (b.blueF() - a.blueF()) * t);
}

//! Bar accuracy 0..1 -> vermillion .. yellow .. green (Okabe-Ito endpoints)
QColor heat(double accuracy)
{
	return accuracy < 0.5 ? mix(WorstBar, MiddleBar, accuracy * 2) : mix(MiddleBar, HitColor, (accuracy - 0.5) * 2);
}

constexpr double MinNoteHeight = 6.0; // px, so very short notes stay visible
constexpr double NoteGap = 3.0;       // px between a note's end and the next note on the same key

std::int64_t nowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

FallCanvas::FallCanvas(KeyboardWidget* keyboard, QWidget* parent)
	: QWidget(parent)
	, m_keyboard(keyboard)
	, m_chart(std::make_shared<const kf::Chart>())
{
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void FallCanvas::setChart(std::shared_ptr<const kf::Chart> chart)
{
	m_chart = chart ? std::move(chart) : std::make_shared<const kf::Chart>();
	update();
}

void FallCanvas::setTimeline(const Timeline& timeline)
{
	m_timeline = timeline;
	update();
}

void FallCanvas::setLookaheadSeconds(double seconds)
{
	m_lookaheadSeconds = seconds;
	update();
}

void FallCanvas::setMessage(const QString& message)
{
	if (message == m_message) { return; }
	m_message = message;
	update();
}

void FallCanvas::setSummary(std::optional<kf::ScoreSummary> summary)
{
	m_summary = std::move(summary);
	update();
}

void FallCanvas::notePress(std::uint16_t control, bool on, std::int64_t timeNs)
{
	if (on)
	{
		m_ghosts.push_back({control, timeNs, 0});
		return;
	}
	for (auto& g : m_ghosts)
	{
		if (g.control == control && g.endNs == 0) { g.endNs = std::max(timeNs, g.startNs + 1); }
	}
}

void FallCanvas::clearPresses()
{
	m_ghosts.clear();
	update();
}

bool FallCanvas::isAnimating(std::int64_t now) const
{
	const double pxPerSecond = height() / m_lookaheadSeconds;
	return std::any_of(m_ghosts.begin(), m_ghosts.end(), [&](const Ghost& g) {
		return g.endNs == 0 || (now - g.endNs) * 1e-9 * pxPerSecond < height();
	});
}

void FallCanvas::wheelEvent(QWheelEvent* event)
{
	if ((event->modifiers() & Qt::ControlModifier) && onLookaheadStep)
	{
		onLookaheadStep(event->angleDelta().y() > 0 ? -1 : 1); // wheel up zooms in: less time on screen
		event->accept();
		return;
	}
	QWidget::wheelEvent(event);
}

double FallCanvas::yForTick(double tick, double lookaheadTicks) const
{
	// Hit line at the bottom edge (right above the keyboard), the future above it
	return height() - (tick - m_timeline.now) / lookaheadTicks * height();
}

void FallCanvas::paintGrid(QPainter& p, double lookaheadTicks) const
{
	const int tpb = std::max(1, m_timeline.ticksPerBar);
	const double ticksPerBeat = static_cast<double>(tpb) / std::max(1, m_timeline.beatsPerBar);
	const double from = m_timeline.now + m_timeline.songTickOfChartZero; // song ticks
	const double to = from + lookaheadTicks;

	p.setRenderHint(QPainter::Antialiasing, false);
	QFont f = font();
	f.setPixelSize(10);
	p.setFont(f);
	for (double beat = std::ceil(from / ticksPerBeat) * ticksPerBeat; beat <= to; beat += ticksPerBeat)
	{
		const double y = yForTick(beat - m_timeline.songTickOfChartZero, lookaheadTicks);
		const long long beatIndex = std::llround(beat / ticksPerBeat);
		const bool isBar = beatIndex % std::max(1, m_timeline.beatsPerBar) == 0;
		p.setPen(isBar ? BarLine : BeatLine);
		p.drawLine(QPointF(0, y), QPointF(width(), y));
		if (isBar)
		{
			p.setPen(BarNumber);
			p.drawText(QPointF(4, y - 3), QString::number(std::llround(beat / tpb) + 1));
		}
	}
}

void FallCanvas::paintNotes(QPainter& p, double lookaheadTicks) const
{
	const auto* layout = m_keyboard->instrumentLayout();
	if (!layout || m_chart->empty()) { return; }
	const auto& notes = m_chart->notes();
	const double now = m_timeline.now;

	p.setRenderHint(QPainter::Antialiasing, true);
	for (std::size_t i = m_chart->firstVisible(now); i < notes.size(); ++i)
	{
		const auto& n = notes[i];
		if (n.start > now + lookaheadTicks) { break; }
		if (n.end() <= now) { continue; }
		const auto candidates = layout->candidates(n.key);
		if (candidates.empty()) { continue; } // counted in the status line
		const QRectF lane = m_keyboard->controlRect(candidates.front());

		double top = yForTick(n.end(), lookaheadTicks);
		double bottom = yForTick(n.start, lookaheadTicks);
		// A small gap at the note's end keeps repeated notes on one key visibly separate
		if (bottom - top > MinNoteHeight + NoteGap) { top += NoteGap; }
		if (bottom - top < MinNoteHeight) { top = bottom - MinNoteHeight; }
		const QRectF r(lane.left() + 1.5, top, std::max(2.0, lane.width() - 3), bottom - top);

		const bool sounding = n.start <= now && now < n.end();
		const kf::Grade grade = m_grades && i < m_grades->size() ? (*m_grades)[i] : kf::Grade::Pending;
		QColor fill = kf::isHit(grade) ? HitColor : grade == kf::Grade::Miss ? MissColor : sounding ? NoteSounding : NoteFill;
		fill.setAlphaF(0.55 + 0.45 * n.velocity / 127.0); // soft notes stay visible
		const double radius = std::min(4.0, r.width() / 3);
		p.setPen(QPen(fill.darker(160), 1));
		p.setBrush(fill);
		p.drawRoundedRect(r, radius, radius);
	}
}

void FallCanvas::paintGhosts(QPainter& p, std::int64_t now) const
{
	const double pxPerSecond = height() / m_lookaheadSeconds;
	QColor fill = GhostColor;
	fill.setAlpha(70);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(QPen(GhostColor, 1.5));
	p.setBrush(fill);
	for (const auto& g : m_ghosts)
	{
		const QRectF lane = m_keyboard->controlRect(g.control);
		// The press grows upwards from the hit line while held, then floats away
		const double top = height() - (now - g.startNs) * 1e-9 * pxPerSecond;
		const double bottom = g.endNs ? height() - (now - g.endNs) * 1e-9 * pxPerSecond : height();
		if (bottom < 0) { continue; }
		p.drawRoundedRect(QRectF(lane.left() + 3, top, std::max(2.0, lane.width() - 6), std::max(2.0, bottom - top)), 3, 3);
	}
}

void FallCanvas::paintStatus(QPainter& p) const
{
	QFont f = font();
	f.setPixelSize(12);
	p.setFont(f);
	p.setPen(MessageText);
	const double now = m_timeline.now;
	const int tpb = std::max(1, m_timeline.ticksPerBar);
	if (now < 0)
	{
		const int bars = static_cast<int>(std::ceil(-now / tpb));
		p.drawText(rect().adjusted(0, 12, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
			bars <= 1 ? tr("The clip starts in less than a bar") : tr("The clip starts in %1 bars").arg(bars));
	}
	else if (now >= m_chart->length() && m_chart->length() > 0)
	{
		p.drawText(rect().adjusted(0, 12, 0, 0), Qt::AlignHCenter | Qt::AlignTop, tr("End of the clip"));
	}

	if (const auto* layout = m_keyboard->instrumentLayout())
	{
		int outside = 0;
		for (const auto& n : m_chart->notes()) { outside += layout->covers(n.key) ? 0 : 1; }
		if (outside > 0)
		{
			p.drawText(rect().adjusted(8, 0, -8, -6), Qt::AlignRight | Qt::AlignBottom,
				tr("%n note(s) of this clip are outside the selected layout", nullptr, outside));
		}
	}
}

void FallCanvas::paintSummary(QPainter& p) const
{
	const auto& s = *m_summary;
	const double w = std::min(width() - 24.0, 560.0);
	// Without a heat map (wait mode) the card is shorter
	const double cardHeight = s.barAccuracy.empty() ? 140 : 190;
	const QRectF card((width() - w) / 2, std::max(8.0, height() / 2.0 - cardHeight / 2), w, cardHeight);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(QPen(BarLine, 1));
	p.setBrush(CardBackground);
	p.drawRoundedRect(card, 8, 8);

	const QRectF inner = card.adjusted(16, 12, -16, -12);
	QFont f = font();
	f.setPixelSize(20);
	f.setBold(true);
	p.setFont(f);
	p.setPen(CardText);
	const QString stars = QString(s.stars, QChar(0x2605)) + QString(3 - s.stars, QChar(0x2606));
	p.drawText(inner, Qt::AlignHCenter | Qt::AlignTop, s.waitMode
		? tr("Chords %1 / %2   Clean %3%   %4").arg(s.chordsCompleted).arg(s.chords).arg(qRound(s.notesPercent)).arg(stars)
		: tr("Notes %1%   Rhythm %2%   %3").arg(qRound(s.notesPercent)).arg(qRound(s.rhythmPercent)).arg(stars));

	f.setPixelSize(12);
	f.setBold(false);
	p.setFont(f);
	p.setPen(MessageText);
	QString details = s.waitMode
		? tr("Wait mode · %n wrong key(s) on the way", nullptr, s.wrongPresses)
		: tr("Hit %1 of %2 notes · best streak %3").arg(s.hits).arg(s.expected).arg(s.bestStreak);
	if (!s.waitMode && s.extras > 0) { details += tr(" · %n extra note(s)", nullptr, s.extras); }
	p.drawText(inner.adjusted(0, 30, 0, 0), Qt::AlignHCenter | Qt::AlignTop, details);

	// Plain-language timing feedback (pedagogy review): consistent bias vs unsteady pulse
	QString timing;
	const int mean = qRound(std::abs(s.meanDeltaMs));
	const int spread = qRound(s.stddevDeltaMs);
	if (s.waitMode)
	{
		timing = s.chordsCompleted == s.chords
			? tr("All chords found. Next step: play it with timing (switch Wait off), at a slower tempo if needed.")
			: tr("Stopped before the end. Wait mode never rushes you: take your time to find each chord.");
	}
	else if (s.hits == 0 && s.extras == 0 && s.brushes == 0)
	{
		timing = tr("No key presses arrived. Check “MIDI in” above, or let KeyFall play: ⚙ → Auto-play (demo).");
	}
	else if (s.hits < 8) { timing = tr("Play a few more notes for timing feedback."); }
	else if (mean > 25 && spread < 40)
	{
		timing = (s.meanDeltaMs > 0 ? tr("You play about %1 ms late on average.") : tr("You play about %1 ms early on average.")).arg(mean)
			+ tr(" If it is the same in every song, recalibrate (⚙ → Calibrate latency).");
	}
	else if (spread > 60) { timing = tr("Your timing varies a lot (±%1 ms): practise slowly with the metronome.").arg(spread); }
	else { timing = tr("Your timing is centred (±%1 ms).").arg(spread); }
	p.drawText(inner.adjusted(0, 52, 0, 0), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, timing);

	// Heat map: one cell per bar, the most actionable view of where to practise
	if (!s.barAccuracy.empty())
	{
		const double cellsTop = inner.top() + 100;
		const double cellW = std::min(28.0, inner.width() / s.barAccuracy.size());
		const double left = inner.center().x() - cellW * s.barAccuracy.size() / 2;
		f.setPixelSize(9);
		p.setFont(f);
		for (std::size_t b = 0; b < s.barAccuracy.size(); ++b)
		{
			const QRectF cell(left + b * cellW + 1, cellsTop, cellW - 2, 16);
			const double acc = s.barAccuracy[b];
			p.setPen(Qt::NoPen);
			p.setBrush(acc < 0 ? BeatLine : heat(acc));
			p.drawRoundedRect(cell, 2, 2);
			if (cellW >= 16)
			{
				p.setPen(MessageText);
				p.drawText(QRectF(cell.left(), cell.bottom() + 1, cell.width(), 12), Qt::AlignHCenter | Qt::AlignTop,
					QString::number(s.firstBar + static_cast<int>(b)));
			}
		}
	}

	f.setPixelSize(11);
	p.setFont(f);
	p.setPen(MessageText);
	p.drawText(inner, Qt::AlignHCenter | Qt::AlignBottom, tr("Press Play (song) or Practise to try again"));
}

void FallCanvas::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), Background);
	const std::int64_t now = nowNs();

	if (!m_message.isEmpty())
	{
		p.setPen(MessageText);
		QFont f = font();
		f.setPixelSize(13);
		p.setFont(f);
		p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, m_message);
		paintGhosts(p, now);
		return;
	}

	const double lookaheadTicks = std::max(1.0, m_lookaheadSeconds * m_timeline.ticksPerSecond);
	paintGrid(p, lookaheadTicks);
	paintNotes(p, lookaheadTicks);
	paintGhosts(p, now);

	p.setRenderHint(QPainter::Antialiasing, false);
	p.fillRect(QRectF(0, height() - 2, width(), 2), HitLine);
	paintStatus(p);
	if (m_summary && !m_timeline.playing) { paintSummary(p); }

	// Ghosts that floated out of view are dropped here, where their age is known
	const double pxPerSecond = height() / m_lookaheadSeconds;
	m_ghosts.erase(std::remove_if(m_ghosts.begin(), m_ghosts.end(), [&](const Ghost& g) {
		return g.endNs != 0 && (now - g.endNs) * 1e-9 * pxPerSecond > height();
	}), m_ghosts.end());
}

} // namespace lmms::gui
