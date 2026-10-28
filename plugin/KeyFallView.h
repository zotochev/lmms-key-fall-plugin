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

#include "ToolPluginView.h"

class QComboBox;
class QLabel;
class QWidget;

namespace lmms {
class KeyFall;
}

namespace lmms::gui {

//! Main window: toolbar on top, falling-notes area in the middle and the
//! keyboard at the bottom. In this stage both areas are placeholders.
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

private:
	QWidget* createToolbar();
	void updateKeyboardHeight();

	KeyFall* m_plugin;
	QComboBox* m_clipCombo;
	QLabel* m_midiInLabel;
	QComboBox* m_difficultyCombo;
	QWidget* m_fallArea;
	QWidget* m_keyboardArea;
};

} // namespace lmms::gui

#endif // LMMS_GUI_KEYFALL_VIEW_H
