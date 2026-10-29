/*
 * KeyFallView.h - window of the KeyFall tool plugin
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

#ifndef LMMS_GUI_KEYFALL_VIEW_H
#define LMMS_GUI_KEYFALL_VIEW_H

#include <QPointer>
#include <QTimer>
#include <QVector>
#include <optional>
#include <random>
#include <vector>

#include "ToolPluginView.h"
#include "core/InputResolver.h"
#include "core/PlayheadClock.h"
#include "core/Practice.h"
#include "core/Scorer.h"

class QCheckBox;
class QComboBox;
class QSpinBox;
class QLabel;
class QMenu;
class QToolButton;
class QWidget;

namespace lmms {
class KeyFall;
class MidiClip;
struct TransportSample;
}

namespace lmms::gui {

class FallCanvas;
class KeyboardWidget;
class MidiPortMenu;

//! Main window: toolbar on top, falling notes in the middle and the
//! instrument (keyboard, pads, ...) at the bottom.
class KeyFallView : public ToolPluginView
{
	Q_OBJECT
public:
	explicit KeyFallView(KeyFall* plugin);

	static constexpr int MinWidth = 480;
	static constexpr int MinHeight = 320;
	static constexpr int DefaultWidth = 900;
	static constexpr int DefaultHeight = 560;

protected:
	void resizeEvent(QResizeEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;
	void modelChanged() override;

private:
	QWidget* createToolbar();
	QWidget* createStatusBar();
	QWidget* createPracticeBar();
	QMenu* createSettingsMenu();
	void updateKeyboardHeight();

	void populateLayoutCombo();
	void populateClipCombo();
	void onLayoutChanged();
	void onChartChanged();
	void updateMidiInState();
	void updateMessage();

	//! Frame tick (~60 Hz while visible): MIDI input, transport, animation
	void tick();
	void pollInput();
	void updateTransport();
	void showLastEvent(const kf::MidiInputEvent& event, bool insideLayout);

	// Scoring runs: one run per pass from Play (or a seek / loop wrap) to Stop or the clip end
	void startRun(double tick, const TransportSample& s);
	void finishRun(const TransportSample& s);
	void updateHud();
	void openCalibration();
	//! Demo / self-test: KeyFall plays the clip itself with human-like mistakes
	void scheduleAutoplay(double tick);

	// Practice: KeyFall's own clock (tempo %, loop, count-in, wait mode); LMMS stays stopped
	void startPractice();
	void stopPractice();
	std::optional<kf::PracticeClock::Loop> practiceLoop() const;
	void updateLoopRange();
	void playClick(bool downbeat);
	void practiseBar(int songBar);

	KeyFall* m_plugin;
	QComboBox* m_clipCombo;
	QComboBox* m_layoutCombo;
	QToolButton* m_midiInButton;
	MidiPortMenu* m_midiInMenu;
	QLabel* m_lastEventLabel;
	QLabel* m_hudLabel;
	FallCanvas* m_fall;
	KeyboardWidget* m_keyboard;

	QTimer m_frameTimer;
	kf::InputResolver m_resolver;
	std::vector<kf::MidiInputEvent> m_inputBuffer;
	kf::PlayheadClock m_clock;
	QVector<QPointer<MidiClip>> m_clipItems; //!< clip per combo row (row 0 = "choose a clip")
	QString m_midiProblem;                   //!< empty when a MIDI input is selected
	bool m_supportedMode = true;
	double m_lastShownTick = -1e300;
	std::vector<bool> m_expected;

	kf::Scorer m_scorer;
	bool m_runActive = false;
	bool m_restartPending = false;   //!< transport jumped: the current pass is over
	bool m_waitForRestart = false;   //!< the run reached the clip end while the song keeps playing
	double m_currentTick = 0;        //!< audible chart position at m_frameNs
	double m_ticksPerSecond = 96;
	std::int64_t m_frameNs = 0;
	std::vector<double> m_recentDeltasMs; //!< last hits, for the live early/late readout

	struct AutoplayEvent
	{
		std::int64_t timeNs;
		std::uint8_t key;
		bool on;
	};
	bool m_autoplay = false;
	std::size_t m_autoplayNext = 0; //!< next chart note to schedule
	std::vector<AutoplayEvent> m_autoplayQueue;
	std::mt19937 m_rng{12345};

	kf::PracticeClock m_practice;
	kf::WaitGate m_wait;
	bool m_practiceActive = false;
	bool m_runIsWait = false;
	QToolButton* m_practiceButton = nullptr;
	QCheckBox* m_waitCheck = nullptr;
	QCheckBox* m_anyNoteCheck = nullptr;
	QCheckBox* m_autoTempoCheck = nullptr;
	kf::TempoCoach m_tempoCoach;
	QString m_hudNote; //!< e.g. "tempo 80 % -> 85 %" after a loop pass
	QSpinBox* m_tempoSpin = nullptr;
	QCheckBox* m_loopCheck = nullptr;
	QSpinBox* m_loopFrom = nullptr;
	QSpinBox* m_loopTo = nullptr;
	QCheckBox* m_countInCheck = nullptr;
	QCheckBox* m_clickCheck = nullptr;
	long long m_lastClickBeat = 0;
	bool m_clickArmed = false;
	// Song geometry of the last frame (bar grid for loops, clicks and summaries)
	int m_ticksPerBar = 192;
	int m_beatsPerBar = 4;
	double m_songTickOfChartZero = 0;
};

} // namespace lmms::gui

#endif // LMMS_GUI_KEYFALL_VIEW_H
