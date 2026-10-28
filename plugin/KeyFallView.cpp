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
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

#include "KeyFall.h"
#include "KeyboardWidget.h"
#include "MidiPortMenu.h"

namespace lmms::gui {

namespace {

constexpr int InputPollIntervalMs = 16; // ~60 Hz, also the future animation tick

const QString FallAreaStyle = QStringLiteral("QLabel { background-color: #1e2226; color: #8a8f98; padding: 24px; }");

} // namespace

KeyFallView::KeyFallView(KeyFall* plugin)
	: ToolPluginView(plugin)
	, m_plugin(plugin)
	, m_clipCombo(new QComboBox())
	, m_layoutCombo(new QComboBox())
	, m_midiInButton(new QToolButton())
	, m_midiInMenu(new MidiPortMenu(MidiPort::Mode::Input))
	, m_lastEventLabel(new QLabel())
	, m_fallArea(new QLabel())
	, m_keyboard(new KeyboardWidget())
{
	m_fallArea->setStyleSheet(FallAreaStyle);
	m_fallArea->setAlignment(Qt::AlignCenter);
	m_fallArea->setWordWrap(true);

	auto mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);
	mainLayout->addWidget(createToolbar());
	mainLayout->addWidget(m_fallArea, 1);
	mainLayout->addWidget(m_keyboard);

	setMinimumSize(MinWidth, MinHeight);
	updateKeyboardHeight();

	connect(m_plugin, &KeyFall::layoutChanged, this, &KeyFallView::onLayoutChanged);
	connect(m_plugin, &KeyFall::layoutsReloaded, this, &KeyFallView::populateLayoutCombo);
	connect(&m_inputTimer, &QTimer::timeout, this, &KeyFallView::pollInput);
	m_inputTimer.setTimerType(Qt::PreciseTimer);
	m_inputTimer.setInterval(InputPollIntervalMs);

	populateLayoutCombo();
	onLayoutChanged();
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

	m_clipCombo->addItem(tr("No clip selected"));
	m_clipCombo->setEnabled(false);
	m_clipCombo->setToolTip(tr("MIDI clip to practise"));

	m_layoutCombo->setToolTip(tr("Your instrument: keyboard size, pads, ..."));
	m_layoutCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
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

	m_lastEventLabel->setMinimumWidth(m_lastEventLabel->fontMetrics().horizontalAdvance(QStringLiteral("CC 127 = 127  ch 16")));
	m_lastEventLabel->setToolTip(tr("Last MIDI message received: useful when writing your own layout file"));

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
	layout->addSpacing(8);
	layout->addWidget(m_lastEventLabel);
	layout->addWidget(settingsButton);
	return toolbar;
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

	menu->addSeparator();
	menu->addAction(tr("Open layouts folder"), this, [] {
		const QString dir = LayoutRepository::userDirectory();
		QDir().mkpath(dir);
		QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
	});
	menu->addAction(tr("Reload layouts"), this, [this] { m_plugin->reloadLayouts(); });
	return menu;
}

void KeyFallView::populateLayoutCombo()
{
	m_layoutCombo->clear();
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

void KeyFallView::onLayoutChanged()
{
	m_resolver.setLayout(m_plugin->currentLayout().get());
	m_keyboard->setInstrumentLayout(m_plugin->currentLayout());
	const int index = m_layoutCombo->findData(m_plugin->currentLayoutId());
	if (index >= 0) { m_layoutCombo->setCurrentIndex(index); }
}

void KeyFallView::updateMidiInState()
{
	const QStringList available = m_plugin->availableMidiInputs();
	const QStringList subscribed = m_plugin->subscribedMidiInputs();
	if (available.isEmpty())
	{
		m_midiInButton->setText(tr("MIDI in: no devices"));
		m_fallArea->setText(tr("No MIDI input device found.\n\nConnect a MIDI keyboard and choose a MIDI backend in "
			"Edit → Settings → MIDI, then restart LMMS.\nWithout a keyboard you can use a virtual MIDI "
			"cable (e.g. loopMIDI) and scripts/virtual_player.py."));
	}
	else if (subscribed.isEmpty())
	{
		m_midiInButton->setText(tr("MIDI in: none selected"));
		m_fallArea->setText(tr("Choose your MIDI keyboard in the “MIDI in” menu above."));
	}
	else
	{
		m_midiInButton->setText(subscribed.size() == 1 ? tr("MIDI in: %1").arg(subscribed.front())
			: tr("MIDI in: %1 devices").arg(subscribed.size()));
		m_fallArea->setText(tr("Play on your keyboard: pressed keys light up below.\n"
			"Falling notes of the selected clip will appear here."));
	}
	m_midiInButton->setToolTip(available.isEmpty() ? tr("No MIDI input devices found")
		: tr("Devices KeyFall listens to (it never changes what your tracks hear)"));
}

void KeyFallView::pollInput()
{
	m_plugin->drainInput(m_inputBuffer);
	for (const auto& event : m_inputBuffer)
	{
		const auto press = m_resolver.resolve(event);
		if (press && press->control) { m_keyboard->setPressed(*press->control, press->on ? std::max<std::uint8_t>(press->velocity, 1) : 0); }
		showLastEvent(event, !press || press->control.has_value());
	}
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
	updateMidiInState();
	m_inputTimer.start();
}

void KeyFallView::hideEvent(QHideEvent* event)
{
	m_inputTimer.stop();
	ToolPluginView::hideEvent(event);
}

void KeyFallView::updateKeyboardHeight()
{
	// Keyboard takes 18% of the window height, clamped to 70..140 px
	m_keyboard->setFixedHeight(std::clamp(height() * 18 / 100, 70, 140));
}

} // namespace lmms::gui
