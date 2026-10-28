/*
 * Json.h - minimal JSON value and parser for KeyFall core (no Qt)
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

#ifndef KEYFALL_CORE_JSON_H
#define KEYFALL_CORE_JSON_H

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kf::json {

class Value;
using Array = std::vector<Value>;
//! Keys keep insertion order irrelevant for layouts, so a sorted map is enough
using Object = std::map<std::string, Value, std::less<>>;

//! Immutable JSON value. Kept deliberately small: layouts are tiny files.
class Value
{
public:
	Value() = default; // null
	Value(bool b) : m_data(b) {}
	Value(double d) : m_data(d) {}
	Value(std::string s) : m_data(std::move(s)) {}
	Value(Array a) : m_data(std::make_shared<Array>(std::move(a))) {}
	Value(Object o) : m_data(std::make_shared<Object>(std::move(o))) {}

	bool isNull() const { return std::holds_alternative<std::monostate>(m_data); }
	bool isBool() const { return std::holds_alternative<bool>(m_data); }
	bool isNumber() const { return std::holds_alternative<double>(m_data); }
	bool isString() const { return std::holds_alternative<std::string>(m_data); }
	bool isArray() const { return std::holds_alternative<std::shared_ptr<Array>>(m_data); }
	bool isObject() const { return std::holds_alternative<std::shared_ptr<Object>>(m_data); }

	bool asBool() const { return std::get<bool>(m_data); }
	double asNumber() const { return std::get<double>(m_data); }
	const std::string& asString() const { return std::get<std::string>(m_data); }
	const Array& asArray() const { return *std::get<std::shared_ptr<Array>>(m_data); }
	const Object& asObject() const { return *std::get<std::shared_ptr<Object>>(m_data); }

	//! Member lookup on objects; nullptr when absent or when this is not an object
	const Value* find(std::string_view key) const;

	//! "object", "array", ... for error messages
	const char* typeName() const;

private:
	std::variant<std::monostate, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> m_data;
};

struct ParseError
{
	std::string message;
	int line = 0;   //!< 1-based
	int column = 0; //!< 1-based
};

//! Strict RFC 8259 parser (no comments, no trailing commas). Returns false on error.
bool parse(std::string_view text, Value& out, ParseError& error);

} // namespace kf::json

#endif // KEYFALL_CORE_JSON_H
