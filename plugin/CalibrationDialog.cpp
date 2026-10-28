/*
 * CalibrationDialog.cpp - measures input + display latency by tapping along
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

#include "CalibrationDialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <chrono>
#include <cmath>

#include "KeyFall.h"
#include "core/Scorer.h"

namespace lmms::gui {

namespace {

constexpr double AcceptWindowMs = 250; // taps further from a beat are ignored
const QColor Background{0x1e, 0x22, 0x26};
const QColor Bar{0x56, 0xb4, 0xe9};
const QColor Line{0xf0, 0xe4, 0x42};
const QColor Text{0xc0, 0xc4, 0xca};

std::int64_t nowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

class BeatCanvas : public QWidget
{
public:
	explicit BeatCanvas(const CalibrationDialog* dialog)
		: m_dialog(dialog)
	{
		setMinimumSize(360, 180);
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.fillRect(rect(), Background);
		const double lineY = height() - 24;
		p.fillRect(QRectF(0, lineY, width(), 2), Line);
		p.setPen(Text);
		QFont f = font();
		f.setPixelSize(14);
		p.setFont(f);
		if (!m_dialog->isRunning())
		{
			p.drawText(rect().adjusted(16, 16, -16, -40), Qt::AlignCenter | Qt::TextWordWrap,
				tr("Press Start, then press any key on your MIDI keyboard each time a bar touches the line."));
			return;
		}

		const std::int64_t now = nowNs();
		const double beat = static_cast<double>(CalibrationDialog::beatLengthNs());
		const int total = CalibrationDialog::CountInBeats + CalibrationDialog::MeasuredBeats;
		p.setRenderHint(QPainter::Antialiasing);
		// Each bar falls for two beats and lands exactly on its beat
		for (int b = 0; b < total; ++b)
		{
			const double untilLanding = (m_dialog->beatNs(b) - now) / beat; // beats
			if (untilLanding > 2 || untilLanding < -0.15) { continue; }
			const double y = lineY - untilLanding / 2 * lineY;
			const bool countIn = b < CalibrationDialog::CountInBeats;
			QColor c = Bar;
			c.setAlpha(countIn ? 110 : 255);
			p.setPen(Qt::NoPen);
			p.setBrush(c);
			p.drawRoundedRect(QRectF(width() / 2.0 - 40, y - 16, 80, 16), 4, 4);
			if (countIn && untilLanding < 1 && untilLanding > 0)
			{
				p.setPen(Text);
				p.drawText(QRectF(0, 8, width(), 24), Qt::AlignHCenter,
					tr("Get ready: %1").arg(CalibrationDialog::CountInBeats - b));
			}
		}
		const int measured = static_cast<int>((now - m_dialog->beatNs(CalibrationDialog::CountInBeats)) / beat) + 1;
		if (measured > 0)
		{
			p.setPen(Text);
			p.drawText(QRectF(0, 8, width(), 24), Qt::AlignHCenter,
				tr("Tap on the line — %1 / %2").arg(std::min(measured, CalibrationDialog::MeasuredBeats))
					.arg(CalibrationDialog::MeasuredBeats));
		}
		// Brief flash when a tap was registered
		if (now - m_dialog->lastTapNs() < 120'000'000)
		{
			p.fillRect(QRectF(0, lineY - 3, width(), 8), QColor(0x00, 0x9e, 0x73, 160));
		}
	}

private:
	const CalibrationDialog* m_dialog;
};

} // namespace

CalibrationDialog::CalibrationDialog(KeyFall* plugin, QWidget* parent)
	: QDialog(parent)
	, m_plugin(plugin)
	, m_canvas(new BeatCanvas(this))
	, m_result(new QLabel())
	, m_offset(new QDoubleSpinBox())
	, m_startButton(new QPushButton(tr("Start")))
{
	setWindowTitle(tr("KeyFall: calibrate latency"));

	auto intro = new QLabel(tr("Keyboards, MIDI drivers and screens all add a little delay. "
		"Tap along with the falling bars so KeyFall can subtract it when scoring. "
		"This is silent: watch the bars, not the sound."));
	intro->setWordWrap(true);

	m_result->setWordWrap(true);
	m_offset->setRange(-300, 300);
	m_offset->setDecimals(0);
	m_offset->setSingleStep(5);
	m_offset->setSuffix(tr(" ms"));
	m_offset->setValue(plugin->inputOffsetMs());
	m_offset->setToolTip(tr("Positive: your presses reach KeyFall late, so they are moved earlier"));

	auto offsetRow = new QHBoxLayout();
	offsetRow->addWidget(new QLabel(tr("Input offset:")));
	offsetRow->addWidget(m_offset);
	offsetRow->addStretch();
	offsetRow->addWidget(m_startButton);

	auto buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
		m_plugin->setInputOffsetMs(m_offset->value());
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_startButton, &QPushButton::clicked, this, &CalibrationDialog::startMeasuring);

	auto layout = new QVBoxLayout(this);
	layout->addWidget(intro);
	layout->addWidget(m_canvas, 1);
	layout->addWidget(m_result);
	layout->addLayout(offsetRow);
	layout->addWidget(buttons);

	m_timer.setInterval(16);
	m_timer.setTimerType(Qt::PreciseTimer);
	connect(&m_timer, &QTimer::timeout, this, &CalibrationDialog::tick);
	m_timer.start();
}

void CalibrationDialog::startMeasuring()
{
	m_plugin->drainInput(m_input); // forget earlier presses
	m_taps.clear();
	m_startNs = nowNs() + 2 * beatLengthNs(); // the first bar needs two beats to fall
	m_running = true;
	m_startButton->setEnabled(false);
	m_result->clear();
}

void CalibrationDialog::tick()
{
	m_plugin->drainInput(m_input);
	if (m_running)
	{
		const int first = CountInBeats;
		const int last = CountInBeats + MeasuredBeats - 1;
		for (const auto& e : m_input)
		{
			if (e.status != 0x90 || e.data2 == 0) { continue; }
			const double beats = static_cast<double>(e.timeNs - m_startNs) / beatLengthNs();
			const int nearest = static_cast<int>(std::lround(beats));
			if (nearest < first || nearest > last) { continue; }
			const double deltaMs = (e.timeNs - beatNs(nearest)) / 1e6;
			if (std::abs(deltaMs) > AcceptWindowMs) { continue; }
			m_taps.push_back(deltaMs);
			m_lastTapNs = e.timeNs;
		}
		if (nowNs() > beatNs(last) + beatLengthNs() / 2) { finish(); }
	}
	m_canvas->update();
}

void CalibrationDialog::finish()
{
	m_running = false;
	m_startButton->setEnabled(true);
	m_startButton->setText(tr("Try again"));
	const auto offset = kf::calibrationOffsetMs(m_taps);
	if (!offset)
	{
		m_result->setText(tr("Only %n tap(s) were close enough to the beat. Try again and tap when a bar touches the line.",
			nullptr, static_cast<int>(m_taps.size())));
		return;
	}
	m_offset->setValue(std::round(*offset));
	m_result->setText(tr("Measured %1 ms from %2 taps. Press Apply to use it.")
		.arg(*offset >= 0 ? QStringLiteral("+%1").arg(qRound(*offset)) : QString::number(qRound(*offset)))
		.arg(m_taps.size()));
}

} // namespace lmms::gui
