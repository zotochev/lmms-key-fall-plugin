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
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

#include "KeyFall.h"

namespace lmms::gui {

namespace {

// Dark background used by the falling-notes and keyboard areas (see PLAN.md, UI/UX)
const QColor BackgroundColor{0x1e, 0x22, 0x26};
const QColor HintColor{0x8a, 0x8f, 0x98};

//! Temporary area that only shows what will be drawn there in later stages.
class PlaceholderArea : public QWidget
{
public:
	PlaceholderArea(const QString& hint, QWidget* parent = nullptr)
		: QWidget(parent)
		, m_hint(hint)
	{
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.fillRect(rect(), BackgroundColor);
		p.setPen(HintColor);
		p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_hint);
	}

private:
	QString m_hint;
};

} // namespace

KeyFallView::KeyFallView(KeyFall* plugin)
	: ToolPluginView(plugin)
	, m_plugin(plugin)
	, m_clipCombo(new QComboBox())
	, m_midiInLabel(new QLabel())
	, m_fallArea(new PlaceholderArea(tr("Falling notes of the selected clip will appear here")))
	, m_keyboardArea(new PlaceholderArea(tr("Keyboard")))
{
	auto mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);
	mainLayout->addWidget(createToolbar());
	mainLayout->addWidget(m_fallArea, 1);
	mainLayout->addWidget(m_keyboardArea);

	setMinimumSize(MinWidth, MinHeight);
	updateKeyboardHeight();

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

	const QStringList inputs = m_plugin->availableMidiInputs();
	m_midiInLabel->setText(tr("MIDI in: %n device(s) available", nullptr, inputs.size()));
	m_midiInLabel->setToolTip(inputs.isEmpty() ? tr("No MIDI input devices found") : inputs.join('\n'));

	auto settingsButton = new QToolButton();
	settingsButton->setText(QStringLiteral("⚙"));
	settingsButton->setToolTip(tr("Settings"));
	settingsButton->setPopupMode(QToolButton::InstantPopup);
	settingsButton->setMenu(createSettingsMenu());

	layout->addWidget(new QLabel(tr("Clip:")));
	layout->addWidget(m_clipCombo, 1);
	layout->addSpacing(12);
	layout->addWidget(m_midiInLabel);
	layout->addSpacing(12);
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
	return menu;
}

void KeyFallView::resizeEvent(QResizeEvent* event)
{
	ToolPluginView::resizeEvent(event);
	updateKeyboardHeight();
}

void KeyFallView::updateKeyboardHeight()
{
	// Keyboard takes 18% of the window height, clamped to 70..140 px
	m_keyboardArea->setFixedHeight(std::clamp(height() * 18 / 100, 70, 140));
}

} // namespace lmms::gui
