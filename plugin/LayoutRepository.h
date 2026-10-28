/*
 * LayoutRepository.h - built-in and user-provided KeyFall instrument layouts
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

#ifndef LMMS_KEYFALL_LAYOUT_REPOSITORY_H
#define LMMS_KEYFALL_LAYOUT_REPOSITORY_H

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

#include "core/KeyboardLayout.h"

namespace lmms {

//! Loads *.kflayout.json files: built-ins embedded in the plugin and the user's
//! own files from <LMMS working dir>/keyfall/layouts/. A valid user layout with
//! the id of a built-in one replaces it; a broken file never breaks the plugin,
//! it is listed as unavailable together with its diagnostics.
class LayoutRepository
{
public:
	struct Entry
	{
		QString id;   //!< layout id, or the file name when the file could not be parsed
		QString name;
		QString path; //!< resource path (":/...") or file system path
		bool builtin = false;
		bool overridesBuiltin = false;
		std::shared_ptr<const kf::layout::KeyboardLayout> layout; //!< null when invalid
		QStringList messages; //!< formatted diagnostics (errors and warnings)

		bool isValid() const { return layout != nullptr; }
	};

	void reload();
	const std::vector<Entry>& entries() const { return m_entries; }
	//! Valid entry with this id, or nullptr
	const Entry* find(const QString& id) const;

	static QString userDirectory();
	static QString builtinDirectory();

private:
	static Entry load(const QString& path, bool builtin);

	std::vector<Entry> m_entries;
};

} // namespace lmms

#endif // LMMS_KEYFALL_LAYOUT_REPOSITORY_H
