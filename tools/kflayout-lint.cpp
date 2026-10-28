/*
 * kflayout-lint.cpp - command-line checker for KeyFall layout files
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

// Usage:
//   kflayout-lint [--strict] FILE...    check layouts; --strict also fails on warnings
//   kflayout-lint --corpus DIR          DIR/valid/* must load without errors,
//                                       DIR/invalid/<CODE>-*.json must fail with <CODE>
// Uses the same core code as the plugin; scripts/validate_layouts.py must agree with it.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "KeyboardLayout.h"

namespace fs = std::filesystem;
using kf::layout::Diagnostic;

namespace {

bool readFile(const fs::path& path, std::string& out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) { return false; }
	std::ostringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

std::vector<Diagnostic> check(const fs::path& path, bool& loaded)
{
	std::vector<Diagnostic> diagnostics;
	std::string text;
	if (!readFile(path, text))
	{
		diagnostics.push_back({Diagnostic::Severity::Error, "E000", "", "cannot read file"});
		loaded = false;
		return diagnostics;
	}
	loaded = kf::layout::KeyboardLayout::fromText(text, diagnostics).has_value();
	return diagnostics;
}

void print(const fs::path& path, const std::vector<Diagnostic>& diagnostics)
{
	for (const auto& d : diagnostics)
	{
		std::cout << path.generic_string() << ": " << (d.isError() ? "error " : "warning ") << d.code;
		if (!d.pointer.empty()) { std::cout << " at " << d.pointer; }
		std::cout << ": " << d.message << "\n";
	}
}

int lintFiles(const std::vector<fs::path>& files, bool strict)
{
	int failures = 0;
	for (const auto& file : files)
	{
		bool loaded = false;
		const auto diagnostics = check(file, loaded);
		print(file, diagnostics);
		const bool failed = !loaded || (strict && !diagnostics.empty());
		failures += failed ? 1 : 0;
		if (!failed) { std::cout << file.generic_string() << ": ok\n"; }
	}
	return failures == 0 ? 0 : 1;
}

int lintCorpus(const fs::path& dir)
{
	int failures = 0, total = 0;
	for (const auto& entry : fs::directory_iterator(dir / "valid"))
	{
		if (entry.path().extension() != ".json") { continue; }
		++total;
		bool loaded = false;
		const auto diagnostics = check(entry.path(), loaded);
		if (!loaded)
		{
			++failures;
			std::cout << "FAIL (expected to load): ";
			print(entry.path(), diagnostics);
		}
	}
	for (const auto& entry : fs::directory_iterator(dir / "invalid"))
	{
		if (entry.path().extension() != ".json") { continue; }
		++total;
		const auto expected = entry.path().filename().string().substr(0, 4); // "E012-....json"
		bool loaded = false;
		const auto diagnostics = check(entry.path(), loaded);
		bool found = false;
		for (const auto& d : diagnostics) { found = found || (d.isError() && d.code == expected); }
		if (loaded || !found)
		{
			++failures;
			std::cout << "FAIL (expected " << expected << "): " << entry.path().generic_string() << "\n";
			print(entry.path(), diagnostics);
		}
	}
	std::cout << total - failures << "/" << total << " corpus files behave as expected\n";
	return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
	std::vector<std::string> args(argv + 1, argv + argc);
	if (args.size() == 2 && args[0] == "--corpus") { return lintCorpus(args[1]); }

	bool strict = false;
	std::vector<fs::path> files;
	for (const auto& a : args)
	{
		if (a == "--strict") { strict = true; }
		else { files.emplace_back(a); }
	}
	if (files.empty())
	{
		std::cerr << "usage: kflayout-lint [--strict] FILE... | kflayout-lint --corpus DIR\n";
		return 2;
	}
	return lintFiles(files, strict);
}
