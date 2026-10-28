/*
 * KeyboardWidget.cpp - draws a KeyFall instrument layout and highlights pressed controls
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

#include "KeyboardWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace lmms::gui {

using kf::layout::Control;
using kf::layout::Shape;

namespace {

// Palette from PLAN.md (UI/UX), checked against the LMMS dark theme
const QColor Background{0x1e, 0x22, 0x26};
const QColor WhiteKey{0xe6, 0xe8, 0xeb};
const QColor WhiteKeyLabel{0x6b, 0x70, 0x78};
const QColor BlackKey{0x2a, 0x2f, 0x35};
const QColor KeyBorder{0x8a, 0x8f, 0x98};
const QColor PadFill{0x3a, 0x40, 0x48};
const QColor PadLabel{0xc0, 0xc4, 0xca};
const QColor Pressed{0x56, 0xb4, 0xe9};
const QColor ExpectedBand{0xf0, 0xe4, 0x42, 220};

constexpr int Margin = 2;
constexpr double MinLabelWidth = 14.0; // px; narrower keys get no label

bool isPianoShape(Shape s) { return s == Shape::PianoWhite || s == Shape::PianoBlack; }

} // namespace

KeyboardWidget::KeyboardWidget(QWidget* parent)
	: QWidget(parent)
{
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void KeyboardWidget::setInstrumentLayout(std::shared_ptr<const kf::layout::KeyboardLayout> layout)
{
	m_layout = std::move(layout);
	m_pressed.assign(m_layout ? m_layout->controls().size() : 0, 0);
	m_expected.assign(m_pressed.size(), false);
	updateMapping();
	m_cacheValid = false;
	update();
}

void KeyboardWidget::setPressed(std::uint16_t control, std::uint8_t velocity)
{
	if (control >= m_pressed.size() || m_pressed[control] == velocity) { return; }
	m_pressed[control] = velocity;
	update();
}

void KeyboardWidget::releaseAll()
{
	std::fill(m_pressed.begin(), m_pressed.end(), 0);
	update();
}

void KeyboardWidget::setExpected(const std::vector<bool>& expected)
{
	if (expected == m_expected) { return; }
	m_expected = expected;
	m_expected.resize(m_pressed.size(), false);
	update();
}

QRectF KeyboardWidget::controlRect(std::uint16_t control) const
{
	if (!m_layout || control >= m_layout->controls().size()) { return {}; }
	const auto& r = m_layout->controls()[control].rect;
	return {m_offsetX + r.x * m_scaleX, m_offsetY + r.y * m_scaleY, r.w * m_scaleX, r.h * m_scaleY};
}

void KeyboardWidget::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	updateMapping();
	m_cacheValid = false;
}

void KeyboardWidget::updateMapping()
{
	if (!m_layout || m_layout->controls().empty()) { return; }
	const auto b = m_layout->bounds();
	const double availW = std::max(1, width() - 2 * Margin);
	const double availH = std::max(1, height() - 2 * Margin);
	const bool pianoOnly = std::all_of(m_layout->controls().begin(), m_layout->controls().end(),
		[](const Control& c) { return isPianoShape(c.shape); });
	if (pianoOnly)
	{
		// Keys stretch to fill the strip: the keyboard height is set by the window, not the key ratio
		m_scaleX = availW / b.w;
		m_scaleY = availH / b.h;
	}
	else
	{
		// Pads and buttons keep their proportions and are centred
		m_scaleX = m_scaleY = std::min(availW / b.w, availH / b.h);
	}
	m_offsetX = Margin + (availW - b.w * m_scaleX) / 2 - b.x * m_scaleX;
	m_offsetY = Margin + (availH - b.h * m_scaleY) / 2 - b.y * m_scaleY;
}

void KeyboardWidget::paintControl(QPainter& p, const Control& control, bool pressed) const
{
	const QRectF r = controlRect(static_cast<std::uint16_t>(&control - m_layout->controls().data()));
	const double radius = std::min(4.0, r.width() / 3);
	switch (control.shape)
	{
		case Shape::PianoWhite:
		{
			p.setPen(QPen(KeyBorder, 1));
			p.setBrush(pressed ? Pressed : WhiteKey);
			QPainterPath path;
			path.addRoundedRect(r.adjusted(0.5, -radius, -0.5, -0.5), radius, radius);
			p.drawPath(path.intersected([&] { QPainterPath clip; clip.addRect(r); return clip; }()));
			if (!pressed && control.pitch % 12 == 0 && r.width() >= MinLabelWidth)
			{
				p.setPen(WhiteKeyLabel);
				p.drawText(r.adjusted(0, 0, 0, -3), Qt::AlignHCenter | Qt::AlignBottom,
					QString::fromStdString(kf::layout::noteName(control.pitch)));
			}
			break;
		}
		case Shape::PianoBlack:
		{
			p.setPen(QPen(Qt::black, 1));
			p.setBrush(pressed ? Pressed.darker(115) : BlackKey);
			p.drawRoundedRect(r.adjusted(0, -radius, 0, 0), radius, radius);
			break;
		}
		default:
		{
			p.setPen(QPen(KeyBorder, 1));
			p.setBrush(pressed ? Pressed : PadFill);
			if (control.shape == Shape::Circle) { p.drawEllipse(r); }
			else if (control.shape == Shape::RoundRect) { p.drawRoundedRect(r, radius, radius); }
			else { p.drawRect(r); }
			if (r.width() >= 2 * MinLabelWidth)
			{
				p.setPen(pressed ? Background : PadLabel);
				p.drawText(r, Qt::AlignCenter, QString::fromStdString(kf::layout::noteName(control.pitch)));
			}
			break;
		}
	}
}

void KeyboardWidget::rebuildCache()
{
	const qreal dpr = devicePixelRatioF();
	m_cache = QPixmap(size() * dpr);
	m_cache.setDevicePixelRatio(dpr);
	m_cache.fill(Background);
	if (m_layout)
	{
		QPainter p(&m_cache);
		p.setRenderHint(QPainter::Antialiasing);
		QFont f = font();
		f.setPixelSize(std::clamp(height() / 9, 8, 11));
		p.setFont(f);
		// White keys first, everything that lies on top of them afterwards
		for (const auto& c : m_layout->controls()) { if (c.shape == Shape::PianoWhite) { paintControl(p, c, false); } }
		for (const auto& c : m_layout->controls()) { if (c.shape != Shape::PianoWhite) { paintControl(p, c, false); } }
	}
	m_cacheValid = true;
}

void KeyboardWidget::paintEvent(QPaintEvent*)
{
	if (!m_cacheValid) { rebuildCache(); }
	QPainter p(this);
	p.drawPixmap(0, 0, m_cache);
	if (!m_layout) { return; }

	p.setRenderHint(QPainter::Antialiasing);
	QFont f = font();
	f.setPixelSize(std::clamp(height() / 9, 8, 11));
	p.setFont(f);
	const auto& controls = m_layout->controls();

	const auto expected = [&](std::size_t i) { return i < m_expected.size() && m_expected[i]; };
	const auto paintState = [&](std::size_t i) {
		paintControl(p, controls[i], m_pressed[i] != 0);
		if (expected(i) && !m_pressed[i])
		{
			// "Play me": a band on the visible lower part of the key
			const QRectF r = controlRect(static_cast<std::uint16_t>(i));
			const QRectF band(r.left() + 1, r.bottom() - r.height() * 0.28, r.width() - 2, r.height() * 0.28 - 1);
			p.fillRect(band, ExpectedBand);
		}
	};

	// A highlighted white key would cover its black neighbours, so those are repainted on top
	std::vector<QRectF> changedWhites;
	for (std::size_t i = 0; i < controls.size(); ++i)
	{
		if ((m_pressed[i] || expected(i)) && controls[i].shape == Shape::PianoWhite)
		{
			paintState(i);
			changedWhites.push_back(controlRect(static_cast<std::uint16_t>(i)));
		}
	}
	for (std::size_t i = 0; i < controls.size(); ++i)
	{
		const auto& c = controls[i];
		if (c.shape == Shape::PianoWhite) { continue; }
		const QRectF r = controlRect(static_cast<std::uint16_t>(i));
		const bool coversChangedWhite = c.shape == Shape::PianoBlack
			&& std::any_of(changedWhites.begin(), changedWhites.end(), [&](const QRectF& w) { return w.intersects(r); });
		if (m_pressed[i] || expected(i) || coversChangedWhite) { paintState(i); }
	}
}

} // namespace lmms::gui
