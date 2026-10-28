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

#include <QTimer>
#include <vector>

#include "ToolPluginView.h"
#include "core/InputResolver.h"

class QComboBox;
class QLabel;
class QMenu;
class QToolButton;
class QWidget;

namespace lmms {
class KeyFall;
}

namespace lmms::gui {

class KeyboardWidget;
class MidiPortMenu;

//! Main window: toolbar on top, falling-notes area in the middle and the
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

private:
	QWidget* createToolbar();
	QMenu* createSettingsMenu();
	void updateKeyboardHeight();

	void populateLayoutCombo();
	void onLayoutChanged();
	void updateMidiInState();
	void pollInput();
	void showLastEvent(const kf::MidiInputEvent& event, bool insideLayout);

	KeyFall* m_plugin;
	QComboBox* m_clipCombo;
	QComboBox* m_layoutCombo;
	QToolButton* m_midiInButton;
	MidiPortMenu* m_midiInMenu;
	QLabel* m_lastEventLabel;
	QLabel* m_fallArea;
	KeyboardWidget* m_keyboard;

	QTimer m_inputTimer;
	kf::InputResolver m_resolver;
	std::vector<kf::MidiInputEvent> m_inputBuffer;
};

} // namespace lmms::gui

#endif // LMMS_GUI_KEYFALL_VIEW_H
