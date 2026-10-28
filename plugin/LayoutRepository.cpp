/*
 * LayoutRepository.cpp - built-in and user-provided KeyFall instrument layouts
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

#include "LayoutRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

#include "ConfigManager.h"

namespace lmms {

namespace {
const auto LayoutFilter = QStringLiteral("*.kflayout.json");
} // namespace

QString LayoutRepository::userDirectory()
{
	return ConfigManager::inst()->workingDir() + QStringLiteral("keyfall/layouts/");
}

QString LayoutRepository::builtinDirectory()
{
	// Embedded by BUILD_PLUGIN(... EMBEDDED_RESOURCES layouts/*.kflayout.json)
	return QStringLiteral(":/artwork/keyfall/");
}

LayoutRepository::Entry LayoutRepository::load(const QString& path, bool builtin)
{
	Entry entry;
	entry.path = path;
	entry.builtin = builtin;
	entry.id = QFileInfo(path).fileName();
	entry.name = entry.id;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		entry.messages << QStringLiteral("E000: ") + file.errorString();
		return entry;
	}
	const QByteArray bytes = file.readAll();

	std::vector<kf::layout::Diagnostic> diagnostics;
	auto layout = kf::layout::KeyboardLayout::fromText(std::string_view(bytes.constData(), bytes.size()), diagnostics);
	for (const auto& d : diagnostics)
	{
		entry.messages << QStringLiteral("%1%2: %3")
			.arg(QString::fromStdString(d.code),
				d.pointer.empty() ? QString() : QStringLiteral(" at ") + QString::fromStdString(d.pointer),
				QString::fromStdString(d.message));
	}
	if (layout)
	{
		entry.id = QString::fromStdString(layout->meta().id);
		entry.name = QString::fromStdString(layout->meta().name);
		entry.layout = std::make_shared<const kf::layout::KeyboardLayout>(std::move(*layout));
	}
	return entry;
}

void LayoutRepository::reload()
{
	m_entries.clear();

	const QDir builtinDir(builtinDirectory());
	for (const auto& file : builtinDir.entryList({LayoutFilter}, QDir::Files, QDir::Name))
	{
		m_entries.push_back(load(builtinDir.filePath(file), true));
	}

	const QDir userDir(userDirectory());
	for (const auto& file : userDir.entryList({LayoutFilter}, QDir::Files, QDir::Name))
	{
		Entry entry = load(userDir.filePath(file), false);
		auto builtin = std::find_if(m_entries.begin(), m_entries.end(),
			[&](const Entry& e) { return e.builtin && e.id == entry.id; });
		if (builtin != m_entries.end() && entry.isValid())
		{
			entry.overridesBuiltin = true;
			*builtin = std::move(entry);
		}
		else
		{
			// An invalid override keeps the built-in and is listed separately with its errors
			m_entries.push_back(std::move(entry));
		}
	}

	// Built-in generic keyboards first (sorted by size), then everything else by name
	std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
		if (a.builtin != b.builtin) { return a.builtin; }
		if (a.builtin && a.layout && b.layout) { return a.layout->controls().size() < b.layout->controls().size(); }
		return a.name.localeAwareCompare(b.name) < 0;
	});
}

const LayoutRepository::Entry* LayoutRepository::find(const QString& id) const
{
	for (const auto& e : m_entries)
	{
		if (e.isValid() && e.id == id) { return &e; }
	}
	return nullptr;
}

} // namespace lmms
