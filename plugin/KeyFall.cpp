/*
 * KeyFall.cpp - MIDI keyboard practice trainer (tool plugin)
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

#include "KeyFall.h"

#include <chrono>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "MidiClient.h"
#include "MidiEvent.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms {

extern "C" {
Plugin::Descriptor PLUGIN_EXPORT keyfall_plugin_descriptor
	= {LMMS_STRINGIFY(PLUGIN_NAME), "KeyFall",
		QT_TRANSLATE_NOOP("PluginBrowser", "Practise a MIDI clip on your keyboard with falling notes and scoring"),
		"Mikhail Zotochev <zotochev@gmail.com>", 0x0100, Plugin::Type::Tool, new PluginPixmapLoader("logo"),
		nullptr, nullptr};

PLUGIN_EXPORT Plugin* lmms_plugin_main(Model*, void*)
{
	return new KeyFall;
}
}

namespace {

const auto ConfigSection = QStringLiteral("keyfall");
const auto ToleranceKey = QStringLiteral("tolerance");
const auto MidiInputsKey = QStringLiteral("midiinputs");
const auto LayoutKey = QStringLiteral("layout");
// Port names go into an XML attribute, where newlines would not survive
const QChar MidiInputSeparator = QLatin1Char(';');
const auto DefaultLayoutId = QStringLiteral("generic-61");
// Drop input rather than grow without bound if the window is hidden for long
constexpr std::size_t MaxQueuedEvents = 4096;

QString toleranceToString(KeyFall::Tolerance tolerance)
{
	switch (tolerance)
	{
		case KeyFall::Tolerance::Loose: return QStringLiteral("loose");
		case KeyFall::Tolerance::Strict: return QStringLiteral("strict");
		default: return QStringLiteral("normal");
	}
}

KeyFall::Tolerance toleranceFromString(const QString& value)
{
	if (value == QLatin1String("loose")) { return KeyFall::Tolerance::Loose; }
	if (value == QLatin1String("strict")) { return KeyFall::Tolerance::Strict; }
	return KeyFall::Tolerance::Normal;
}

std::int64_t nowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

KeyFall::KeyFall()
	: ToolPlugin(&keyfall_plugin_descriptor, nullptr)
	, m_midiPort(QStringLiteral("KeyFall"), Engine::audioEngine()->midiClient(), this, this, MidiPort::Mode::Input)
	, m_tolerance(toleranceFromString(ConfigManager::inst()->value(ConfigSection, ToleranceKey)))
{
	restoreMidiInputs();
	m_layouts.reload();
	selectLayout(ConfigManager::inst()->value(ConfigSection, LayoutKey, DefaultLayoutId));
}

void KeyFall::setTolerance(Tolerance tolerance)
{
	m_tolerance = tolerance;
	ConfigManager::inst()->setValue(ConfigSection, ToleranceKey, toleranceToString(tolerance));
}

QString KeyFall::nodeName() const
{
	return keyfall_plugin_descriptor.name;
}

QStringList KeyFall::availableMidiInputs() const
{
	return Engine::audioEngine()->midiClient()->readablePorts();
}

QStringList KeyFall::subscribedMidiInputs() const
{
	QStringList result;
	const auto& ports = m_midiPort.readablePorts();
	for (auto it = ports.begin(); it != ports.end(); ++it)
	{
		if (it.value()) { result << it.key(); }
	}
	return result;
}

void KeyFall::restoreMidiInputs()
{
	auto* client = Engine::audioEngine()->midiClient();
	if (client->isRaw()) { return; } // raw clients (e.g. OSS) have a single implicit input
	const QStringList available = client->readablePorts();
	// First start ("*", nothing saved yet): listen to everything, KeyFall only reads input.
	// Afterwards: exactly what the user chose, possibly nothing.
	const QString saved = ConfigManager::inst()->value(ConfigSection, MidiInputsKey, QStringLiteral("*"));
	const QStringList wanted = saved == QLatin1String("*") ? available : saved.split(MidiInputSeparator, Qt::SkipEmptyParts);
	for (const auto& port : wanted)
	{
		if (available.contains(port)) { m_midiPort.subscribeReadablePort(port, true); }
	}
}

void KeyFall::saveMidiInputs()
{
	ConfigManager::inst()->setValue(ConfigSection, MidiInputsKey, subscribedMidiInputs().join(MidiInputSeparator));
}

void KeyFall::processInEvent(const MidiEvent& event, const TimePos&, f_cnt_t)
{
	kf::MidiInputEvent e;
	e.timeNs = nowNs();
	e.channel = static_cast<std::uint8_t>(event.channel() + 1);
	switch (event.type())
	{
		case MidiNoteOn:
		case MidiNoteOff:
			e.status = static_cast<std::uint8_t>(event.type());
			e.data1 = static_cast<std::uint8_t>(event.key());
			e.data2 = event.velocity();
			break;
		case MidiControlChange:
			e.status = static_cast<std::uint8_t>(event.type());
			e.data1 = event.controllerNumber();
			e.data2 = event.controllerValue();
			break;
		default:
			return;
	}
	const std::lock_guard lock(m_inputMutex);
	if (m_inputQueue.size() < MaxQueuedEvents) { m_inputQueue.push_back(e); }
}

void KeyFall::drainInput(std::vector<kf::MidiInputEvent>& out)
{
	out.clear();
	const std::lock_guard lock(m_inputMutex);
	out.swap(m_inputQueue);
}

void KeyFall::reloadLayouts()
{
	m_layouts.reload();
	emit layoutsReloaded();
	selectLayout(m_currentLayoutId);
}

void KeyFall::selectLayout(const QString& id)
{
	const auto* entry = m_layouts.find(id);
	if (!entry) { entry = m_layouts.find(DefaultLayoutId); }
	if (entry)
	{
		m_currentLayoutId = entry->id;
		m_currentLayout = entry->layout;
	}
	else
	{
		// Even without any layout files the trainer keeps working with a 61-key piano
		m_currentLayoutId = DefaultLayoutId;
		m_currentLayout = std::make_shared<const kf::layout::KeyboardLayout>(
			kf::layout::KeyboardLayout::piano(36, 96, DefaultLayoutId.toStdString(), "61 keys"));
	}
	if (m_layouts.find(id)) { ConfigManager::inst()->setValue(ConfigSection, LayoutKey, m_currentLayoutId); }
	emit layoutChanged();
}

} // namespace lmms
