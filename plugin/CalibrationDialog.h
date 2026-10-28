/*
 * CalibrationDialog.h - measures input + display latency by tapping along
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

#ifndef LMMS_GUI_KEYFALL_CALIBRATION_DIALOG_H
#define LMMS_GUI_KEYFALL_CALIBRATION_DIALOG_H

#include <QDialog>
#include <QTimer>
#include <cstdint>
#include <vector>

#include "core/InputResolver.h"

class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace lmms {
class KeyFall;
}

namespace lmms::gui {

//! Bars fall onto a line on a steady beat (silent, visual only); the user presses any
//! key when a bar lands. The median offset (outliers removed) is the delay between
//! what the screen shows and when KeyFall receives the key, and is subtracted from
//! every press while scoring. Audio-click calibration is a later addition.
class CalibrationDialog : public QDialog
{
public:
	explicit CalibrationDialog(KeyFall* plugin, QWidget* parent = nullptr);

	static constexpr int Bpm = 100;
	static constexpr int CountInBeats = 4;
	static constexpr int MeasuredBeats = 16;

	// Read by the animation widget
	bool isRunning() const { return m_running; }
	std::int64_t beatNs(int beat) const { return m_startNs + beat * beatLengthNs(); }
	static std::int64_t beatLengthNs() { return 60'000'000'000LL / Bpm; }
	int tapsCount() const { return static_cast<int>(m_taps.size()); }
	std::int64_t lastTapNs() const { return m_lastTapNs; }

private:
	void startMeasuring();
	void tick();
	void finish();

	KeyFall* m_plugin;
	QWidget* m_canvas;
	QLabel* m_result;
	QDoubleSpinBox* m_offset;
	QPushButton* m_startButton;
	QTimer m_timer;
	bool m_running = false;
	std::int64_t m_startNs = 0;
	std::int64_t m_lastTapNs = 0;
	std::vector<double> m_taps; //!< ms, + = after the beat
	std::vector<kf::MidiInputEvent> m_input;
};

} // namespace lmms::gui

#endif // LMMS_GUI_KEYFALL_CALIBRATION_DIALOG_H
