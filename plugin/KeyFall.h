/*
 * KeyFall.h - MIDI keyboard practice trainer (tool plugin)
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

#ifndef LMMS_KEYFALL_H
#define LMMS_KEYFALL_H

#include <QPointer>
#include <array>
#include <QStringList>
#include <QVector>
#include <memory>
#include <mutex>
#include <vector>

#include "KeyFallView.h"
#include "LayoutRepository.h"
#include "MidiClip.h"
#include "ToolPlugin.h"
#include "core/Chart.h"
#include "core/InputResolver.h"

namespace lmms {

class InstrumentTrack;

//! One clip the user can practise
struct ClipChoice
{
	QPointer<MidiClip> clip;
	QString label; //!< "Track > clip (bars a-b)"
};

//! Snapshot of the LMMS transport, already mapped onto the chart timeline
struct TransportSample
{
	double chartTick = 0;      //!< host position in chart ticks (0 = clip's left edge), per audio buffer
	double ticksPerSecond = 96;
	double latencyTicks = 0;   //!< the song position runs ahead of what is audible by about one buffer
	bool playing = false;
	bool supportedMode = true; //!< false while the Pattern editor plays
	int ticksPerBar = 192;
	int beatsPerBar = 4;
	double songTickOfChartZero = 0; //!< for bar numbers and grid alignment
};

//! Tool plugin model. Lives for the whole LMMS session (created at startup,
//! survives project reloads) and is never saved into a project: per-user
//! settings go through ConfigManager instead of saveSettings/loadSettings.
//!
//! Hears the user's keyboard through the instrument tracks: every track reports the
//! notes that reach it from MIDI input (the signals Piano Roll recording uses), so no
//! MIDI port of its own and no LMMS patch are needed. KeyFall never produces MIDI output.
class KeyFall : public ToolPlugin
{
	Q_OBJECT
public:
	//! How strictly note timing is judged (hit-window presets, see PLAN.md §5)
	enum class Tolerance
	{
		Loose,
		Normal,
		Strict
	};

	KeyFall();

	Tolerance tolerance() const { return m_tolerance; }
	//! Also persists the choice in the user's LMMS config
	void setTolerance(Tolerance tolerance);

	//! Input + display delay measured by calibration (ms, + = presses arrive late); persisted
	double inputOffsetMs() const { return m_inputOffsetMs; }
	void setInputOffsetMs(double ms);

	QString nodeName() const override;
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}

	gui::PluginView* instantiateView(QWidget*) override { return new gui::KeyFallView(this); }

	// --- MIDI input -------------------------------------------------------

	//! Names of the instrument tracks whose MIDI input is switched on
	QStringList listeningTracks() const;

	//! Moves all MIDI events received since the last call into out (GUI thread)
	void drainInput(std::vector<kf::MidiInputEvent>& out);

	// --- Instrument layouts -----------------------------------------------

	const LayoutRepository& layouts() const { return m_layouts; }
	void reloadLayouts();
	QString currentLayoutId() const { return m_currentLayoutId; }
	std::shared_ptr<const kf::layout::KeyboardLayout> currentLayout() const { return m_currentLayout; }
	//! Selects and persists a layout; unknown ids fall back to the default keyboard.
	//! AutoLayoutId is a piano fitted to the current clip's range.
	void selectLayout(const QString& id);
	static const QString AutoLayoutId;

	// --- Practised clip and transport -------------------------------------

	//! Instrument-track clips of the current song (Pattern editor clips are not supported yet)
	QVector<ClipChoice> listClips() const;
	MidiClip* currentClip() const { return m_clip; }
	void selectClip(MidiClip* clip);
	std::shared_ptr<const kf::Chart> chart() const { return m_chart; }
	const kf::ChartStats& chartStats() const { return m_chartStats; }

	TransportSample sampleTransport() const;

	//! Seconds of music visible above the hit line
	double lookaheadSeconds() const { return m_lookaheadSeconds; }
	void setLookaheadSeconds(double seconds);

signals:
	void layoutChanged();
	void layoutsReloaded();
	void chartChanged();
	//! Seek, loop wrap, start/stop: smoothing must restart from the host position
	void transportJumped();

private:
	void listenToTrack(Track* track);
	//! Called on the MIDI driver thread (or the audio thread for sustain-pedal releases)
	void trackNote(InstrumentTrack* track, const Note& note, bool on);
	void rebuildChart();
	void applyLayout(const QString& id, bool persist);

	Tolerance m_tolerance = Tolerance::Normal;
	double m_inputOffsetMs = 0;

	//! Several MIDI backends call back on different threads, so this is a
	//! multi-producer queue; a short mutex is fine because it is never the audio thread.
	std::mutex m_inputMutex;
	std::vector<kf::MidiInputEvent> m_inputQueue;
	//! Several tracks may hear the same keyboard: each key is reported once
	std::array<bool, 128> m_keyDown{};

	LayoutRepository m_layouts;
	QString m_currentLayoutId;
	std::shared_ptr<const kf::layout::KeyboardLayout> m_currentLayout;

	QPointer<MidiClip> m_clip;
	QVector<QMetaObject::Connection> m_clipConnections;
	std::shared_ptr<const kf::Chart> m_chart;
	kf::ChartStats m_chartStats;
	double m_lookaheadSeconds = 3.0;
};

} // namespace lmms

#endif // LMMS_KEYFALL_H
