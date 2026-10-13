#include "Project.h"
#include <algorithm>
#include <atomic>

namespace studio
{

juce::String makeId (const juce::String& prefix)
{
    static std::atomic<juce::uint32> counter { 0 };
    auto r = juce::Random::getSystemRandom().nextInt64();
    return prefix + "_" + juce::String::toHexString ((juce::int64) (r ^ (juce::int64) ++counter)).substring (0, 10);
}

//==============================================================================
namespace
{
    template <typename E>
    struct EnumNames { std::vector<std::pair<E, const char*>> entries; };

    const std::vector<std::pair<TrackRole, const char*>>& roleNames()
    {
        static const std::vector<std::pair<TrackRole, const char*>> names {
            { TrackRole::Unknown, "unknown" }, { TrackRole::LeadVocal, "lead_vocal" },
            { TrackRole::BackingVocal, "backing_vocal" }, { TrackRole::Choir, "choir" },
            { TrackRole::Kick, "kick" }, { TrackRole::Snare, "snare" }, { TrackRole::HiHat, "hihat" },
            { TrackRole::Drums, "drums" }, { TrackRole::Percussion, "percussion" }, { TrackRole::Bass, "bass" },
            { TrackRole::Keys, "keys" }, { TrackRole::Guitar, "guitar" }, { TrackRole::Pad, "pad" },
            { TrackRole::Strings, "strings" }, { TrackRole::Brass, "brass" }, { TrackRole::Lead, "lead" },
            { TrackRole::Fx, "fx" }, { TrackRole::Click, "click" }
        };
        return names;
    }

    const std::vector<std::pair<SectionType, const char*>>& sectionNames()
    {
        static const std::vector<std::pair<SectionType, const char*>> names {
            { SectionType::Intro, "intro" }, { SectionType::Verse, "verse" },
            { SectionType::PreChorus, "pre-chorus" }, { SectionType::Chorus, "chorus" },
            { SectionType::Bridge, "bridge" }, { SectionType::Vamp, "vamp" },
            { SectionType::Instrumental, "instrumental" }, { SectionType::Outro, "outro" }
        };
        return names;
    }

    // --- var helpers ----------------------------------------------------------
    juce::DynamicObject::Ptr obj() { return new juce::DynamicObject(); }

    juce::var str (const juce::var& v, const char* key, const juce::String& fallback = {})
    {
        auto x = v.getProperty (key, {});
        return x.isVoid() ? juce::var (fallback) : x;
    }

    double num (const juce::var& v, const char* key, double fallback)
    {
        auto x = v.getProperty (key, {});
        return (x.isDouble() || x.isInt() || x.isInt64() || x.isBool()) ? (double) x : fallback;
    }

    bool flag (const juce::var& v, const char* key, bool fallback)
    {
        auto x = v.getProperty (key, {});
        return x.isVoid() ? fallback : (bool) x;
    }

    juce::var arrayOf (const juce::StringArray& a)
    {
        juce::Array<juce::var> out;
        for (auto& s : a) out.add (s);
        return out;
    }

    juce::StringArray stringsFrom (const juce::var& v, const char* key)
    {
        juce::StringArray out;
        if (auto* arr = v.getProperty (key, {}).getArray())
            for (auto& s : *arr) out.add (s.toString());
        return out;
    }

    juce::var floatMap (const std::map<juce::String, float>& m)
    {
        auto o = obj();
        for (auto& [k, val] : m) o->setProperty (k, val);
        return o.get();
    }

    std::map<juce::String, float> floatMapFrom (const juce::var& v)
    {
        std::map<juce::String, float> m;
        if (auto* o = v.getDynamicObject())
            for (auto& p : o->getProperties())
                m[p.name.toString()] = (float) (double) p.value;
        return m;
    }

    juce::var boolMap (const std::map<juce::String, bool>& m)
    {
        auto o = obj();
        for (auto& [k, val] : m) o->setProperty (k, val);
        return o.get();
    }

    std::map<juce::String, bool> boolMapFrom (const juce::var& v)
    {
        std::map<juce::String, bool> m;
        if (auto* o = v.getDynamicObject())
            for (auto& p : o->getProperties())
                m[p.name.toString()] = (bool) p.value;
        return m;
    }

    template <typename T, typename Fn>
    juce::var listOf (const std::vector<T>& items, Fn&& toVar)
    {
        juce::Array<juce::var> out;
        for (auto& i : items) out.add (toVar (i));
        return out;
    }

    template <typename T, typename Fn>
    std::vector<T> listFrom (const juce::var& v, const char* key, Fn&& fromVar)
    {
        std::vector<T> out;
        if (auto* arr = v.getProperty (key, {}).getArray())
            for (auto& item : *arr) out.push_back (fromVar (item));
        return out;
    }

    // --- entities ---------------------------------------------------------------
    juce::var noteToVar (const MidiNote& n)
    {
        // Compact array form keeps project.json small for dense MIDI.
        return juce::Array<juce::var> { n.pitch, n.velocity, n.startBeat, n.lengthBeats };
    }

    MidiNote noteFromVar (const juce::var& v)
    {
        MidiNote n;
        if (auto* a = v.getArray(); a != nullptr && a->size() >= 4)
        {
            n.pitch = (int) (*a)[0];
            n.velocity = (float) (double) (*a)[1];
            n.startBeat = (double) (*a)[2];
            n.lengthBeats = (double) (*a)[3];
        }
        return n;
    }

    juce::var clipToVar (const Clip& c)
    {
        auto o = obj();
        o->setProperty ("id", c.id);
        o->setProperty ("name", c.name);
        o->setProperty ("start", c.startBeat);
        o->setProperty ("length", c.lengthBeats);
        o->setProperty ("midi", c.isMidi);
        if (c.isMidi)
            o->setProperty ("notes", listOf (c.notes, noteToVar));
        else
        {
            o->setProperty ("file", c.audioFile);
            o->setProperty ("offset", c.sourceOffsetSeconds);
            o->setProperty ("fadeIn", c.fadeInBeats);
            o->setProperty ("fadeOut", c.fadeOutBeats);
            o->setProperty ("stretch", c.stretchRatio);
            o->setProperty ("pitch", c.pitchSemitones);
            if (c.takeId.isNotEmpty()) o->setProperty ("take", c.takeId);
        }
        o->setProperty ("gain", c.gainDb);
        return o.get();
    }

    Clip clipFromVar (const juce::var& v)
    {
        Clip c;
        c.id = str (v, "id", makeId ("clip"));
        c.name = str (v, "name");
        c.startBeat = num (v, "start", 0);
        c.lengthBeats = num (v, "length", 4);
        c.isMidi = flag (v, "midi", false);
        c.notes = listFrom<MidiNote> (v, "notes", noteFromVar);
        c.audioFile = str (v, "file");
        c.sourceOffsetSeconds = num (v, "offset", 0);
        c.fadeInBeats = num (v, "fadeIn", 0);
        c.fadeOutBeats = num (v, "fadeOut", 0);
        c.stretchRatio = num (v, "stretch", 1);
        c.pitchSemitones = num (v, "pitch", 0);
        c.takeId = str (v, "take");
        c.gainDb = (float) num (v, "gain", 0);
        return c;
    }

    juce::var takeToVar (const Take& t)
    {
        auto o = obj();
        o->setProperty ("id", t.id);
        o->setProperty ("file", t.audioFile);
        o->setProperty ("start", t.startBeat);
        o->setProperty ("length", t.lengthBeats);
        o->setProperty ("offset", t.sourceOffsetSeconds);
        o->setProperty ("recordedAt", t.recordedAt);
        o->setProperty ("rating", t.rating);
        return o.get();
    }

    Take takeFromVar (const juce::var& v)
    {
        Take t;
        t.id = str (v, "id", makeId ("take"));
        t.audioFile = str (v, "file");
        t.startBeat = num (v, "start", 0);
        t.lengthBeats = num (v, "length", 0);
        t.sourceOffsetSeconds = num (v, "offset", 0);
        t.recordedAt = str (v, "recordedAt");
        t.rating = (int) num (v, "rating", 0);
        return t;
    }

    juce::var effectToVar (const EffectSlot& e)
    {
        auto o = obj();
        o->setProperty ("id", e.id);
        o->setProperty ("type", e.type);
        o->setProperty ("bypassed", e.bypassed);
        o->setProperty ("params", floatMap (e.params));
        if (e.pluginIdentifier.isNotEmpty()) o->setProperty ("plugin", e.pluginIdentifier);
        if (e.pluginState.isNotEmpty()) o->setProperty ("pluginState", e.pluginState);
        if (e.sidechainTrackId.isNotEmpty()) o->setProperty ("sidechain", e.sidechainTrackId);
        return o.get();
    }

    EffectSlot effectFromVar (const juce::var& v)
    {
        EffectSlot e;
        e.id = str (v, "id", makeId ("fx"));
        e.type = str (v, "type");
        e.bypassed = flag (v, "bypassed", false);
        e.params = floatMapFrom (v.getProperty ("params", {}));
        e.pluginIdentifier = str (v, "plugin");
        e.pluginState = str (v, "pluginState");
        e.sidechainTrackId = str (v, "sidechain");
        return e;
    }

    juce::var laneToVar (const AutomationLane& l)
    {
        auto o = obj();
        o->setProperty ("parameter", l.parameter);
        o->setProperty ("enabled", l.enabled);
        o->setProperty ("points", listOf (l.points, [] (const AutomationPoint& p) {
            return juce::var (juce::Array<juce::var> { p.beat, p.value });
        }));
        return o.get();
    }

    AutomationLane laneFromVar (const juce::var& v)
    {
        AutomationLane l;
        l.parameter = str (v, "parameter");
        l.enabled = flag (v, "enabled", true);
        l.points = listFrom<AutomationPoint> (v, "points", [] (const juce::var& p) {
            AutomationPoint ap;
            if (auto* a = p.getArray(); a != nullptr && a->size() >= 2)
            {
                ap.beat = (double) (*a)[0];
                ap.value = (float) (double) (*a)[1];
            }
            return ap;
        });
        return l;
    }

    juce::var trackToVar (const Track& t)
    {
        auto o = obj();
        o->setProperty ("id", t.id);
        o->setProperty ("name", t.name);
        o->setProperty ("type", toString (t.type));
        o->setProperty ("role", toString (t.role));
        o->setProperty ("colour", juce::String::toHexString ((int) t.colour));
        o->setProperty ("folder", t.folder);
        o->setProperty ("output", t.outputBusId);
        o->setProperty ("volume", t.volumeDb);
        o->setProperty ("pan", t.pan);
        o->setProperty ("mute", t.mute);
        o->setProperty ("solo", t.solo);
        o->setProperty ("armed", t.armed);
        o->setProperty ("monitor", t.monitorInput);
        o->setProperty ("input", t.inputChannel);
        o->setProperty ("inputChannels", t.inputChannels);

        auto inst = obj();
        inst->setProperty ("id", t.instrument.id);
        inst->setProperty ("params", floatMap (t.instrument.params));
        o->setProperty ("instrument", inst.get());

        o->setProperty ("effects", listOf (t.effects, effectToVar));
        o->setProperty ("sends", listOf (t.sends, [] (const Send& s) {
            auto so = obj();
            so->setProperty ("bus", s.busId);
            so->setProperty ("level", s.levelDb);
            so->setProperty ("pre", s.preFader);
            return juce::var (so.get());
        }));
        o->setProperty ("clips", listOf (t.clips, clipToVar));
        o->setProperty ("takes", listOf (t.takes, takeToVar));
        o->setProperty ("comp", listOf (t.comp, [] (const CompSegment& c) {
            auto co = obj();
            co->setProperty ("take", c.takeId);
            co->setProperty ("start", c.startBeat);
            co->setProperty ("end", c.endBeat);
            return juce::var (co.get());
        }));
        o->setProperty ("automation", listOf (t.automation, laneToVar));

        if (t.generator.part.isNotEmpty())
        {
            auto g = obj();
            g->setProperty ("part", t.generator.part);
            g->setProperty ("seed", t.generator.seed);
            g->setProperty ("variation", t.generator.variation);
            g->setProperty ("density", t.generator.density);
            g->setProperty ("feel", t.generator.feel);
            o->setProperty ("generator", g.get());
        }

        auto vc = obj();
        vc->setProperty ("enabled", t.vocalChain.enabled);
        vc->setProperty ("preset", t.vocalChain.preset);
        vc->setProperty ("amount", t.vocalChain.amount);
        vc->setProperty ("bypass", boolMap (t.vocalChain.bypass));
        vc->setProperty ("params", floatMap (t.vocalChain.params));
        o->setProperty ("vocalChain", vc.get());
        return o.get();
    }

    Track trackFromVar (const juce::var& v)
    {
        Track t;
        t.id = str (v, "id", makeId ("trk"));
        t.name = str (v, "name", "Track");
        t.type = trackTypeFromString (str (v, "type", "audio"));
        t.role = trackRoleFromString (str (v, "role", "unknown"));
        t.colour = (juce::uint32) str (v, "colour", "ff4f8cff").toString().getHexValue64();
        t.folder = str (v, "folder");
        t.outputBusId = str (v, "output");
        t.volumeDb = (float) num (v, "volume", 0);
        t.pan = (float) num (v, "pan", 0);
        t.mute = flag (v, "mute", false);
        t.solo = flag (v, "solo", false);
        t.armed = flag (v, "armed", false);
        t.monitorInput = flag (v, "monitor", false);
        t.inputChannel = (int) num (v, "input", 0);
        t.inputChannels = (int) num (v, "inputChannels", 1);

        auto inst = v.getProperty ("instrument", {});
        t.instrument.id = str (inst, "id");
        t.instrument.params = floatMapFrom (inst.getProperty ("params", {}));

        t.effects = listFrom<EffectSlot> (v, "effects", effectFromVar);
        t.sends = listFrom<Send> (v, "sends", [] (const juce::var& s) {
            Send send;
            send.busId = str (s, "bus");
            send.levelDb = (float) num (s, "level", -12);
            send.preFader = flag (s, "pre", false);
            return send;
        });
        t.clips = listFrom<Clip> (v, "clips", clipFromVar);
        t.takes = listFrom<Take> (v, "takes", takeFromVar);
        t.comp = listFrom<CompSegment> (v, "comp", [] (const juce::var& c) {
            return CompSegment { str (c, "take"), num (c, "start", 0), num (c, "end", 0) };
        });
        t.automation = listFrom<AutomationLane> (v, "automation", laneFromVar);

        auto g = v.getProperty ("generator", {});
        t.generator.part = str (g, "part");
        t.generator.seed = (juce::int64) num (g, "seed", 0);
        t.generator.variation = (juce::int64) num (g, "variation", 0);
        t.generator.density = (float) num (g, "density", 0.5);
        t.generator.feel = (int) num (g, "feel", 0);

        auto vc = v.getProperty ("vocalChain", {});
        t.vocalChain.enabled = flag (vc, "enabled", false);
        t.vocalChain.preset = str (vc, "preset", "Worship lead");
        t.vocalChain.amount = (float) num (vc, "amount", 0.7);
        t.vocalChain.bypass = boolMapFrom (vc.getProperty ("bypass", {}));
        t.vocalChain.params = floatMapFrom (vc.getProperty ("params", {}));
        return t;
    }

    juce::var sectionToVar (const Section& s)
    {
        auto o = obj();
        o->setProperty ("id", s.id);
        o->setProperty ("type", toString (s.type));
        o->setProperty ("name", s.name);
        o->setProperty ("start", s.startBeat);
        o->setProperty ("length", s.lengthBeats);
        o->setProperty ("energy", s.energy);
        return o.get();
    }

    Section sectionFromVar (const juce::var& v)
    {
        Section s;
        s.id = str (v, "id", makeId ("sec"));
        s.type = sectionTypeFromString (str (v, "type", "verse"));
        s.name = str (v, "name", toString (s.type));
        s.startBeat = num (v, "start", 0);
        s.lengthBeats = num (v, "length", 32);
        s.energy = (float) num (v, "energy", 0.5);
        return s;
    }

    juce::var releaseToVar (const ReleaseMetadata& r)
    {
        auto o = obj();
        o->setProperty ("releaseType", r.releaseType);
        o->setProperty ("title", r.title);
        o->setProperty ("primaryArtist", r.primaryArtist);
        o->setProperty ("featuredArtists", arrayOf (r.featuredArtists));
        o->setProperty ("songwriters", arrayOf (r.songwriters));
        o->setProperty ("composers", arrayOf (r.composers));
        o->setProperty ("producers", arrayOf (r.producers));
        o->setProperty ("genre", r.genre);
        o->setProperty ("subgenre", r.subgenre);
        o->setProperty ("language", r.language);
        o->setProperty ("explicit", r.explicitContent);
        o->setProperty ("releaseDate", r.releaseDate);
        o->setProperty ("originalReleaseDate", r.originalReleaseDate);
        o->setProperty ("isrc", r.isrc);
        o->setProperty ("upc", r.upc);
        o->setProperty ("label", r.label);
        o->setProperty ("copyright", r.copyrightLine);
        o->setProperty ("lyrics", r.lyrics);
        o->setProperty ("syncedLyrics", listOf (r.syncedLyrics, [] (const SyncedLyricLine& l) {
            return juce::var (juce::Array<juce::var> { l.timeSeconds, l.text });
        }));
        o->setProperty ("artwork", r.artworkFile);
        o->setProperty ("splits", listOf (r.splits, [] (const Split& s) {
            auto so = obj();
            so->setProperty ("name", s.name);
            so->setProperty ("role", s.role);
            so->setProperty ("percent", s.percent);
            so->setProperty ("email", s.email);
            return juce::var (so.get());
        }));
        return o.get();
    }

    ReleaseMetadata releaseFromVar (const juce::var& v)
    {
        ReleaseMetadata r;
        r.releaseType = str (v, "releaseType", "single");
        r.title = str (v, "title");
        r.primaryArtist = str (v, "primaryArtist");
        r.featuredArtists = stringsFrom (v, "featuredArtists");
        r.songwriters = stringsFrom (v, "songwriters");
        r.composers = stringsFrom (v, "composers");
        r.producers = stringsFrom (v, "producers");
        r.genre = str (v, "genre", r.genre);
        r.subgenre = str (v, "subgenre");
        r.language = str (v, "language", "English");
        r.explicitContent = flag (v, "explicit", false);
        r.releaseDate = str (v, "releaseDate");
        r.originalReleaseDate = str (v, "originalReleaseDate");
        r.isrc = str (v, "isrc");
        r.upc = str (v, "upc");
        r.label = str (v, "label");
        r.copyrightLine = str (v, "copyright");
        r.lyrics = str (v, "lyrics");
        r.syncedLyrics = listFrom<SyncedLyricLine> (v, "syncedLyrics", [] (const juce::var& l) {
            SyncedLyricLine line;
            if (auto* a = l.getArray(); a != nullptr && a->size() >= 2)
            {
                line.timeSeconds = (double) (*a)[0];
                line.text = (*a)[1].toString();
            }
            return line;
        });
        r.artworkFile = str (v, "artwork");
        r.splits = listFrom<Split> (v, "splits", [] (const juce::var& s) {
            Split sp;
            sp.name = str (s, "name");
            sp.role = str (s, "role");
            sp.percent = num (s, "percent", 0);
            sp.email = str (s, "email");
            return sp;
        });
        return r;
    }
}

//==============================================================================
juce::String toString (TrackType t)
{
    switch (t)
    {
        case TrackType::Audio: return "audio";
        case TrackType::Midi:  return "midi";
        case TrackType::Bus:   return "bus";
    }
    return "audio";
}

TrackType trackTypeFromString (const juce::String& s)
{
    if (s == "midi") return TrackType::Midi;
    if (s == "bus")  return TrackType::Bus;
    return TrackType::Audio;
}

juce::String toString (TrackRole r)
{
    for (auto& [role, name] : roleNames())
        if (role == r) return name;
    return "unknown";
}

TrackRole trackRoleFromString (const juce::String& s)
{
    for (auto& [role, name] : roleNames())
        if (s == name) return role;
    return TrackRole::Unknown;
}

bool isVocalRole (TrackRole r)
{
    return r == TrackRole::LeadVocal || r == TrackRole::BackingVocal || r == TrackRole::Choir;
}

juce::String toString (SectionType t)
{
    for (auto& [type, name] : sectionNames())
        if (type == t) return name;
    return "verse";
}

SectionType sectionTypeFromString (const juce::String& s)
{
    auto lower = s.toLowerCase();
    for (auto& [type, name] : sectionNames())
        if (lower == name) return type;
    if (lower == "prechorus") return SectionType::PreChorus;
    return SectionType::Verse;
}

//==============================================================================
float AutomationLane::valueAt (double beat, float fallback) const
{
    if (points.empty() || ! enabled)
        return fallback;
    if (beat <= points.front().beat) return points.front().value;
    if (beat >= points.back().beat)  return points.back().value;

    auto it = std::upper_bound (points.begin(), points.end(), beat,
                                [] (double b, const AutomationPoint& p) { return b < p.beat; });
    auto& b = *it;
    auto& a = *(it - 1);
    auto t = (beat - a.beat) / juce::jmax (1.0e-9, b.beat - a.beat);
    return (float) (a.value + (b.value - a.value) * t);
}

Clip* Track::findClip (const juce::String& clipId)
{
    for (auto& c : clips)
        if (c.id == clipId) return &c;
    return nullptr;
}

AutomationLane& Track::automationFor (const juce::String& parameter)
{
    for (auto& l : automation)
        if (l.parameter == parameter) return l;
    automation.push_back ({ parameter, {}, true });
    return automation.back();
}

//==============================================================================
Track* Project::findTrack (const juce::String& trackId)
{
    for (auto& t : tracks)
        if (t.id == trackId) return &t;
    return nullptr;
}

const Track* Project::findTrack (const juce::String& trackId) const
{
    for (auto& t : tracks)
        if (t.id == trackId) return &t;
    return nullptr;
}

Track& Project::addTrack (TrackType type, const juce::String& trackName, TrackRole role)
{
    static const juce::uint32 palette[] { 0xff4f8cff, 0xffff6b6b, 0xffffc145, 0xff4cd4a0, 0xffb07cff,
                                          0xff45c8ff, 0xffff8f45, 0xffe86bd1, 0xff9bd45c, 0xffd4b25c };
    Track t;
    t.id = makeId ("trk");
    t.name = trackName;
    t.type = type;
    t.role = role;
    t.colour = palette[tracks.size() % std::size (palette)];
    tracks.push_back (std::move (t));
    return tracks.back();
}

bool Project::removeTrack (const juce::String& trackId)
{
    auto it = std::remove_if (tracks.begin(), tracks.end(), [&] (auto& t) { return t.id == trackId; });
    if (it == tracks.end())
        return false;
    tracks.erase (it, tracks.end());

    // Anything routed to a removed bus falls back to the master.
    for (auto& t : tracks)
    {
        if (t.outputBusId == trackId) t.outputBusId.clear();
        t.sends.erase (std::remove_if (t.sends.begin(), t.sends.end(), [&] (auto& s) { return s.busId == trackId; }), t.sends.end());
    }
    return true;
}

std::vector<const Track*> Project::buses() const
{
    std::vector<const Track*> out;
    for (auto& t : tracks)
        if (t.type == TrackType::Bus) out.push_back (&t);
    return out;
}

Section* Project::findSection (const juce::String& sectionId)
{
    for (auto& s : sections)
        if (s.id == sectionId) return &s;
    return nullptr;
}

const Section* Project::sectionAt (double beat) const
{
    for (auto& s : sections)
        if (beat >= s.startBeat && beat < s.endBeat()) return &s;
    return nullptr;
}

juce::String Project::chordAt (double beat) const
{
    for (auto& c : chords)
        if (beat >= c.beat && beat < c.beat + c.lengthBeats) return c.chord;
    return {};
}

Key Project::keyAt (double beat) const
{
    Key k = key;
    double best = -1;
    for (auto& e : keyEvents)
        if (e.beat <= beat && e.beat >= best) { k = e.key; best = e.beat; }
    return k;
}

double Project::lengthInBeats() const
{
    double end = 0;
    for (auto& s : sections) end = std::max (end, s.endBeat());
    for (auto& t : tracks)
        for (auto& c : t.clips) end = std::max (end, c.endBeat());
    return end;
}

juce::var Project::toVar() const
{
    auto o = obj();
    o->setProperty ("format", "studio-project");
    o->setProperty ("version", 1);
    o->setProperty ("id", id);
    o->setProperty ("name", name);
    o->setProperty ("bpm", bpm);
    o->setProperty ("timeSignature", juce::Array<juce::var> { timeSigNumerator, timeSigDenominator });
    o->setProperty ("key", key.toString());
    o->setProperty ("style", style);
    o->setProperty ("mood", mood);
    o->setProperty ("sampleRate", sampleRate);
    o->setProperty ("guidedMode", guidedMode);
    o->setProperty ("guidedStep", guidedStep);
    o->setProperty ("createdAt", createdAt);
    o->setProperty ("modifiedAt", modifiedAt);

    o->setProperty ("tempoEvents", listOf (tempoEvents, [] (const TempoEvent& e) {
        return juce::var (juce::Array<juce::var> { e.beat, e.bpm });
    }));
    o->setProperty ("keyEvents", listOf (keyEvents, [] (const KeyEvent& e) {
        return juce::var (juce::Array<juce::var> { e.beat, e.key.toString() });
    }));
    o->setProperty ("sections", listOf (sections, sectionToVar));
    o->setProperty ("chords", listOf (chords, [] (const ChordEvent& e) {
        return juce::var (juce::Array<juce::var> { e.beat, e.lengthBeats, e.chord });
    }));
    o->setProperty ("tracks", listOf (tracks, trackToVar));
    o->setProperty ("markers", listOf (markers, [] (const Marker& m) {
        return juce::var (juce::Array<juce::var> { m.beat, m.name });
    }));
    o->setProperty ("comments", listOf (comments, [] (const TimelineComment& c) {
        auto co = obj();
        co->setProperty ("id", c.id);
        co->setProperty ("beat", c.beat);
        co->setProperty ("author", c.author);
        co->setProperty ("text", c.text);
        co->setProperty ("createdAt", c.createdAt);
        co->setProperty ("resolved", c.resolved);
        return juce::var (co.get());
    }));
    o->setProperty ("lyricSheet", listOf (lyricSheet, [] (const LyricSection& s) {
        auto so = obj();
        so->setProperty ("name", s.name);
        so->setProperty ("lines", listOf (s.lines, [] (const LyricLine& l) {
            auto lo = obj();
            lo->setProperty ("text", l.text);
            lo->setProperty ("chords", l.chords);
            lo->setProperty ("memo", l.memoFile);
            return juce::var (lo.get());
        }));
        return juce::var (so.get());
    }));

    auto m = obj();
    m->setProperty ("preset", mastering.preset);
    m->setProperty ("targetLufs", mastering.targetLufs);
    m->setProperty ("ceilingDbtp", mastering.ceilingDbtp);
    m->setProperty ("enabled", mastering.enabled);
    m->setProperty ("params", floatMap (mastering.params));
    o->setProperty ("mastering", m.get());

    o->setProperty ("release", releaseToVar (release));
    return o.get();
}

Project Project::fromVar (const juce::var& v)
{
    Project p;
    p.id = str (v, "id", makeId ("prj"));
    p.name = str (v, "name", "Untitled");
    p.bpm = num (v, "bpm", 120);
    if (auto* ts = v.getProperty ("timeSignature", {}).getArray(); ts != nullptr && ts->size() == 2)
    {
        p.timeSigNumerator = juce::jlimit (1, 16, (int) (*ts)[0]);
        p.timeSigDenominator = juce::jlimit (1, 16, (int) (*ts)[1]);
    }
    p.key = Key::fromString (str (v, "key", "C major"));
    p.style = str (v, "style");
    p.mood = str (v, "mood", "joyful");
    p.sampleRate = num (v, "sampleRate", 48000);
    p.guidedMode = flag (v, "guidedMode", true);
    p.guidedStep = (int) num (v, "guidedStep", 0);
    p.createdAt = str (v, "createdAt");
    p.modifiedAt = str (v, "modifiedAt");

    p.tempoEvents = listFrom<TempoEvent> (v, "tempoEvents", [] (const juce::var& e) {
        TempoEvent t;
        if (auto* a = e.getArray(); a != nullptr && a->size() >= 2) { t.beat = (*a)[0]; t.bpm = (*a)[1]; }
        return t;
    });
    p.keyEvents = listFrom<KeyEvent> (v, "keyEvents", [] (const juce::var& e) {
        KeyEvent k;
        if (auto* a = e.getArray(); a != nullptr && a->size() >= 2) { k.beat = (*a)[0]; k.key = Key::fromString ((*a)[1].toString()); }
        return k;
    });
    p.sections = listFrom<Section> (v, "sections", sectionFromVar);
    p.chords = listFrom<ChordEvent> (v, "chords", [] (const juce::var& e) {
        ChordEvent ce;
        if (auto* a = e.getArray(); a != nullptr && a->size() >= 3) { ce.beat = (*a)[0]; ce.lengthBeats = (*a)[1]; ce.chord = (*a)[2].toString(); }
        return ce;
    });
    p.tracks = listFrom<Track> (v, "tracks", trackFromVar);
    p.markers = listFrom<Marker> (v, "markers", [] (const juce::var& e) {
        Marker m;
        if (auto* a = e.getArray(); a != nullptr && a->size() >= 2) { m.beat = (*a)[0]; m.name = (*a)[1].toString(); }
        return m;
    });
    p.comments = listFrom<TimelineComment> (v, "comments", [] (const juce::var& c) {
        TimelineComment tc;
        tc.id = str (c, "id", makeId ("cmt"));
        tc.beat = num (c, "beat", 0);
        tc.author = str (c, "author");
        tc.text = str (c, "text");
        tc.createdAt = str (c, "createdAt");
        tc.resolved = flag (c, "resolved", false);
        return tc;
    });
    p.lyricSheet = listFrom<LyricSection> (v, "lyricSheet", [] (const juce::var& s) {
        LyricSection ls;
        ls.name = str (s, "name");
        ls.lines = listFrom<LyricLine> (s, "lines", [] (const juce::var& l) {
            return LyricLine { str (l, "text"), str (l, "chords"), str (l, "memo") };
        });
        return ls;
    });

    auto m = v.getProperty ("mastering", {});
    p.mastering.preset = str (m, "preset", "Warm");
    p.mastering.targetLufs = (float) num (m, "targetLufs", -14);
    p.mastering.ceilingDbtp = (float) num (m, "ceilingDbtp", -1);
    p.mastering.enabled = flag (m, "enabled", true);
    p.mastering.params = floatMapFrom (m.getProperty ("params", {}));

    p.release = releaseFromVar (v.getProperty ("release", {}));
    return p;
}

juce::String Project::toJson() const
{
    return juce::JSON::toString (toVar(), false);
}

Project Project::fromJson (const juce::String& json)
{
    return fromVar (juce::JSON::parse (json));
}

} // namespace studio
