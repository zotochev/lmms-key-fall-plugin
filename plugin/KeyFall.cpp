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

#include <algorithm>
#include <chrono>

#include <QTimer>

#include "Mixer.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "MidiPort.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "embed.h"
#include "export.h"

// LMMS 1.2 asks each plug-in for its pixmaps (PluginPixmapLoader); KeyFall keeps them in its Qt resources
namespace PLUGIN_NAME {
QPixmap getIconPixmap(const char* name, int width, int height)
{
	QPixmap pixmap(QStringLiteral(":/artwork/keyfall/%1.png").arg(QString::fromLatin1(name)));
	if (width > 0 && height > 0) { pixmap = pixmap.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation); }
	return pixmap;
}
} // namespace PLUGIN_NAME

namespace lmms {

extern "C" {
Plugin::Descriptor PLUGIN_EXPORT keyfall_plugin_descriptor
	= {STRINGIFY(PLUGIN_NAME), "KeyFall",
		QT_TRANSLATE_NOOP("PluginBrowser", "Practise a MIDI clip on your keyboard with falling notes and scoring"),
		"Mikhail Zotochev <zotochev@gmail.com>", 0x0100, Plugin::Tool, new PluginPixmapLoader("logo"),
		nullptr, nullptr};

PLUGIN_EXPORT Plugin* lmms_plugin_main(Model*, void*)
{
	return new KeyFall;
}
}

namespace {

const auto ConfigSection = QStringLiteral("keyfall");
const auto ToleranceKey = QStringLiteral("tolerance");
const auto LayoutKey = QStringLiteral("layout");
const auto LookaheadKey = QStringLiteral("lookahead");
const auto InputOffsetKey = QStringLiteral("inputoffset");
constexpr double MaxInputOffsetMs = 300;
//! Fallback when a saved layout id no longer exists
const auto DefaultLayoutId = QStringLiteral("generic-61");
constexpr double MinLookahead = 1.5;
constexpr double MaxLookahead = 8.0;
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

const QString KeyFall::AutoLayoutId = QStringLiteral("auto");

KeyFall::KeyFall()
	: ToolPlugin(&keyfall_plugin_descriptor, nullptr)
	, m_tolerance(toleranceFromString(ConfigManager::inst()->value(ConfigSection, ToleranceKey)))
	, m_chart(std::make_shared<const kf::Chart>())
{
	bool ok = false;
	const double lookahead = ConfigManager::inst()->value(ConfigSection, LookaheadKey).toDouble(&ok);
	if (ok) { m_lookaheadSeconds = std::clamp(lookahead, MinLookahead, MaxLookahead); }
	const double offset = ConfigManager::inst()->value(ConfigSection, InputOffsetKey).toDouble(&ok);
	if (ok) { m_inputOffsetMs = std::clamp(offset, -MaxInputOffsetMs, MaxInputOffsetMs); }

	m_layouts.reload();
	// A fresh install fits the keyboard to the clip; afterwards the user's choice is kept
	applyLayout(ConfigManager::inst()->value(ConfigSection, LayoutKey, AutoLayoutId), false);

	auto* song = Engine::getSong();
	for (Track* track : song->tracks()) { listenToTrack(track); }
	// trackAdded comes from Track's own constructor, before the InstrumentTrack part exists:
	// look at the track once it is fully built
	connect(song, &TrackContainer::trackAdded, this, [this](Track* track) {
		QTimer::singleShot(0, this, [this, track = QPointer<Track>(track)] {
			if (track) { listenToTrack(track); }
		});
	});
	connect(song, &Song::projectLoaded, this, [this] { selectClip(nullptr); });
	connect(song, &Song::playbackPositionChanged, this, &KeyFall::transportJumped);
	connect(song, &Song::playbackStateChanged, this, &KeyFall::transportJumped);
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

QStringList KeyFall::listeningTracks() const
{
	QStringList result;
	for (Track* track : Engine::getSong()->tracks())
	{
		auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(track);
		if (!instrumentTrack || !instrumentTrack->midiPort()->isReadable()) { continue; }
		// LMMS can leave input switched on with no device ticked (auto-assign moving to another
		// track does that). Raw MIDI backends list no devices and always deliver.
		const auto& ports = instrumentTrack->midiPort()->readablePorts();
		if (ports.isEmpty() || std::any_of(ports.begin(), ports.end(), [](bool on) { return on; }))
		{
			result << track->name();
		}
	}
	return result;
}

void KeyFall::listenToTrack(Track* track)
{
	auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(track);
	if (!instrumentTrack) { return; }
	// Direct: the press is timestamped on the thread that received it, not when the GUI gets to it.
	// The connections die with the track.
	connect(instrumentTrack, &InstrumentTrack::midiNoteOn, this,
		[this, instrumentTrack](const Note& note) { trackNote(instrumentTrack, note, true); }, Qt::DirectConnection);
	connect(instrumentTrack, &InstrumentTrack::midiNoteOff, this,
		[this, instrumentTrack](const Note& note) { trackNote(instrumentTrack, note, false); }, Qt::DirectConnection);
}

void KeyFall::trackNote(InstrumentTrack* track, const Note& note, bool on)
{
	const int key = note.key();
	if (key < 0 || key >= static_cast<int>(m_keyDown.size())) { return; }
	kf::MidiInputEvent e;
	e.timeNs = nowNs();
	// The signal carries the note handle, which knows the channel the key came in on
	const auto* handle = dynamic_cast<const NotePlayHandle*>(&note);
	e.channel = static_cast<std::uint8_t>((handle ? handle->midiChannel() : 0) + 1);
	e.status = static_cast<std::uint8_t>(on ? MidiNoteOn : MidiNoteOff);
	e.data1 = static_cast<std::uint8_t>(key);
	e.data2 = on ? static_cast<std::uint8_t>(std::clamp(note.midiVelocity(track->midiPort()->baseVelocity()), 1, 127)) : 0;
	const std::lock_guard lock(m_inputMutex);
	if (m_keyDown[key] == on) { return; }
	m_keyDown[key] = on;
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
	applyLayout(m_currentLayoutId, false);
}

void KeyFall::selectLayout(const QString& id)
{
	applyLayout(id, true);
}

void KeyFall::applyLayout(const QString& id, bool persist)
{
	const auto* entry = id == AutoLayoutId ? nullptr : m_layouts.find(id);
	if (id == AutoLayoutId)
	{
		const auto [lo, hi] = m_chart->keyRange();
		const auto [first, last] = kf::autoKeyRange(lo, hi);
		m_currentLayoutId = AutoLayoutId;
		m_currentLayout = std::make_shared<const kf::layout::KeyboardLayout>(
			kf::layout::KeyboardLayout::piano(first, last, "auto", "Auto"));
	}
	else if (entry || (entry = m_layouts.find(DefaultLayoutId)))
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
	if (persist && m_currentLayoutId == id) { ConfigManager::inst()->setValue(ConfigSection, LayoutKey, id); }
	emit layoutChanged();
}

QVector<ClipChoice> KeyFall::listClips() const
{
	QVector<ClipChoice> result;
	const int ticksPerBar = std::max(1, Engine::getSong()->ticksPerTact());
	for (Track* track : Engine::getSong()->tracks())
	{
		if (track->type() != Track::InstrumentTrack) { continue; }
		for (TrackContentObject* clip : track->getTCOs())
		{
			auto* pattern = dynamic_cast<Pattern*>(clip);
			if (!pattern) { continue; }
			const int firstBar = pattern->startPosition().getTicks() / ticksPerBar + 1;
			const int lastBar = (pattern->startPosition().getTicks() + std::max(1, pattern->length().getTicks() - 1))
				/ ticksPerBar + 1;
			QString label = track->name();
			if (!pattern->name().isEmpty() && pattern->name() != track->name()) { label += " › " + pattern->name(); }
			label += firstBar == lastBar ? tr("  (bar %1)").arg(firstBar) : tr("  (bars %1–%2)").arg(firstBar).arg(lastBar);
			if (pattern->notes().empty()) { label += tr(", empty"); }
			result.push_back({pattern, label});
		}
	}
	return result;
}

void KeyFall::selectClip(Pattern* clip)
{
	for (const auto& c : m_clipConnections) { disconnect(c); }
	m_clipConnections.clear();
	m_clip = clip;
	if (clip)
	{
		// Note edits, resizing and moving the clip all change what has to fall
		m_clipConnections << connect(clip, &Model::dataChanged, this, &KeyFall::rebuildChart)
			<< connect(clip, &TrackContentObject::lengthChanged, this, &KeyFall::rebuildChart)
			<< connect(clip, &TrackContentObject::positionChanged, this, &KeyFall::rebuildChart)
			<< connect(clip, &QObject::destroyed, this, [this] { selectClip(nullptr); });
	}
	rebuildChart();
}

void KeyFall::rebuildChart()
{
	std::vector<kf::RawNote> raw;
	double length = 0, offset = 0;
	if (m_clip)
	{
		length = m_clip->length().getTicks();
		raw.reserve(m_clip->notes().size());
		for (const Note* n : m_clip->notes())
		{
			raw.push_back({static_cast<double>(n->pos().getTicks()), static_cast<double>(n->length().getTicks()),
				n->key(), n->midiVelocity(DefaultVolume)});
		}
	}
	m_chart = std::make_shared<const kf::Chart>(kf::Chart::build(raw, length, offset, &m_chartStats));
	if (m_currentLayoutId == AutoLayoutId) { applyLayout(AutoLayoutId, false); }
	emit chartChanged();
}

TransportSample KeyFall::sampleTransport() const
{
	TransportSample s;
	const Song* song = Engine::getSong();
	const float framesPerTick = std::max(1e-3f, Engine::framesPerTick());
	s.ticksPerSecond = Engine::mixer()->processingSampleRate() / framesPerTick;
	s.latencyTicks = Engine::mixer()->framesPerPeriod() / framesPerTick;
	s.ticksPerBar = std::max(1, song->ticksPerTact());
	s.beatsPerBar = std::max(1, const_cast<Song*>(song)->getTimeSigModel().getNumerator());

	const auto mode = song->playMode();
	const double clipStart = m_clip ? m_clip->startPosition().getTicks() : 0;
	const double clipOffset = 0;
	s.songTickOfChartZero = clipStart;

	if (mode == Song::Mode_PlayPattern)
	{
		// Piano Roll playback: the position is in the clip's note coordinates
		const auto& pos = song->getPlayPos(Song::Mode_PlayPattern);
		s.chartTick = pos.getTicks() + pos.currentFrame() / framesPerTick + clipOffset;
		s.playing = song->isPlaying();
	}
	else
	{
		const auto& pos = song->getPlayPos(Song::Mode_PlaySong);
		s.chartTick = pos.getTicks() + pos.currentFrame() / framesPerTick - clipStart;
		s.playing = song->isPlaying() && mode == Song::Mode_PlaySong;
		s.supportedMode = !(song->isPlaying() && mode == Song::Mode_PlayBB);
	}
	return s;
}

void KeyFall::setInputOffsetMs(double ms)
{
	m_inputOffsetMs = std::clamp(ms, -MaxInputOffsetMs, MaxInputOffsetMs);
	ConfigManager::inst()->setValue(ConfigSection, InputOffsetKey, QString::number(m_inputOffsetMs, 'f', 1));
}

void KeyFall::setLookaheadSeconds(double seconds)
{
	m_lookaheadSeconds = std::clamp(seconds, MinLookahead, MaxLookahead);
	ConfigManager::inst()->setValue(ConfigSection, LookaheadKey, QString::number(m_lookaheadSeconds, 'f', 2));
}

} // namespace lmms
