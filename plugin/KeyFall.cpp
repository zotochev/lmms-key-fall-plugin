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

#include "AudioEngine.h"
#include "Engine.h"
#include "MidiClient.h"
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

KeyFall::KeyFall()
	: ToolPlugin(&keyfall_plugin_descriptor, nullptr)
	, m_midiPort(QStringLiteral("KeyFall"), Engine::audioEngine()->midiClient(), this, this, MidiPort::Mode::Input)
{
}

QString KeyFall::nodeName() const
{
	return keyfall_plugin_descriptor.name;
}

QStringList KeyFall::availableMidiInputs() const
{
	return Engine::audioEngine()->midiClient()->readablePorts();
}

} // namespace lmms
