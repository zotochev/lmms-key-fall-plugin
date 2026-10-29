/*
 * KeyFallView.cpp - window of the KeyFall tool plugin
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

#include "KeyFallView.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>

#include "AudioEngine.h"
#include "CalibrationDialog.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "FallCanvas.h"
#include "KeyFall.h"
#include "KeyboardWidget.h"
#include "MidiPortMenu.h"
#include "SamplePlayHandle.h"

namespace lmms::gui {

namespace {

constexpr int FrameIntervalMs = 16; // ~60 Hz

// Auto-play (demo) performer: how human it plays
constexpr double AutoplayJitterMs = 25;  // timing spread (standard deviation)
constexpr double AutoplayWrongRate = 0.05;
constexpr double AutoplayMissRate = 0.05;
constexpr double AutoplayScheduleAheadSeconds = 0.5;
constexpr double AutoplayWaitReactionSeconds = 0.25; // in wait mode the demo "finds" each chord this fast

const auto ConfigSection = QStringLiteral("keyfall");

std::int64_t nowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

//! Clips can be added or moved at any time without a signal we could use,
//! so the list is rebuilt right before it opens.
class ClipCombo : public QComboBox
{
public:
	std::function<void()> beforePopup;

	void showPopup() override
	{
		if (beforePopup) { beforePopup(); }
		QComboBox::showPopup();
	}
};

} // namespace

KeyFallView::KeyFallView(KeyFall* plugin)
	: ToolPluginView(plugin)
	, m_plugin(plugin)
	, m_clipCombo(new ClipCombo())
	, m_layoutCombo(new QComboBox())
	, m_midiInButton(new QToolButton())
	, m_midiInMenu(new MidiPortMenu(MidiPort::Mode::Input))
	, m_lastEventLabel(new QLabel())
	, m_hudLabel(new QLabel())
	, m_keyboard(new KeyboardWidget())
{
	m_fall = new FallCanvas(m_keyboard);
	m_fall->setLookaheadSeconds(m_plugin->lookaheadSeconds());
	m_fall->onLookaheadStep = [this](int step) {
		m_plugin->setLookaheadSeconds(m_plugin->lookaheadSeconds() * (step > 0 ? 1.25 : 0.8));
		m_fall->setLookaheadSeconds(m_plugin->lookaheadSeconds());
	};
	m_fall->setToolTip(tr("Ctrl + mouse wheel: show more or less time ahead"));
	m_fall->setGrades(&m_scorer.grades());

	auto mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);
	mainLayout->addWidget(createToolbar());
	mainLayout->addWidget(createPracticeBar());
	mainLayout->addWidget(createStatusBar());
	mainLayout->addWidget(m_fall, 1);
	mainLayout->addWidget(m_keyboard);

	setMinimumSize(MinWidth, MinHeight);
	updateKeyboardHeight();

	// Model -> view connections live in modelChanged(): Plugin::createView() calls setModel()
	// after construction, and ModelView::setModel() disconnects the model from this widget.
	connect(&m_frameTimer, &QTimer::timeout, this, &KeyFallView::tick);
	m_frameTimer.setTimerType(Qt::PreciseTimer);
	m_frameTimer.setInterval(FrameIntervalMs);

	populateLayoutCombo();
	populateClipCombo();
	onLayoutChanged();
	onChartChanged();
	updateMidiInState();

	// MainWindow creates every tool view at startup and shows it from the Tools menu.
	hide();
	if (parentWidget())
	{
		parentWidget()->resize(DefaultWidth, DefaultHeight);
		parentWidget()->hide();
	}
}

QWidget* KeyFallView::createToolbar()
{
	auto toolbar = new QWidget();
	auto layout = new QHBoxLayout(toolbar);
	layout->setContentsMargins(6, 4, 6, 4);

	m_clipCombo->setToolTip(tr("Instrument-track clip to practise"));
	m_clipCombo->setMinimumContentsLength(24);
	m_clipCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	static_cast<ClipCombo*>(m_clipCombo)->beforePopup = [this] { populateClipCombo(); };
	connect(m_clipCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
		m_plugin->selectClip(index > 0 && index < m_clipItems.size() ? m_clipItems[index].data() : nullptr);
	});

	m_layoutCombo->setToolTip(tr("Your instrument: keyboard size, pads, ..."));
	// Long layout names must not squeeze the clip list: the popup still shows them in full
	m_layoutCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_layoutCombo->setMinimumContentsLength(16);
	connect(m_layoutCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
		m_plugin->selectLayout(m_layoutCombo->itemData(index).toString());
	});

	m_midiInMenu->setModel(m_plugin->midiPort());
	connect(m_midiInMenu, &QMenu::triggered, this, [this] {
		m_plugin->saveMidiInputs();
		updateMidiInState();
	});
	m_midiInButton->setMenu(m_midiInMenu);
	m_midiInButton->setPopupMode(QToolButton::InstantPopup);
	m_midiInButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

	auto settingsButton = new QToolButton();
	settingsButton->setText(QStringLiteral("⚙"));
	settingsButton->setToolTip(tr("Settings"));
	settingsButton->setPopupMode(QToolButton::InstantPopup);
	settingsButton->setMenu(createSettingsMenu());

	layout->addWidget(new QLabel(tr("Clip:")));
	layout->addWidget(m_clipCombo, 1);
	layout->addSpacing(8);
	layout->addWidget(m_layoutCombo);
	layout->addWidget(m_midiInButton);
	layout->addWidget(settingsButton);
	return toolbar;
}

QWidget* KeyFallView::createPracticeBar()
{
	auto bar = new QWidget();
	auto layout = new QHBoxLayout(bar);
	layout->setContentsMargins(6, 0, 6, 2);
	auto* config = ConfigManager::inst();
	const auto flag = [config](const QString& key, bool fallback) {
		return config->value(ConfigSection, key, fallback ? "1" : "0") == QLatin1String("1");
	};

	m_practiceButton = new QToolButton();
	m_practiceButton->setCheckable(true);
	m_practiceButton->setText(tr("▶ Practise"));
	m_practiceButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_practiceButton->setToolTip(tr("Play the clip on KeyFall's own clock: slower tempo, loop and wait mode.\n"
		"LMMS stays stopped; your keyboard still sounds through its track."));
	connect(m_practiceButton, &QToolButton::toggled, this, [this](bool on) { on ? startPractice() : stopPractice(); });

	m_waitCheck = new QCheckBox(tr("Wait for me"));
	m_waitCheck->setToolTip(tr("Time stops at each chord until you play all of its keys"));
	m_waitCheck->setChecked(flag("wait", false));

	m_tempoSpin = new QSpinBox();
	m_tempoSpin->setRange(25, 100);
	m_tempoSpin->setSingleStep(5);
	m_tempoSpin->setSuffix(QStringLiteral(" %"));
	m_tempoSpin->setPrefix(tr("Tempo "));
	m_tempoSpin->setToolTip(tr("Practice tempo relative to the song"));
	m_tempoSpin->setValue(config->value(ConfigSection, "practicetempo", "100").toInt());

	m_loopCheck = new QCheckBox(tr("Loop bars"));
	m_loopCheck->setToolTip(tr("Repeat a passage; each pass is scored on its own"));
	m_loopFrom = new QSpinBox();
	m_loopTo = new QSpinBox();
	for (auto* spin : {m_loopFrom, m_loopTo}) { spin->setRange(1, 999); }

	m_countInCheck = new QCheckBox(tr("Count-in"));
	m_countInCheck->setToolTip(tr("One bar of clicks before the music starts"));
	m_countInCheck->setChecked(flag("countin", true));
	m_clickCheck = new QCheckBox(tr("Click"));
	m_clickCheck->setToolTip(tr("Metronome click on every beat"));
	m_clickCheck->setChecked(flag("click", false));

	connect(m_waitCheck, &QCheckBox::toggled, this, [this](bool on) {
		ConfigManager::inst()->setValue(ConfigSection, "wait", on ? "1" : "0");
		m_restartPending = true; // a different kind of run
	});
	connect(m_tempoSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int value) {
		ConfigManager::inst()->setValue(ConfigSection, "practicetempo", QString::number(value));
	});
	connect(m_countInCheck, &QCheckBox::toggled, this, [](bool on) {
		ConfigManager::inst()->setValue(ConfigSection, "countin", on ? "1" : "0");
	});
	connect(m_clickCheck, &QCheckBox::toggled, this, [](bool on) {
		ConfigManager::inst()->setValue(ConfigSection, "click", on ? "1" : "0");
	});

	layout->addWidget(m_practiceButton);
	layout->addWidget(m_waitCheck);
	layout->addSpacing(8);
	layout->addWidget(m_tempoSpin);
	layout->addSpacing(8);
	layout->addWidget(m_loopCheck);
	layout->addWidget(m_loopFrom);
	layout->addWidget(new QLabel(QStringLiteral("–")));
	layout->addWidget(m_loopTo);
	layout->addSpacing(8);
	layout->addWidget(m_countInCheck);
	layout->addWidget(m_clickCheck);
	layout->addStretch(1);
	return bar;
}

QWidget* KeyFallView::createStatusBar()
{
	// A line of its own with a fixed height: the live score appearing or disappearing
	// must not resize the toolbar controls above it
	auto bar = new QWidget();
	auto layout = new QHBoxLayout(bar);
	layout->setContentsMargins(8, 0, 8, 3);

	m_hudLabel->setToolTip(tr("Accuracy so far · streak · average timing of the last hits"));
	m_hudLabel->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; }"));
	m_lastEventLabel->setToolTip(tr("Last MIDI message received: useful when writing your own layout file"));
	m_lastEventLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_lastEventLabel->setStyleSheet(QStringLiteral("QLabel { color: #8a8f98; }"));

	layout->addWidget(m_hudLabel);
	layout->addStretch(1);
	layout->addWidget(m_lastEventLabel);
	bar->setFixedHeight(bar->fontMetrics().height() + 6);
	return bar;
}

QMenu* KeyFallView::createSettingsMenu()
{
	auto menu = new QMenu(this);

	auto toleranceMenu = menu->addMenu(tr("Timing tolerance"));
	toleranceMenu->setToolTipsVisible(true);
	auto toleranceGroup = new QActionGroup(toleranceMenu);
	const struct
	{
		KeyFall::Tolerance value;
		QString text;
		QString hint;
	} options[] = {
		{KeyFall::Tolerance::Loose, tr("Loose"), tr("Forgiving timing, for learning a new piece")},
		{KeyFall::Tolerance::Normal, tr("Normal"), tr("Default timing windows")},
		{KeyFall::Tolerance::Strict, tr("Strict"), tr("Tight timing, for polishing rhythm")},
	};
	for (const auto& option : options)
	{
		auto action = toleranceMenu->addAction(option.text);
		action->setToolTip(option.hint);
		action->setCheckable(true);
		action->setChecked(m_plugin->tolerance() == option.value);
		toleranceGroup->addAction(action);
		const auto value = option.value;
		connect(action, &QAction::triggered, this, [this, value] { m_plugin->setTolerance(value); });
	}

	menu->addAction(tr("Calibrate latency…"), this, &KeyFallView::openCalibration);
	auto autoplay = menu->addAction(tr("Auto-play (demo)"));
	autoplay->setCheckable(true);
	autoplay->setToolTip(tr("KeyFall plays the clip itself, with small human-like mistakes"));
	connect(autoplay, &QAction::toggled, this, [this](bool on) {
		m_autoplay = on;
		m_autoplayQueue.clear();
		m_restartPending = true; // score the demo from a clean run
	});
	menu->setToolTipsVisible(true);

	menu->addSeparator();
	menu->addAction(tr("Open layouts folder"), this, [] {
		const QString dir = LayoutRepository::userDirectory();
		QDir().mkpath(dir);
		QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
	});
	menu->addAction(tr("Reload layouts"), this, [this] { m_plugin->reloadLayouts(); });
	return menu;
}

void KeyFallView::modelChanged()
{
	ToolPluginView::modelChanged();
	connect(m_plugin, &KeyFall::layoutChanged, this, &KeyFallView::onLayoutChanged, Qt::UniqueConnection);
	connect(m_plugin, &KeyFall::layoutsReloaded, this, &KeyFallView::populateLayoutCombo, Qt::UniqueConnection);
	connect(m_plugin, &KeyFall::chartChanged, this, &KeyFallView::onChartChanged, Qt::UniqueConnection);
	connect(m_plugin, &KeyFall::transportJumped, this, [this] {
		m_clock.reset();
		m_restartPending = true;
	});
}

void KeyFallView::populateLayoutCombo()
{
	m_layoutCombo->clear();
	m_layoutCombo->addItem(tr("Auto (fit to the clip)"), KeyFall::AutoLayoutId);
	m_layoutCombo->setItemData(0, tr("A piano keyboard covering the notes of the selected clip"), Qt::ToolTipRole);

	auto* model = qobject_cast<QStandardItemModel*>(m_layoutCombo->model());
	for (const auto& entry : m_plugin->layouts().entries())
	{
		QString text = entry.name;
		if (!entry.builtin) { text += entry.overridesBuiltin ? tr(" (yours, replaces built-in)") : tr(" (yours)"); }
		m_layoutCombo->addItem(text, entry.id);
		const int row = m_layoutCombo->count() - 1;
		QStringList tooltip{entry.path};
		tooltip << entry.messages;
		m_layoutCombo->setItemData(row, tooltip.join('\n'), Qt::ToolTipRole);
		if (!entry.isValid() && model)
		{
			// Broken files stay visible so their authors can see what is wrong
			model->item(row)->setEnabled(false);
			m_layoutCombo->setItemText(row, entry.name + tr(" (invalid)"));
		}
	}
	m_layoutCombo->setCurrentIndex(m_layoutCombo->findData(m_plugin->currentLayoutId()));
}

void KeyFallView::populateClipCombo()
{
	m_clipCombo->clear();
	m_clipItems.clear();
	m_clipCombo->addItem(tr("Choose a clip to practise…"));
	m_clipItems.push_back(nullptr);
	for (const auto& choice : m_plugin->listClips())
	{
		m_clipCombo->addItem(choice.label);
		m_clipItems.push_back(choice.clip);
		if (choice.clip == m_plugin->currentClip()) { m_clipCombo->setCurrentIndex(m_clipCombo->count() - 1); }
	}
	if (m_clipCombo->count() == 1) { m_clipCombo->setItemText(0, tr("No instrument clips in this project")); }
}

void KeyFallView::onLayoutChanged()
{
	m_resolver.setLayout(m_plugin->currentLayout().get());
	m_keyboard->setInstrumentLayout(m_plugin->currentLayout());
	m_fall->clearPresses(); // ghost bars refer to controls of the old layout
	m_expected.clear();
	const int index = m_layoutCombo->findData(m_plugin->currentLayoutId());
	if (index >= 0) { m_layoutCombo->setCurrentIndex(index); }
	m_fall->update();
}

void KeyFallView::onChartChanged()
{
	// A different or edited clip invalidates the current run
	m_scorer.setChart(m_plugin->chart());
	m_runActive = false;
	m_waitForRestart = false;
	m_recentDeltasMs.clear();
	m_fall->setSummary(std::nullopt);
	updateHud();
	m_fall->setChart(m_plugin->chart());
	m_fall->setGrades(&m_scorer.grades());
	if (m_practiceActive) { stopPractice(); }
	updateLoopRange();
	if (!m_plugin->currentClip()) { m_clipCombo->setCurrentIndex(0); }
	m_lastShownTick = -1e300;
	updateMessage();
}

void KeyFallView::updateMidiInState()
{
	const QStringList available = m_plugin->availableMidiInputs();
	const QStringList subscribed = m_plugin->subscribedMidiInputs();
	if (available.isEmpty())
	{
		m_midiInButton->setText(tr("MIDI in: no devices"));
		m_midiProblem = tr("No MIDI input device found. Connect a MIDI keyboard and choose a MIDI backend in "
			"Edit → Settings → MIDI, then restart LMMS. Without a keyboard you can use a virtual MIDI "
			"cable (e.g. loopMIDI) and scripts/virtual_player.py.");
	}
	else if (subscribed.isEmpty())
	{
		m_midiInButton->setText(tr("MIDI in: none selected"));
		m_midiProblem = tr("Choose your MIDI keyboard in the “MIDI in” menu above.");
	}
	else
	{
		m_midiInButton->setText(subscribed.size() == 1 ? tr("MIDI in: %1").arg(subscribed.front())
			: tr("MIDI in: %1 devices").arg(subscribed.size()));
		m_midiProblem.clear();
	}
	m_midiInButton->setToolTip(available.isEmpty() ? tr("No MIDI input devices found")
		: tr("Devices KeyFall listens to (it never changes what your tracks hear)"));
	updateMessage();
}

void KeyFallView::updateMessage()
{
	QString message;
	if (!m_plugin->currentClip())
	{
		message = tr("Choose a clip to practise in “Clip” above, then press Play in LMMS (Space).\n"
			"You can already play: pressed keys light up below.");
		if (!m_midiProblem.isEmpty()) { message += "\n\n" + m_midiProblem; }
	}
	else if (m_plugin->chart()->empty())
	{
		message = tr("This clip has no notes. Pick another one or draw some in the Piano Roll.");
	}
	else if (!m_supportedMode)
	{
		message = tr("Playback from the Pattern Editor is not supported yet.\n"
			"Play the song in the Song Editor, or the clip in the Piano Roll.");
	}
	else if (!m_midiProblem.isEmpty())
	{
		// The waterfall still works; the problem is shown but does not block it
		message.clear();
	}
	m_fall->setMessage(message);
}

void KeyFallView::tick()
{
	updateTransport();
	pollInput();
}

void KeyFallView::pollInput()
{
	m_plugin->drainInput(m_inputBuffer);
	if (m_autoplay)
	{
		std::sort(m_autoplayQueue.begin(), m_autoplayQueue.end(),
			[](const AutoplayEvent& a, const AutoplayEvent& b) { return a.timeNs < b.timeNs; });
		auto due = m_autoplayQueue.begin();
		for (; due != m_autoplayQueue.end() && due->timeNs <= m_frameNs; ++due)
		{
			m_inputBuffer.push_back({due->timeNs, static_cast<std::uint8_t>(due->on ? 0x90 : 0x80), 1, due->key,
				static_cast<std::uint8_t>(due->on ? 90 : 0)});
		}
		m_autoplayQueue.erase(m_autoplayQueue.begin(), due);
	}
	for (const auto& event : m_inputBuffer)
	{
		const auto press = m_resolver.resolve(event);
		auto feedback = KeyboardWidget::Feedback::Neutral;
		if (press && press->on && m_runActive && m_runIsWait)
		{
			feedback = m_wait.press(press->pitch) ? KeyboardWidget::Feedback::Correct : KeyboardWidget::Feedback::Wrong;
			updateHud();
		}
		else if (press && press->on && m_runActive)
		{
			// Where the press lands on the chart: back-date it from this frame, minus the calibrated delay
			const double tick = m_currentTick - (m_frameNs - press->timeNs) * 1e-9 * m_ticksPerSecond
				- m_plugin->inputOffsetMs() / 1000.0 * m_ticksPerSecond;
			const auto result = m_scorer.press(press->pitch, tick);
			if (result.kind == kf::PressResult::Kind::Hit && kf::isHit(result.grade))
			{
				feedback = KeyboardWidget::Feedback::Correct;
				m_recentDeltasMs.push_back(result.deltaMs);
				if (m_recentDeltasMs.size() > 8) { m_recentDeltasMs.erase(m_recentDeltasMs.begin()); }
			}
			else if (result.kind == kf::PressResult::Kind::Extra) { feedback = KeyboardWidget::Feedback::Wrong; }
			updateHud();
		}
		if (press && press->control)
		{
			m_keyboard->setPressed(*press->control, press->on ? std::max<std::uint8_t>(press->velocity, 1) : 0, feedback);
			m_fall->notePress(*press->control, press->on, press->timeNs);
		}
		showLastEvent(event, !press || press->control.has_value());
	}
}

void KeyFallView::updateTransport()
{
	const TransportSample s = m_plugin->sampleTransport();
	if (s.supportedMode != m_supportedMode)
	{
		m_supportedMode = s.supportedMode;
		updateMessage();
	}
	m_ticksPerBar = s.ticksPerBar;
	m_beatsPerBar = s.beatsPerBar;
	m_songTickOfChartZero = s.songTickOfChartZero;
	const double ticksPerBeat = static_cast<double>(s.ticksPerBar) / s.beatsPerBar;
	const std::int64_t now = nowNs();

	// Pressing Play in LMMS always wins over practice
	if (m_practiceActive && s.playing) { stopPractice(); }

	double tick = 0;
	double tps = s.ticksPerSecond;
	bool playing = false;
	if (m_practiceActive)
	{
		m_practice.setTempo(s.ticksPerSecond, m_tempoSpin->value() / 100.0);
		const auto loop = practiceLoop();
		m_practice.setLoop(loop);
		const std::optional<double> gate = m_runActive && m_runIsWait ? m_wait.gateTick() : std::nullopt;
		if (m_practice.update(now, gate)) { m_restartPending = true; } // next pass of the loop
		tick = m_practice.position();
		tps = m_practice.ticksPerSecond();
		playing = true;

		// Count-in clicks always, beat clicks when asked for; nothing while waiting at a chord
		const auto beat = static_cast<long long>(std::floor((s.songTickOfChartZero + tick) / ticksPerBeat + 1e-6));
		if (!m_clickArmed || beat != m_lastClickBeat)
		{
			const bool countIn = tick < 0;
			if (m_clickArmed && ((countIn && m_countInCheck->isChecked()) || m_clickCheck->isChecked()))
			{
				playClick(((beat % m_beatsPerBar) + m_beatsPerBar) % m_beatsPerBar == 0);
			}
			m_lastClickBeat = beat;
			m_clickArmed = true;
		}
		if (!loop && tick > m_plugin->chart()->length() + ticksPerBeat)
		{
			stopPractice(); // played to the end of the clip
			playing = false;
		}
	}
	else
	{
		tick = m_clock.update(s.chartTick, now, s.ticksPerSecond, s.playing);
		// The song position runs about one buffer ahead of what is audible
		if (s.playing) { tick -= s.latencyTicks; }
		playing = s.playing;
		// A loop wrap does not always arrive as a jump signal: going back in time ends the pass too
		if (m_runActive && tick < m_currentTick - 0.25 * tps) { m_restartPending = true; }
	}
	m_currentTick = tick;
	m_ticksPerSecond = tps;
	m_frameNs = now;

	const bool scoring = playing && (m_practiceActive || s.supportedMode) && m_plugin->currentClip()
		&& !m_plugin->chart()->empty();
	if (!scoring)
	{
		if (m_runActive) { finishRun(s); }
		m_waitForRestart = false;
	}
	else if (m_restartPending || (!m_runActive && !m_waitForRestart))
	{
		if (m_runActive) { finishRun(s); }
		startRun(tick, s);
	}
	m_restartPending = false;
	if (m_runActive)
	{
		if (m_autoplay) { scheduleAutoplay(tick); }
		if (m_runIsWait)
		{
			if (m_wait.finished())
			{
				finishRun(s);
				m_waitForRestart = true;
			}
		}
		else
		{
			m_scorer.setTempo(tps, ticksPerBeat);
			if (!m_scorer.advance(tick).empty()) { updateHud(); }
			if (m_scorer.finished() && tick > m_plugin->chart()->length())
			{
				finishRun(s);
				m_waitForRestart = true; // the song plays on past the clip: keep the result on screen
			}
		}
	}

	if (tick != m_lastShownTick || playing)
	{
		m_lastShownTick = tick;
		FallCanvas::Timeline t;
		t.now = tick;
		t.ticksPerSecond = tps;
		t.ticksPerBar = s.ticksPerBar;
		t.beatsPerBar = s.beatsPerBar;
		t.songTickOfChartZero = s.songTickOfChartZero;
		t.playing = playing;
		m_fall->setTimeline(t);

		// Keys whose notes are sounding at the hit line get the "play me" band
		const auto* layout = m_keyboard->instrumentLayout();
		const auto chart = m_plugin->chart();
		std::vector<bool> expected(layout ? layout->controls().size() : 0, false);
		if (layout && m_runActive && m_runIsWait)
		{
			// Wait mode: always show the chord that is being waited for
			for (const auto i : m_wait.currentNotes())
			{
				const auto c = layout->candidates(chart->notes()[i].key);
				if (!c.empty()) { expected[c.front()] = true; }
			}
		}
		else if (layout)
		{
			const auto& notes = chart->notes();
			for (std::size_t i = chart->firstVisible(tick); i < notes.size() && notes[i].start <= tick; ++i)
			{
				const auto c = layout->candidates(notes[i].key);
				if (notes[i].end() > tick && !c.empty()) { expected[c.front()] = true; }
			}
		}
		if (expected != m_expected)
		{
			m_expected = expected;
			m_keyboard->setExpected(m_expected);
		}
	}
	else if (m_fall->isAnimating(now))
	{
		m_fall->update(); // ghost bars move with the wall clock
	}
}

void KeyFallView::startRun(double tick, const TransportSample& s)
{
	switch (m_plugin->tolerance())
	{
		case KeyFall::Tolerance::Loose: m_scorer.setTolerance(kf::TolerancePreset::Loose); break;
		case KeyFall::Tolerance::Strict: m_scorer.setTolerance(kf::TolerancePreset::Strict); break;
		default: m_scorer.setTolerance(kf::TolerancePreset::Normal); break;
	}
	const double ticksPerBeat = static_cast<double>(s.ticksPerBar) / s.beatsPerBar;
	m_runIsWait = m_practiceActive && m_waitCheck->isChecked();
	if (m_runIsWait)
	{
		const auto loop = practiceLoop();
		m_wait.setChart(m_plugin->chart(), kf::Scorer::ChordMs / 1000.0 * m_ticksPerSecond);
		m_wait.reset(tick, loop ? loop->end : 1e300);
		m_fall->setGrades(&m_wait.grades());
	}
	else
	{
		// Windows follow the practice tempo: at half speed a beat is twice as long
		m_scorer.start(tick, m_ticksPerSecond, ticksPerBeat);
		m_fall->setGrades(&m_scorer.grades());
	}
	m_runActive = true;
	m_recentDeltasMs.clear();
	m_autoplayQueue.clear();
	const auto& notes = m_plugin->chart()->notes();
	m_autoplayNext = static_cast<std::size_t>(std::lower_bound(notes.begin(), notes.end(), tick,
		[](const kf::ChartNote& n, double t) { return n.start < t; }) - notes.begin());
	m_fall->setSummary(std::nullopt);
	updateHud();
}

void KeyFallView::finishRun(const TransportSample& s)
{
	m_scorer.stop();
	m_runActive = false;
	kf::ScoreSummary summary;
	if (m_runIsWait)
	{
		const auto w = m_wait.stats();
		summary.waitMode = true;
		summary.chords = summary.expected = w.chords;
		summary.chordsCompleted = w.completed;
		summary.cleanChords = w.clean;
		summary.wrongPresses = w.wrongPresses;
		summary.notesPercent = w.chords ? 100.0 * w.clean / w.chords : 0;
		summary.stars = w.completed < w.chords ? 0 : summary.notesPercent >= 90 ? 3 : summary.notesPercent >= 70 ? 2 : 1;
		// Nothing completed (e.g. stopped right away): keep the previous result
		if (w.completed == 0) { summary.expected = 0; }
	}
	else { summary = m_scorer.summary(s.ticksPerBar, s.songTickOfChartZero); }
	// A pass without a single judged note (e.g. stopped right away) leaves the last result in place
	if (summary.expected > 0) { m_fall->setSummary(summary); }
	updateHud();
}

void KeyFallView::updateHud()
{
	if (!m_runActive)
	{
		m_hudLabel->clear();
		return;
	}
	if (m_runIsWait)
	{
		const auto w = m_wait.stats();
		m_hudLabel->setText(tr("Chord %1 / %2  \u00b7  %n wrong key(s)", nullptr, w.wrongPresses)
			.arg(std::min(w.completed + 1, w.chords)).arg(w.chords));
		return;
	}
	QString text = tr("%1%").arg(qRound(m_scorer.liveAccuracy()));
	text += tr("  ·  streak %1").arg(m_scorer.streak());
	if (!m_recentDeltasMs.empty())
	{
		double mean = 0;
		for (const double d : m_recentDeltasMs) { mean += d; }
		mean /= m_recentDeltasMs.size();
		const int ms = qRound(std::abs(mean));
		text += ms < 10 ? tr("  ·  on time") : mean > 0 ? tr("  ·  %1 ms late").arg(ms) : tr("  ·  %1 ms early").arg(ms);
	}
	m_hudLabel->setText(text);
}

void KeyFallView::scheduleAutoplay(double tick)
{
	const auto& notes = m_plugin->chart()->notes();
	if (m_runIsWait)
	{
		const bool pending = std::any_of(m_autoplayQueue.begin(), m_autoplayQueue.end(), [](const AutoplayEvent& e) { return e.on; });
		if (!m_practice.holding() || pending) { return; }
		std::uniform_real_distribution<double> chance(0.0, 1.0);
		const auto onNs = m_frameNs + static_cast<std::int64_t>(AutoplayWaitReactionSeconds * 1e9);
		for (const auto i : m_wait.currentNotes())
		{
			int key = notes[i].key;
			if (chance(m_rng) < AutoplayWrongRate) { key += 2; } // a wrong key first; the right one comes next time
			m_autoplayQueue.push_back({onNs, static_cast<std::uint8_t>(std::clamp(key, 0, 127)), true});
			m_autoplayQueue.push_back({onNs + 300'000'000, static_cast<std::uint8_t>(std::clamp(key, 0, 127)), false});
		}
		return;
	}
	const double tps = m_ticksPerSecond;
	std::normal_distribution<double> jitter(0.0, AutoplayJitterMs);
	std::uniform_real_distribution<double> chance(0.0, 1.0);
	// Presses are timestamped like real ones, including the calibrated input delay
	const double offsetNs = m_plugin->inputOffsetMs() * 1e6;
	while (m_autoplayNext < notes.size() && notes[m_autoplayNext].start < tick + AutoplayScheduleAheadSeconds * tps)
	{
		const auto& n = notes[m_autoplayNext++];
		if (chance(m_rng) < AutoplayMissRate) { continue; }
		int key = n.key;
		if (chance(m_rng) < AutoplayWrongRate) { key += chance(m_rng) < 0.5 ? -2 : 2; }
		const auto onNs = m_frameNs + static_cast<std::int64_t>((n.start - tick) / tps * 1e9 + jitter(m_rng) * 1e6 + offsetNs);
		const auto offNs = onNs + static_cast<std::int64_t>(n.length / tps * 1e9 * 0.9);
		m_autoplayQueue.push_back({onNs, static_cast<std::uint8_t>(std::clamp(key, 0, 127)), true});
		m_autoplayQueue.push_back({offNs, static_cast<std::uint8_t>(std::clamp(key, 0, 127)), false});
	}
}

void KeyFallView::startPractice()
{
	const TransportSample s = m_plugin->sampleTransport();
	if (!m_plugin->currentClip() || m_plugin->chart()->empty() || s.playing)
	{
		// Nothing to practise, or LMMS is playing: undo the button press
		const QSignalBlocker block(m_practiceButton);
		m_practiceButton->setChecked(false);
		return;
	}
	m_practice.setTempo(s.ticksPerSecond, m_tempoSpin->value() / 100.0);
	const auto loop = practiceLoop();
	m_practice.setLoop(loop);
	double from = loop ? loop->begin : 0;
	if (m_countInCheck->isChecked()) { from -= s.ticksPerBar; }
	m_practice.start(from, nowNs());
	m_practiceActive = true;
	m_restartPending = true;
	m_clickArmed = false;
	m_autoplayQueue.clear();
	m_practiceButton->setText(tr("■ Stop"));
}

void KeyFallView::stopPractice()
{
	m_practice.stop();
	m_practiceActive = false;
	const QSignalBlocker block(m_practiceButton);
	m_practiceButton->setChecked(false);
	m_practiceButton->setText(tr("▶ Practise"));
	m_clock.reset();
	m_keyboard->releaseAll();
}

std::optional<kf::PracticeClock::Loop> KeyFallView::practiceLoop() const
{
	if (!m_loopCheck->isChecked()) { return std::nullopt; }
	// Spin boxes hold song bar numbers; the loop covers whole bars from..to
	const double begin = (m_loopFrom->value() - 1.0) * m_ticksPerBar - m_songTickOfChartZero;
	const double end = static_cast<double>(m_loopTo->value()) * m_ticksPerBar - m_songTickOfChartZero;
	if (end <= begin) { return std::nullopt; }
	return kf::PracticeClock::Loop{begin, end};
}

void KeyFallView::updateLoopRange()
{
	const TransportSample s = m_plugin->sampleTransport();
	m_ticksPerBar = s.ticksPerBar;
	m_songTickOfChartZero = s.songTickOfChartZero;
	const int tpb = std::max(1, s.ticksPerBar);
	const int firstBar = static_cast<int>(std::floor(s.songTickOfChartZero / tpb)) + 1;
	const int lastBar = std::max(firstBar,
		static_cast<int>(std::ceil((s.songTickOfChartZero + m_plugin->chart()->length()) / tpb)));
	for (auto* spin : {m_loopFrom, m_loopTo})
	{
		const QSignalBlocker block(spin);
		spin->setRange(firstBar, lastBar);
	}
	m_loopFrom->setValue(firstBar);
	m_loopTo->setValue(lastBar);
}

void KeyFallView::playClick(bool downbeat)
{
	// The same samples and mechanism as LMMS's Tap Tempo tool
	Engine::audioEngine()->addPlayHandle(new SamplePlayHandle(downbeat ? "misc/metronome02.ogg" : "misc/metronome01.ogg"));
}

void KeyFallView::openCalibration()
{
	// The dialog reads MIDI input itself; the window's frame loop pauses meanwhile
	m_frameTimer.stop();
	CalibrationDialog dialog(m_plugin, this);
	dialog.exec();
	m_plugin->drainInput(m_inputBuffer);
	m_keyboard->releaseAll();
	if (isVisible()) { m_frameTimer.start(); }
}

void KeyFallView::showLastEvent(const kf::MidiInputEvent& event, bool insideLayout)
{
	QString text;
	if (event.status == 0xB0) { text = tr("CC %1 = %2").arg(event.data1).arg(event.data2); }
	else
	{
		const bool on = event.status == 0x90 && event.data2 > 0;
		text = QStringLiteral("%1 %2").arg(QString::fromStdString(kf::layout::noteName(event.data1)),
			on ? tr("vel %1").arg(event.data2) : tr("off"));
	}
	text += tr("  ch %1").arg(event.channel);
	if (!insideLayout) { text += tr("  (outside layout)"); }
	m_lastEventLabel->setText(text);
}

void KeyFallView::resizeEvent(QResizeEvent* event)
{
	ToolPluginView::resizeEvent(event);
	updateKeyboardHeight();
}

void KeyFallView::showEvent(QShowEvent* event)
{
	ToolPluginView::showEvent(event);
	m_plugin->drainInput(m_inputBuffer); // ignore whatever was played while hidden
	m_keyboard->releaseAll();
	m_fall->clearPresses();
	populateClipCombo();
	updateMidiInState();
	m_clock.reset();
	m_frameTimer.start();
}

void KeyFallView::hideEvent(QHideEvent* event)
{
	m_frameTimer.stop();
	ToolPluginView::hideEvent(event);
}

void KeyFallView::updateKeyboardHeight()
{
	// Keyboard takes 18% of the window height, clamped to 70..140 px
	m_keyboard->setFixedHeight(std::clamp(height() * 18 / 100, 70, 140));
}

} // namespace lmms::gui
