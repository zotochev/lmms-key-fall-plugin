/*
 * Json.cpp - minimal JSON value and parser for KeyFall core (no Qt)
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

#include "Json.h"

#include <cstdlib>

namespace kf::json {

const Value* Value::find(std::string_view key) const
{
	if (!isObject()) { return nullptr; }
	const auto& obj = asObject();
	const auto it = obj.find(key);
	return it == obj.end() ? nullptr : &it->second;
}

const char* Value::typeName() const
{
	if (isNull()) { return "null"; }
	if (isBool()) { return "boolean"; }
	if (isNumber()) { return "number"; }
	if (isString()) { return "string"; }
	if (isArray()) { return "array"; }
	return "object";
}

namespace {

class Parser
{
public:
	Parser(std::string_view text, ParseError& error)
		: m_text(text)
		, m_error(error)
	{
	}

	bool parseDocument(Value& out)
	{
		skipWhitespace();
		if (!parseValue(out, 0)) { return false; }
		skipWhitespace();
		if (m_pos != m_text.size()) { return fail("unexpected data after the JSON value"); }
		return true;
	}

private:
	static constexpr int MaxDepth = 64;

	bool fail(const std::string& message)
	{
		m_error.message = message;
		m_error.line = 1;
		m_error.column = 1;
		for (std::size_t i = 0; i < m_pos && i < m_text.size(); ++i)
		{
			if (m_text[i] == '\n') { ++m_error.line; m_error.column = 1; }
			else { ++m_error.column; }
		}
		return false;
	}

	bool atEnd() const { return m_pos >= m_text.size(); }
	char peek() const { return atEnd() ? '\0' : m_text[m_pos]; }

	void skipWhitespace()
	{
		while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) { ++m_pos; }
	}

	bool expectLiteral(std::string_view literal)
	{
		if (m_text.substr(m_pos, literal.size()) != literal) { return fail("invalid literal"); }
		m_pos += literal.size();
		return true;
	}

	bool parseValue(Value& out, int depth)
	{
		if (depth > MaxDepth) { return fail("nesting too deep"); }
		switch (peek())
		{
			case '{': return parseObject(out, depth);
			case '[': return parseArray(out, depth);
			case '"':
			{
				std::string s;
				if (!parseString(s)) { return false; }
				out = Value(std::move(s));
				return true;
			}
			case 't': if (!expectLiteral("true")) { return false; } out = Value(true); return true;
			case 'f': if (!expectLiteral("false")) { return false; } out = Value(false); return true;
			case 'n': if (!expectLiteral("null")) { return false; } out = Value(); return true;
			default:
				if (peek() == '-' || (peek() >= '0' && peek() <= '9')) { return parseNumber(out); }
				return fail(atEnd() ? "unexpected end of file" : std::string("unexpected character '") + peek() + "'");
		}
	}

	bool parseObject(Value& out, int depth)
	{
		++m_pos; // {
		Object obj;
		skipWhitespace();
		if (peek() == '}') { ++m_pos; out = Value(std::move(obj)); return true; }
		while (true)
		{
			skipWhitespace();
			if (peek() != '"') { return fail("expected a string key"); }
			std::string key;
			if (!parseString(key)) { return false; }
			skipWhitespace();
			if (peek() != ':') { return fail("expected ':'"); }
			++m_pos;
			skipWhitespace();
			Value v;
			if (!parseValue(v, depth + 1)) { return false; }
			if (obj.count(key)) { return fail("duplicate key \"" + key + "\""); }
			obj.emplace(std::move(key), std::move(v));
			skipWhitespace();
			if (peek() == ',') { ++m_pos; continue; }
			if (peek() == '}') { ++m_pos; break; }
			return fail("expected ',' or '}'");
		}
		out = Value(std::move(obj));
		return true;
	}

	bool parseArray(Value& out, int depth)
	{
		++m_pos; // [
		Array arr;
		skipWhitespace();
		if (peek() == ']') { ++m_pos; out = Value(std::move(arr)); return true; }
		while (true)
		{
			skipWhitespace();
			Value v;
			if (!parseValue(v, depth + 1)) { return false; }
			arr.push_back(std::move(v));
			skipWhitespace();
			if (peek() == ',') { ++m_pos; continue; }
			if (peek() == ']') { ++m_pos; break; }
			return fail("expected ',' or ']'");
		}
		out = Value(std::move(arr));
		return true;
	}

	static void appendUtf8(std::string& s, unsigned cp)
	{
		if (cp < 0x80) { s += static_cast<char>(cp); }
		else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
		else if (cp < 0x10000)
		{
			s += static_cast<char>(0xE0 | (cp >> 12));
			s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			s += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else
		{
			s += static_cast<char>(0xF0 | (cp >> 18));
			s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			s += static_cast<char>(0x80 | (cp & 0x3F));
		}
	}

	bool parseHex4(unsigned& out)
	{
		if (m_pos + 4 > m_text.size()) { return fail("truncated \\u escape"); }
		out = 0;
		for (int i = 0; i < 4; ++i)
		{
			const char c = m_text[m_pos++];
			out <<= 4;
			if (c >= '0' && c <= '9') { out |= c - '0'; }
			else if (c >= 'a' && c <= 'f') { out |= c - 'a' + 10; }
			else if (c >= 'A' && c <= 'F') { out |= c - 'A' + 10; }
			else { return fail("invalid \\u escape"); }
		}
		return true;
	}

	bool parseString(std::string& out)
	{
		++m_pos; // opening quote
		while (true)
		{
			if (atEnd()) { return fail("unterminated string"); }
			const char c = m_text[m_pos++];
			if (c == '"') { return true; }
			if (static_cast<unsigned char>(c) < 0x20) { return fail("control character in string"); }
			if (c != '\\') { out += c; continue; }
			if (atEnd()) { return fail("unterminated escape"); }
			switch (m_text[m_pos++])
			{
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
				{
					unsigned cp = 0;
					if (!parseHex4(cp)) { return false; }
					if (cp >= 0xD800 && cp <= 0xDBFF)
					{
						unsigned low = 0;
						if (m_text.substr(m_pos, 2) != "\\u") { return fail("unpaired surrogate"); }
						m_pos += 2;
						if (!parseHex4(low) || low < 0xDC00 || low > 0xDFFF) { return fail("unpaired surrogate"); }
						cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
					}
					appendUtf8(out, cp);
					break;
				}
				default: return fail("invalid escape");
			}
		}
	}

	bool parseNumber(Value& out)
	{
		const std::size_t start = m_pos;
		if (peek() == '-') { ++m_pos; }
		if (peek() == '0') { ++m_pos; }
		else if (peek() >= '1' && peek() <= '9') { while (peek() >= '0' && peek() <= '9') { ++m_pos; } }
		else { return fail("invalid number"); }
		if (peek() == '.')
		{
			++m_pos;
			if (!(peek() >= '0' && peek() <= '9')) { return fail("invalid number"); }
			while (peek() >= '0' && peek() <= '9') { ++m_pos; }
		}
		if (peek() == 'e' || peek() == 'E')
		{
			++m_pos;
			if (peek() == '+' || peek() == '-') { ++m_pos; }
			if (!(peek() >= '0' && peek() <= '9')) { return fail("invalid number"); }
			while (peek() >= '0' && peek() <= '9') { ++m_pos; }
		}
		const std::string token(m_text.substr(start, m_pos - start));
		out = Value(std::strtod(token.c_str(), nullptr));
		return true;
	}

	std::string_view m_text;
	ParseError& m_error;
	std::size_t m_pos = 0;
};

} // namespace

bool parse(std::string_view text, Value& out, ParseError& error)
{
	// Tolerate a UTF-8 byte order mark written by some Windows editors
	if (text.substr(0, 3) == "\xEF\xBB\xBF") { text.remove_prefix(3); }
	return Parser(text, error).parseDocument(out);
}

} // namespace kf::json
