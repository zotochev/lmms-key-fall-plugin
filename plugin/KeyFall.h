/*
 * KeyFall.h - MIDI keyboard practice trainer (tool plugin)
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

#ifndef LMMS_KEYFALL_H
#define LMMS_KEYFALL_H

#include <QStringList>

#include "KeyFallView.h"
#include "MidiEventProcessor.h"
#include "MidiPort.h"
#include "ToolPlugin.h"

namespace lmms {

//! Tool plugin model. Lives for the whole LMMS session (created at startup,
//! survives project reloads) and is never saved into a project: per-user
//! settings go through ConfigManager instead of saveSettings/loadSettings.
//!
//! Owns its own input-only MidiPort, so the user's keyboard can be listened to
//! without touching any instrument track. KeyFall never produces MIDI output.
class KeyFall : public ToolPlugin, public MidiEventProcessor
{
	Q_OBJECT
public:
	KeyFall();

	QString nodeName() const override;
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}

	gui::PluginView* instantiateView(QWidget*) override { return new gui::KeyFallView(this); }

	//! Names of MIDI inputs offered by the current MIDI backend
	QStringList availableMidiInputs() const;

	// Events arrive on the MIDI driver thread; handled from stage 3 on.
	void processInEvent(const MidiEvent&, const TimePos& = TimePos(), f_cnt_t = 0) override {}
	void processOutEvent(const MidiEvent&, const TimePos& = TimePos(), f_cnt_t = 0) override {}

private:
	MidiPort m_midiPort;
};

} // namespace lmms

#endif // LMMS_KEYFALL_H
