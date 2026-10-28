/*
 * KeyboardWidget.h - draws a KeyFall instrument layout and highlights pressed controls
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

#ifndef LMMS_GUI_KEYFALL_KEYBOARD_WIDGET_H
#define LMMS_GUI_KEYFALL_KEYBOARD_WIDGET_H

#include <QPixmap>
#include <QWidget>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/KeyboardLayout.h"

namespace lmms::gui {

//! Instrument view: renders any layout (piano keys, pads, ...) from its controls'
//! geometry, so other views can ask controlRect() instead of assuming pitch == x.
//! The static picture is cached in a pixmap; each repaint only adds highlights.
class KeyboardWidget : public QWidget
{
public:
	explicit KeyboardWidget(QWidget* parent = nullptr);

	void setInstrumentLayout(std::shared_ptr<const kf::layout::KeyboardLayout> layout);
	const kf::layout::KeyboardLayout* instrumentLayout() const { return m_layout.get(); }

	//! How a press was judged; decides its colour (and a hatch for wrong notes)
	enum class Feedback : std::uint8_t
	{
		Neutral, //!< no run active
		Correct,
		Wrong
	};

	//! Velocity 0 releases the control
	void setPressed(std::uint16_t control, std::uint8_t velocity, Feedback feedback = Feedback::Neutral);
	void releaseAll();

	//! Controls whose chart notes are sounding now get a yellow "play me" band
	void setExpected(const std::vector<bool>& expected);

	//! Control geometry in widget coordinates
	QRectF controlRect(std::uint16_t control) const;

protected:
	void paintEvent(QPaintEvent*) override;
	void resizeEvent(QResizeEvent*) override;

private:
	void updateMapping();
	void rebuildCache();
	void paintControl(QPainter& p, const kf::layout::Control& control, bool pressed) const;

	std::shared_ptr<const kf::layout::KeyboardLayout> m_layout;
	std::vector<std::uint8_t> m_pressed; //!< velocity per control, 0 = up
	std::vector<Feedback> m_feedback;
	std::vector<bool> m_expected;
	QPixmap m_cache;
	bool m_cacheValid = false;
	// layout units -> widget pixels
	double m_scaleX = 1, m_scaleY = 1, m_offsetX = 0, m_offsetY = 0;
};

} // namespace lmms::gui

#endif // LMMS_GUI_KEYFALL_KEYBOARD_WIDGET_H
