/*
 * core_tests.cpp - unit tests for the Qt-free KeyFall core
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

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "Chart.h"
#include "InputResolver.h"
#include "PlayheadClock.h"
#include "Json.h"
#include "KeyboardLayout.h"

using namespace kf::layout;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond) \
	do { ++g_checks; if (!(cond)) { ++g_failures; std::cout << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed\n"; } } while (0)

struct Loaded
{
	std::optional<KeyboardLayout> layout;
	std::vector<Diagnostic> diagnostics;

	bool hasError(const std::string& code) const
	{
		for (const auto& d : diagnostics) { if (d.isError() && d.code == code) { return true; } }
		return false;
	}
	bool hasWarning(const std::string& code) const
	{
		for (const auto& d : diagnostics) { if (!d.isError() && d.code == code) { return true; } }
		return false;
	}
};

Loaded load(const std::string& text)
{
	Loaded l;
	l.layout = KeyboardLayout::fromText(text, l.diagnostics);
	return l;
}

void testNoteNames()
{
	CHECK(parseNoteName("C4") == 60);
	CHECK(parseNoteName("C#4") == 61);
	CHECK(parseNoteName("Db4") == 61);
	CHECK(parseNoteName("A0") == 21);
	CHECK(parseNoteName("C8") == 108);
	CHECK(parseNoteName("C-1") == 0);
	CHECK(parseNoteName("G9") == 127);
	CHECK(!parseNoteName("G#9"));
	CHECK(!parseNoteName("H4"));
	CHECK(!parseNoteName("C"));
	CHECK(!parseNoteName("c4"));
	CHECK(!parseNoteName("C4x"));
	CHECK(noteName(60) == "C4");
	CHECK(noteName(61) == "C#4");
	CHECK(noteName(0) == "C-1");
	CHECK(isBlackKey(61) && !isBlackKey(60) && isBlackKey(70) && !isBlackKey(71));
}

void testJson()
{
	kf::json::Value v;
	kf::json::ParseError err;
	CHECK(kf::json::parse(R"({"a": [1, 2.5, "xé", true, null], "b": {}})", v, err));
	CHECK(v.find("a")->asArray().size() == 5);
	CHECK(v.find("a")->asArray()[2].asString() == "x\xC3\xA9");
	CHECK(!kf::json::parse("{\n  \"a\": 1,\n}", v, err));
	CHECK(err.line == 3);
	CHECK(!kf::json::parse(R"({"a":1,"a":2})", v, err));
	CHECK(!kf::json::parse("[1] 2", v, err));
	CHECK(kf::json::parse("\xEF\xBB\xBF{}", v, err)); // BOM
}

void testPianoShorthand()
{
	const auto l = load(R"({"format":2,"id":"generic-49","name":"Generic 49","piano":["C2","C6"]})");
	CHECK(l.layout.has_value());
	CHECK(l.diagnostics.empty());
	if (!l.layout) { return; }
	const auto& k = *l.layout;
	CHECK(k.controls().size() == 49);
	int whites = 0;
	for (const auto& c : k.controls()) { whites += c.shape == Shape::PianoWhite ? 1 : 0; }
	CHECK(whites == 29);
	CHECK(k.pitchRange() == std::make_pair(36, 84));
	CHECK(std::abs(k.bounds().w - 29.0f) < 1e-4f);
	// C#4 sits between C4 and D4, and lanes never assume pitch == x
	const auto c4 = k.lane(60), cs4 = k.lane(61), d4 = k.lane(62);
	CHECK(c4 && cs4 && d4);
	if (c4 && cs4 && d4)
	{
		CHECK(cs4->centerX() > c4->centerX() && cs4->centerX() < d4->centerX());
		CHECK(cs4->w < c4->w && cs4->h < c4->h);
	}
	CHECK(!k.lane(35) && !k.lane(85));
	CHECK(k.controlForInput(1, 60).has_value());
	CHECK(k.controlForInput(16, 60).has_value());
}

void testPianoFactory()
{
	const auto k = KeyboardLayout::piano(21, 108, "generic-88", "88 keys");
	CHECK(k.controls().size() == 88);
	CHECK(k.pitchRange() == std::make_pair(21, 108));
	CHECK(k.meta().id == "generic-88");
}

void testGridFormula()
{
	const auto l = load(R"({"format":2,"id":"pads","name":"Pads","zones":[
		{"type":"grid","rows":8,"cols":8,"startNote":11,"colStep":1,"rowStep":10,"input":{"channel":1}}]})");
	CHECK(l.layout.has_value());
	if (!l.layout) { return; }
	const auto& k = *l.layout;
	CHECK(k.controls().size() == 64);
	CHECK(k.controls().front().pitch == 81); // top-left, rows counted bottom-up
	CHECK(k.controls()[56].pitch == 11);     // bottom-left
	CHECK(k.controlForInput(1, 11).has_value());
	CHECK(!k.controlForInput(2, 11).has_value()); // channel filter
}

void testGridExplicitNotes()
{
	const auto ok = load(R"({"format":2,"id":"g","name":"G","zones":[
		{"type":"grid","rows":1,"cols":3,"notes":[36,"D2",36]}]})");
	CHECK(ok.layout.has_value());
	if (ok.layout) { CHECK(ok.layout->candidates(36).size() == 2); } // same note on two pads

	const auto bad = load(R"({"format":2,"id":"g","name":"G","zones":[{"type":"grid","rows":2,"cols":2,"notes":[1,2,3]}]})");
	CHECK(!bad.layout && bad.hasError("E009"));
}

void testZonesAndChannels()
{
	const auto l = load(R"({"format":2,"id":"kp","name":"Keys and pads","zones":[
		{"id":"keys","type":"piano","firstNote":48,"lastNote":72},
		{"id":"pads","type":"grid","rows":2,"cols":2,"notes":[40,41,36,37],"input":{"channel":10},"hand":"left"}]})");
	CHECK(l.layout.has_value());
	if (!l.layout) { return; }
	const auto& k = *l.layout;
	CHECK(k.zones().size() == 2);
	CHECK(k.zones()[1].channel == 10 && k.zones()[1].hand == Hand::Left);
	// pads are placed to the right of the keys
	CHECK(k.controls().back().rect.x > 14.0f);
	CHECK(k.controlForInput(10, 36).has_value());
	CHECK(!k.controlForInput(1, 36).has_value());
}

void testMigrationV1()
{
	const auto l = load(R"({"format":1,"id":"old","name":"Old","octaveShift":{"min":-3,"max":3},
		"zones":[{"type":"grid","rows":1,"cols":2,"notes":[36,38],"channel":10}]})");
	CHECK(l.layout.has_value());
	CHECK(l.hasWarning("W002"));
	if (l.layout) { CHECK(l.layout->zones()[0].channel == 10); }
}

void testErrors()
{
	CHECK(load("{").hasError("E001"));
	CHECK(load(R"({"id":"x","name":"X","piano":[60,72]})").hasError("E002"));
	CHECK(load(R"({"format":2,"id":"Bad_ID","name":"X","piano":[60,72]})").hasError("E003"));
	CHECK(load(R"({"format":2,"id":"x","piano":[60,72]})").hasError("E004"));
	CHECK(load(R"({"format":2,"id":"x","name":"X"})").hasError("E005"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","zones":[{"type":"wheel"}]})").hasError("E006"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","piano":[60,128]})").hasError("E007"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","piano":["C5","C4"]})").hasError("E008"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","zones":[{"type":"grid","rows":0,"cols":2,"startNote":1}]})").hasError("E009"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","zones":[{"type":"piano","firstNote":1,"lastNote":2,"input":{"channel":17}}]})").hasError("E010"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","zones":[{"id":"a","type":"piano","firstNote":1,"lastNote":2},{"id":"a","type":"piano","firstNote":3,"lastNote":4}]})").hasError("E011"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","piano":[60,72],"states":{}})").hasError("E012"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","piano":[60,72],"requires":["states"]})").hasError("E012"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","zones":[{"type":"hex"}]})").hasError("E012"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","zones":[{"type":"grid","rows":1,"cols":1,"notes":[[60,64]]}]})").hasError("E012"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","zones":[{"type":"grid","rows":32,"cols":32,"startNote":0,"colStep":0,"rowStep":0},{"type":"grid","rows":1,"cols":1,"notes":[1]}]})").hasError("E013"));
	CHECK(load(R"({"format":2,"id":"x","name":5,"piano":[60,72]})").hasError("E014"));
	CHECK(load(R"({"format":2,"id":"x","name":"X","piano":[60,72],"zones":[]})").hasError("E015"));

	const auto unknown = load(R"({"format":2,"id":"x","name":"X","piano":[60,72],"colour":"red"})");
	CHECK(unknown.layout.has_value() && unknown.hasWarning("W001"));
}

void testInputResolver()
{
	const auto keys = KeyboardLayout::piano(48, 72, "k", "K");
	const kf::InputResolver resolver(&keys);

	const auto on = resolver.resolve({100, 0x90, 1, 60, 90});
	CHECK(on && on->on && on->velocity == 90 && on->pitch == 60 && on->timeNs == 100);
	CHECK(on && on->control && keys.controls()[*on->control].pitch == 60);

	const auto zeroVelocity = resolver.resolve({200, 0x90, 1, 60, 0}); // running-status style release
	CHECK(zeroVelocity && !zeroVelocity->on);
	const auto off = resolver.resolve({300, 0x80, 1, 60, 64});
	CHECK(off && !off->on && off->velocity == 0);

	const auto outside = resolver.resolve({400, 0x90, 1, 30, 90});
	CHECK(outside && outside->on && !outside->control);

	CHECK(!resolver.resolve({500, 0xB0, 1, 64, 127})); // CC is not a press
	CHECK(!kf::InputResolver().resolve({0, 0x90, 1, 60, 1})->control); // no layout yet
}

void testChartBuild()
{
	using kf::Chart;
	using kf::RawNote;
	kf::ChartStats stats;
	// Clip of 2 bars (384 ticks), left edge trimmed by one beat: startTimeOffset = -48
	const std::vector<RawNote> raw = {
		{0, 48, 60, 100},    // before the trimmed left edge: hidden
		{48, 96, 62, 100},   // chart tick 0
		{96, 96, 64, 80},    // overlaps the next E4: trimmed to 48
		{144, 48, 64, 80},
		{144, 48, 64, 80},   // exact duplicate: dropped
		{200, 0, 67, 90},    // step note: minimal length
		{440, 48, 72, 100},  // past the right edge (pos >= 384 + 48): hidden
		{420, 200, 71, 100}, // starts inside, runs past the edge: cut at the clip end
	};
	const Chart c = Chart::build(raw, 384, -48, &stats);
	CHECK(stats.outsideClip == 2);
	CHECK(stats.overlapsTrimmed == 1);
	CHECK(stats.duplicatesDropped == 1);
	CHECK(stats.shortExtended == 1);
	CHECK(c.notes().size() == 5);
	if (c.notes().size() != 5) { return; }
	CHECK(c.notes()[0].start == 0 && c.notes()[0].key == 62);
	CHECK(c.notes()[1].key == 64 && c.notes()[1].length == 48);
	CHECK(c.notes()[3].key == 67 && c.notes()[3].length == Chart::MinLength);
	CHECK(c.notes()[4].key == 71 && c.notes()[4].end() <= 384 + 1e-9);
	CHECK(c.keyRange() == std::make_pair(62, 71));
	// sorted by start
	for (std::size_t i = 1; i < c.notes().size(); ++i) { CHECK(c.notes()[i - 1].start <= c.notes()[i].start); }
	// firstVisible never skips a note that is still sounding
	for (double t = 0; t < 384; t += 7)
	{
		const auto first = c.firstVisible(t);
		for (std::size_t i = 0; i < first; ++i) { CHECK(c.notes()[i].end() <= t); }
	}
	CHECK(Chart::build({}, 192, 0).empty());
}

void testAutoKeyRange()
{
	CHECK(kf::autoKeyRange(60, 67) == std::make_pair(48, 71));   // C3..B4: at least two octaves
	CHECK(kf::autoKeyRange(40, 84) == std::make_pair(36, 95));   // E2..C6 -> C2..B6
	CHECK(kf::autoKeyRange(0, 3) == std::make_pair(0, 23));
	const auto top = kf::autoKeyRange(120, 127);
	CHECK(top.second == 127 && top.first % 12 == 0);
	CHECK(kf::autoKeyRange(0, -1) == std::make_pair(48, 72));    // empty clip
}

void testPlayheadClock()
{
	constexpr std::int64_t Ms = 1000000;
	const double tps = 96; // 120 bpm
	kf::PlayheadClock clock;

	// Stopped: shows the host position as is
	CHECK(clock.update(10, 0, tps, false) == 10);

	// Playing: host updates every 10 ms (one buffer), frames every 4 ms
	double prev = clock.update(10, 0, tps, true);
	double maxError = 0;
	for (std::int64_t t = 4; t <= 1000; t += 4)
	{
		const double raw = 10 + std::floor(t / 10.0) * 10 * tps / 1000; // stepwise host position
		const double shown = clock.update(raw, t * Ms, tps, true);
		CHECK(shown >= prev); // never runs backwards
		maxError = std::max(maxError, std::abs(shown - (10 + t * tps / 1000)));
		prev = shown;
	}
	CHECK(maxError < 0.02 * tps); // within 20 ms of the true position

	// Loop wrap: snaps back immediately
	CHECK(clock.update(5, 1004 * Ms, tps, true) == 5);
	// Engine stalls: extrapolation is capped
	const double stalled = clock.update(5, 3000 * Ms, tps, true);
	CHECK(stalled <= 5 + kf::PlayheadClock::MaxExtrapolationSeconds * tps + 1e-9);
	// Big forward seek: snaps
	CHECK(clock.update(500, 3004 * Ms, tps, true) == 500);
}

} // namespace

int main()
{
	const std::vector<std::pair<const char*, std::function<void()>>> tests = {
		{"note names", testNoteNames},
		{"json", testJson},
		{"piano shorthand", testPianoShorthand},
		{"piano factory", testPianoFactory},
		{"grid formula", testGridFormula},
		{"grid explicit notes", testGridExplicitNotes},
		{"zones and channels", testZonesAndChannels},
		{"migration v1", testMigrationV1},
		{"errors", testErrors},
		{"input resolver", testInputResolver},
		{"chart build", testChartBuild},
		{"auto key range", testAutoKeyRange},
		{"playhead clock", testPlayheadClock},
	};
	for (const auto& [name, fn] : tests)
	{
		const int before = g_failures;
		fn();
		std::cout << (g_failures == before ? "[ ok ] " : "[FAIL] ") << name << "\n";
	}
	std::cout << g_checks - g_failures << "/" << g_checks << " checks passed\n";
	return g_failures == 0 ? 0 : 1;
}
