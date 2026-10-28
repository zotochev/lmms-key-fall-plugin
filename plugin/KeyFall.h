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
#include <memory>
#include <mutex>
#include <vector>

#include "KeyFallView.h"
#include "LayoutRepository.h"
#include "MidiEventProcessor.h"
#include "MidiPort.h"
#include "ToolPlugin.h"
#include "core/InputResolver.h"

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
	//! How strictly note timing is judged (hit-window presets, see PLAN.md §5)
	enum class Tolerance
	{
		Loose,
		Normal,
		Strict
	};

	KeyFall();

	Tolerance tolerance() const { return m_tolerance; }
	//! Also persists the choice in the user's LMMS config
	void setTolerance(Tolerance tolerance);

	QString nodeName() const override;
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}

	gui::PluginView* instantiateView(QWidget*) override { return new gui::KeyFallView(this); }

	// --- MIDI input -------------------------------------------------------

	MidiPort* midiPort() { return &m_midiPort; }
	//! Names of MIDI inputs offered by the current MIDI backend
	QStringList availableMidiInputs() const;
	//! Inputs KeyFall currently listens to
	QStringList subscribedMidiInputs() const;
	//! Stores the current subscriptions so they are restored on the next start
	void saveMidiInputs();

	//! Moves all MIDI events received since the last call into out (GUI thread)
	void drainInput(std::vector<kf::MidiInputEvent>& out);

	// Called on the MIDI driver thread
	void processInEvent(const MidiEvent& event, const TimePos& = TimePos(), f_cnt_t = 0) override;
	void processOutEvent(const MidiEvent&, const TimePos& = TimePos(), f_cnt_t = 0) override {}

	// --- Instrument layouts -----------------------------------------------

	const LayoutRepository& layouts() const { return m_layouts; }
	void reloadLayouts();
	QString currentLayoutId() const { return m_currentLayoutId; }
	std::shared_ptr<const kf::layout::KeyboardLayout> currentLayout() const { return m_currentLayout; }
	//! Selects and persists a layout; unknown ids fall back to the default keyboard
	void selectLayout(const QString& id);

signals:
	void layoutChanged();
	void layoutsReloaded();

private:
	void restoreMidiInputs();

	MidiPort m_midiPort;
	Tolerance m_tolerance = Tolerance::Normal;

	//! Several MIDI backends call back on different threads, so this is a
	//! multi-producer queue; a short mutex is fine because it is never the audio thread.
	std::mutex m_inputMutex;
	std::vector<kf::MidiInputEvent> m_inputQueue;

	LayoutRepository m_layouts;
	QString m_currentLayoutId;
	std::shared_ptr<const kf::layout::KeyboardLayout> m_currentLayout;
};

} // namespace lmms

#endif // LMMS_KEYFALL_H
