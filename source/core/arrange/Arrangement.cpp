#include "Arrangement.h"
#include "../model/TempoMap.h"
#include <algorithm>

namespace studio::arrange
{

namespace
{
    double beatToSeconds (const Project& p, double beat) { return TempoMap (p).beatsToSeconds (beat); }

    /** Splits a clip into [start, beat) and [beat, end); returns the right part. */
    Clip rightPart (const Project& p, Clip& c, double beat)
    {
        Clip r = c;
        r.id = makeId ("clip");
        const double cut = beat - c.startBeat;
        r.startBeat = beat;
        r.lengthBeats = c.lengthBeats - cut;
        r.fadeInBeats = 0;
        c.fadeOutBeats = 0;
        c.lengthBeats = cut;

        if (c.isMidi)
        {
            std::vector<MidiNote> left, right;
            for (auto& n : c.notes)
            {
                if (n.startBeat < cut)
                {
                    auto ln = n;
                    ln.lengthBeats = std::min (n.lengthBeats, cut - n.startBeat);
                    left.push_back (ln);
                }
                else
                {
                    auto rn = n;
                    rn.startBeat -= cut;
                    right.push_back (rn);
                }
            }
            c.notes = left;
            r.notes = right;
        }
        else
        {
            // Source offset advances by the real time between the clip start and the cut.
            r.sourceOffsetSeconds = c.sourceOffsetSeconds + (beatToSeconds (p, beat) - beatToSeconds (p, c.startBeat)) / c.stretchRatio;
        }
        return r;
    }

    void shiftLanes (std::vector<AutomationLane>& lanes, double at, double delta)
    {
        for (auto& l : lanes)
            for (auto& pt : l.points)
                if (pt.beat >= at) pt.beat += delta;
    }
}

//==============================================================================
juce::String splitClip (Project& p, Track& t, const juce::String& clipId, double beat)
{
    auto* c = t.findClip (clipId);
    if (c == nullptr || beat <= c->startBeat + 1.0e-6 || beat >= c->endBeat() - 1.0e-6)
        return {};
    auto r = rightPart (p, *c, beat);
    auto id = r.id;
    t.clips.push_back (std::move (r));
    return id;
}

void splitAt (Project& p, double beat)
{
    for (auto& t : p.tracks)
    {
        std::vector<juce::String> ids;
        for (auto& c : t.clips)
            if (beat > c.startBeat + 1.0e-6 && beat < c.endBeat() - 1.0e-6) ids.push_back (c.id);
        for (auto& id : ids) splitClip (p, t, id, beat);
    }
    for (size_t i = 0; i < p.chords.size(); ++i)
    {
        auto& ch = p.chords[i];
        if (beat > ch.beat && beat < ch.beat + ch.lengthBeats)
        {
            ChordEvent r = ch;
            r.beat = beat;
            r.lengthBeats = ch.beat + ch.lengthBeats - beat;
            ch.lengthBeats = beat - ch.beat;
            p.chords.push_back (r);
        }
    }
}

void insertTime (Project& p, double at, double len)
{
    if (len <= 0) return;
    splitAt (p, at);
    for (auto& t : p.tracks)
    {
        for (auto& c : t.clips) if (c.startBeat >= at - 1.0e-6) c.startBeat += len;
        for (auto& tk : t.takes) if (tk.startBeat >= at - 1.0e-6) tk.startBeat += len;
        for (auto& cs : t.comp) if (cs.startBeat >= at - 1.0e-6) { cs.startBeat += len; cs.endBeat += len; }
        shiftLanes (t.automation, at, len);
    }
    for (auto& s : p.sections)
    {
        if (s.startBeat >= at - 1.0e-6) s.startBeat += len;
        else if (s.endBeat() > at) s.lengthBeats += len; // inserting inside a section grows it
    }
    for (auto& c : p.chords) if (c.beat >= at - 1.0e-6) c.beat += len;
    for (auto& m : p.markers) if (m.beat >= at) m.beat += len;
    for (auto& c : p.comments) if (c.beat >= at) c.beat += len;
    for (auto& e : p.tempoEvents) if (e.beat >= at && e.beat > 0) e.beat += len;
    for (auto& e : p.keyEvents) if (e.beat >= at && e.beat > 0) e.beat += len;
}

void removeTime (Project& p, double start, double end)
{
    const double len = end - start;
    if (len <= 0) return;
    splitAt (p, start);
    splitAt (p, end);

    auto inside = [&] (double s, double e) { return s >= start - 1.0e-6 && e <= end + 1.0e-6; };
    for (auto& t : p.tracks)
    {
        t.clips.erase (std::remove_if (t.clips.begin(), t.clips.end(), [&] (auto& c) { return inside (c.startBeat, c.endBeat()); }), t.clips.end());
        for (auto& c : t.clips) if (c.startBeat >= end - 1.0e-6) c.startBeat -= len;
        t.comp.erase (std::remove_if (t.comp.begin(), t.comp.end(), [&] (auto& c) { return inside (c.startBeat, c.endBeat); }), t.comp.end());
        for (auto& c : t.comp) if (c.startBeat >= end - 1.0e-6) { c.startBeat -= len; c.endBeat -= len; }
        for (auto& tk : t.takes) if (tk.startBeat >= end - 1.0e-6) tk.startBeat -= len;
        for (auto& l : t.automation)
        {
            l.points.erase (std::remove_if (l.points.begin(), l.points.end(), [&] (auto& pt) { return pt.beat > start && pt.beat < end; }), l.points.end());
            for (auto& pt : l.points) if (pt.beat >= end) pt.beat -= len;
        }
    }

    p.chords.erase (std::remove_if (p.chords.begin(), p.chords.end(), [&] (auto& c) { return inside (c.beat, c.beat + c.lengthBeats); }), p.chords.end());
    for (auto& c : p.chords) if (c.beat >= end - 1.0e-6) c.beat -= len;

    for (auto& s : p.sections)
    {
        const double s0 = s.startBeat, s1 = s.endBeat();
        const double overlap = std::max (0.0, std::min (s1, end) - std::max (s0, start));
        if (s0 >= end - 1.0e-6) s.startBeat -= len;
        else if (s0 < start && s1 > start) s.lengthBeats -= overlap;
        else if (s0 >= start && s0 < end) { s.startBeat = start; s.lengthBeats -= overlap; }
    }
    p.sections.erase (std::remove_if (p.sections.begin(), p.sections.end(), [] (auto& s) { return s.lengthBeats < 0.5; }), p.sections.end());

    auto shiftSimple = [&] (double& b) { if (b >= end) b -= len; else if (b > start) b = start; };
    for (auto& m : p.markers) shiftSimple (m.beat);
    for (auto& c : p.comments) shiftSimple (c.beat);
    for (auto& e : p.tempoEvents) if (e.beat > 0) shiftSimple (e.beat);
    for (auto& e : p.keyEvents) if (e.beat > 0) shiftSimple (e.beat);
}

Region copyRegion (const Project& p, double start, double end)
{
    Project tmp = p; // split a copy so the source is untouched
    splitAt (tmp, start);
    splitAt (tmp, end);

    Region r;
    r.lengthBeats = end - start;
    for (auto& t : tmp.tracks)
    {
        for (auto& c : t.clips)
            if (c.startBeat >= start - 1.0e-6 && c.endBeat() <= end + 1.0e-6)
            {
                auto cc = c;
                cc.startBeat -= start;
                r.clips[t.id].push_back (cc);
            }
        for (auto& l : t.automation)
        {
            AutomationLane copy = l;
            copy.points.clear();
            for (auto& pt : l.points)
                if (pt.beat >= start && pt.beat <= end) copy.points.push_back ({ pt.beat - start, pt.value });
            if (! copy.points.empty()) r.automation[t.id].push_back (copy);
        }
    }
    for (auto& c : tmp.chords)
        if (c.beat >= start - 1.0e-6 && c.beat < end)
        {
            auto cc = c;
            cc.beat -= start;
            cc.lengthBeats = std::min (cc.lengthBeats, end - c.beat);
            r.chords.push_back (cc);
        }
    return r;
}

void pasteRegion (Project& p, const Region& r, double at)
{
    for (auto& [trackId, clips] : r.clips)
        if (auto* t = p.findTrack (trackId))
            for (auto c : clips)
            {
                c.id = makeId ("clip");
                c.startBeat += at;
                t->clips.push_back (c);
            }
    for (auto& [trackId, lanes] : r.automation)
        if (auto* t = p.findTrack (trackId))
            for (auto& l : lanes)
            {
                auto& dest = t->automationFor (l.parameter);
                for (auto& pt : l.points) dest.points.push_back ({ pt.beat + at, pt.value });
                std::sort (dest.points.begin(), dest.points.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
            }
    for (auto c : r.chords)
    {
        c.beat += at;
        p.chords.push_back (c);
    }
    std::sort (p.chords.begin(), p.chords.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
}

//==============================================================================
void sortSections (Project& p)
{
    std::sort (p.sections.begin(), p.sections.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
}

juce::String sectionLabel (const Project& p, const Section& s)
{
    if (s.name.isNotEmpty() && s.name != toString (s.type))
        return s.name;
    int count = 0, index = 0;
    for (auto& o : p.sections)
        if (o.type == s.type)
        {
            ++count;
            if (o.id == s.id) index = count;
        }
    static const std::map<SectionType, juce::String> names {
        { SectionType::Intro, "Intro" }, { SectionType::Verse, "Verse" }, { SectionType::PreChorus, "Pre-chorus" },
        { SectionType::Chorus, "Chorus" }, { SectionType::Bridge, "Bridge" }, { SectionType::Vamp, "Vamp" },
        { SectionType::Instrumental, "Instrumental" }, { SectionType::Outro, "Outro" }
    };
    auto base = names.at (s.type);
    return count > 1 && s.type != SectionType::Chorus ? base + " " + juce::String (index) : base;
}

juce::String duplicateSection (Project& p, const juce::String& id)
{
    auto* s = p.findSection (id);
    if (s == nullptr) return {};
    const Section original = *s;
    auto region = copyRegion (p, original.startBeat, original.endBeat());
    insertTime (p, original.endBeat(), original.lengthBeats);
    pasteRegion (p, region, original.endBeat());

    Section copy = original;
    copy.id = makeId ("sec");
    copy.startBeat = original.endBeat();
    copy.name = toString (copy.type);
    // insertTime grew nothing (insert at the boundary); add the new section.
    p.sections.push_back (copy);
    sortSections (p);
    return copy.id;
}

void moveSection (Project& p, const juce::String& id, int newIndex)
{
    sortSections (p);
    auto it = std::find_if (p.sections.begin(), p.sections.end(), [&] (auto& s) { return s.id == id; });
    if (it == p.sections.end()) return;
    const int oldIndex = (int) (it - p.sections.begin());
    newIndex = juce::jlimit (0, (int) p.sections.size() - 1, newIndex);
    if (newIndex == oldIndex) return;

    const Section sec = *it;
    auto region = copyRegion (p, sec.startBeat, sec.endBeat());
    removeTime (p, sec.startBeat, sec.endBeat());
    p.sections.erase (std::remove_if (p.sections.begin(), p.sections.end(), [&] (auto& s) { return s.id == id; }), p.sections.end());
    sortSections (p);

    const double at = newIndex >= (int) p.sections.size() ? (p.sections.empty() ? 0.0 : p.sections.back().endBeat())
                                                          : p.sections[(size_t) newIndex].startBeat;
    // Insert empty time exactly at a section boundary, then put the content back.
    for (auto& s : p.sections) if (s.startBeat >= at - 1.0e-6) s.startBeat += sec.lengthBeats;
    {
        // Shift everything else (clips, chords, ...) without growing sections.
        auto saved = p.sections;
        insertTime (p, at, sec.lengthBeats);
        p.sections = saved;
    }
    pasteRegion (p, region, at);
    Section moved = sec;
    moved.startBeat = at;
    p.sections.push_back (moved);
    sortSections (p);
}

void resizeSection (Project& p, const juce::String& id, double newLength)
{
    auto* s = p.findSection (id);
    if (s == nullptr || newLength < 1) return;
    const double end = s->endBeat();
    const double delta = newLength - s->lengthBeats;
    if (delta > 0)
    {
        auto saved = p.sections;
        insertTime (p, end, delta);
        p.sections = saved;
        for (auto& o : p.sections)
        {
            if (o.id == id) o.lengthBeats = newLength;
            else if (o.startBeat >= end - 1.0e-6) o.startBeat += delta;
        }
    }
    else if (delta < 0)
        removeTime (p, end + delta, end);
}

void deleteSection (Project& p, const juce::String& id)
{
    auto* s = p.findSection (id);
    if (s == nullptr) return;
    removeTime (p, s->startBeat, s->endBeat());
}

std::vector<juce::String> buildVamp (Project& p, const juce::String& chorusId, const VampOptions& opt)
{
    std::vector<juce::String> created;
    auto* ch = p.findSection (chorusId);
    if (ch == nullptr) return created;
    const Section chorus = *ch;
    auto region = copyRegion (p, chorus.startBeat, chorus.endBeat());

    double at = chorus.endBeat();
    for (int r = 0; r < opt.repeats; ++r)
    {
        auto saved = p.sections;
        insertTime (p, at, chorus.lengthBeats);
        p.sections = saved;
        for (auto& s : p.sections) if (s.startBeat >= at - 1.0e-6) s.startBeat += chorus.lengthBeats;

        auto rep = region;
        const float lift = 1.0f + 0.07f * (float) (r + 1);
        const bool lastWithKey = opt.keyLiftOnLast && r == opt.repeats - 1;
        for (auto& [trackId, clips] : rep.clips)
        {
            const auto* t = p.findTrack (trackId);
            const bool drums = t != nullptr && (t->role == TrackRole::Drums || t->role == TrackRole::Percussion
                                                || t->instrument.id.startsWith ("drums_") || t->instrument.id == "african_percussion");
            for (auto& c : clips)
            {
                if (! c.isMidi) continue;
                for (auto& n : c.notes)
                {
                    n.velocity = juce::jlimit (0.05f, 1.0f, n.velocity * lift);
                    if (lastWithKey && ! drums) n.pitch += opt.liftSemitones;
                }
                if (drums)
                    c.notes.push_back ({ 49, juce::jmin (1.0f, 0.75f * lift), 0.0, 2.0 }); // crash on each repeat
            }
        }
        if (lastWithKey)
            for (auto& c : rep.chords)
            {
                auto chord = Chord::fromName (c.chord);
                chord.root = (chord.root + opt.liftSemitones) % 12;
                if (chord.bass >= 0) chord.bass = (chord.bass + opt.liftSemitones) % 12;
                c.chord = chord.name();
            }
        pasteRegion (p, rep, at);

        Section v;
        v.id = makeId ("sec");
        v.type = SectionType::Vamp;
        v.name = "vamp";
        v.startBeat = at;
        v.lengthBeats = chorus.lengthBeats;
        v.energy = juce::jmin (1.0f, chorus.energy + 0.1f * (float) (r + 1));
        p.sections.push_back (v);
        created.push_back (v.id);

        if (lastWithKey)
        {
            p.keyEvents.push_back ({ at, p.keyAt (at).transposed (opt.liftSemitones) });
            // Restore the original key after the lifted vamp.
            p.keyEvents.push_back ({ at + chorus.lengthBeats, p.keyAt (at - 1) });
        }
        at += chorus.lengthBeats;
    }
    sortSections (p);
    return created;
}

//==============================================================================
std::vector<Section> suggestStructure (const juce::String& styleId, double targetSeconds, double bpm, double beatsPerBar)
{
    struct Part { SectionType type; int bars; float energy; };
    std::vector<Part> plan;

    if (styleId.contains ("worship") || styleId == "gospel_rnb")
        plan = { { SectionType::Intro, 4, 0.25f }, { SectionType::Verse, 16, 0.35f }, { SectionType::PreChorus, 8, 0.5f },
                 { SectionType::Chorus, 16, 0.7f }, { SectionType::Verse, 16, 0.45f }, { SectionType::Chorus, 16, 0.75f },
                 { SectionType::Bridge, 8, 0.6f }, { SectionType::Chorus, 16, 0.85f }, { SectionType::Vamp, 8, 0.9f },
                 { SectionType::Outro, 4, 0.3f } };
    else if (styleId == "nigerian_praise" || styleId == "highlife")
        plan = { { SectionType::Intro, 8, 0.5f }, { SectionType::Verse, 16, 0.6f }, { SectionType::Chorus, 16, 0.8f },
                 { SectionType::Verse, 16, 0.65f }, { SectionType::Chorus, 16, 0.85f }, { SectionType::Instrumental, 8, 0.75f },
                 { SectionType::Vamp, 16, 0.95f }, { SectionType::Outro, 4, 0.5f } };
    else if (styleId == "amapiano")
        plan = { { SectionType::Intro, 8, 0.35f }, { SectionType::Verse, 16, 0.55f }, { SectionType::Chorus, 16, 0.75f },
                 { SectionType::Instrumental, 16, 0.85f }, { SectionType::Verse, 16, 0.6f }, { SectionType::Chorus, 16, 0.8f },
                 { SectionType::Outro, 8, 0.4f } };
    else if (styleId == "hiphop_trap")
        plan = { { SectionType::Intro, 4, 0.3f }, { SectionType::Chorus, 8, 0.75f }, { SectionType::Verse, 16, 0.6f },
                 { SectionType::Chorus, 8, 0.8f }, { SectionType::Verse, 16, 0.65f }, { SectionType::Chorus, 8, 0.85f },
                 { SectionType::Outro, 4, 0.3f } };
    else // afrobeats, reggae/dancehall, default pop form
        plan = { { SectionType::Intro, 4, 0.4f }, { SectionType::Verse, 16, 0.55f }, { SectionType::PreChorus, 4, 0.65f },
                 { SectionType::Chorus, 8, 0.8f }, { SectionType::Verse, 16, 0.6f }, { SectionType::PreChorus, 4, 0.7f },
                 { SectionType::Chorus, 8, 0.85f }, { SectionType::Bridge, 8, 0.65f }, { SectionType::Chorus, 8, 0.9f },
                 { SectionType::Outro, 4, 0.4f } };

    // Scale bar counts (keeping multiples of 4) so the song lands near the target length.
    const double secondsPerBar = beatsPerBar * 60.0 / bpm;
    double planBars = 0;
    for (auto& pt : plan) planBars += pt.bars;
    const double scale = targetSeconds > 0 ? (targetSeconds / secondsPerBar) / planBars : 1.0;

    std::vector<Section> out;
    double beat = 0;
    for (auto& pt : plan)
    {
        int bars = (int) std::round (pt.bars * scale / 4.0) * 4;
        if (pt.type == SectionType::Intro || pt.type == SectionType::Outro || pt.type == SectionType::PreChorus)
            bars = juce::jlimit (2, 8, (int) std::round (pt.bars * juce::jmin (scale, 1.5)));
        bars = juce::jmax (2, bars);
        Section s;
        s.id = makeId ("sec");
        s.type = pt.type;
        s.name = toString (pt.type);
        s.startBeat = beat;
        s.lengthBeats = bars * beatsPerBar;
        s.energy = pt.energy;
        out.push_back (s);
        beat += s.lengthBeats;
    }
    return out;
}

std::vector<Issue> analyse (const Project& p)
{
    std::vector<Issue> issues;
    if (p.sections.empty())
    {
        issues.push_back ({ Issue::Severity::Info, "Add song sections (verse, chorus...) so the arrangement assistant can help.", {} });
        return issues;
    }

    TempoMap tm (p);
    auto sorted = p.sections;
    std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });

    const Section* firstChorus = nullptr;
    for (auto& s : sorted)
        if (s.type == SectionType::Chorus) { firstChorus = &s; break; }

    if (firstChorus == nullptr)
        issues.push_back ({ Issue::Severity::Warning, "There is no chorus. Most songs need a clear, repeated chorus.", {} });
    else
    {
        const double t = tm.beatsToSeconds (firstChorus->startBeat);
        if (t > 75.0)
            issues.push_back ({ Issue::Severity::Warning,
                                juce::String::formatted ("The first chorus arrives at %d:%02d. Listeners may drift before then; "
                                                         "try a shorter intro or first verse (aim for under a minute).",
                                                         (int) t / 60, (int) t % 60), firstChorus->id });
    }

    for (auto& s : sorted)
    {
        const double secs = tm.beatsToSeconds (s.endBeat()) - tm.beatsToSeconds (s.startBeat);
        const auto label = sectionLabel (p, s);
        if (s.type == SectionType::Intro && secs > 25)
            issues.push_back ({ Issue::Severity::Warning, label + " is " + juce::String ((int) secs) + " s long. Intros over ~20 s often drag.", s.id });
        if ((s.type == SectionType::Verse || s.type == SectionType::Bridge) && secs > 60)
            issues.push_back ({ Issue::Severity::Warning, label + " runs over a minute. Consider trimming it or adding a change halfway.", s.id });
        if (s.type == SectionType::Vamp && secs > 150)
            issues.push_back ({ Issue::Severity::Info, label + " is long. On a recording, a vamp over ~2 minutes may lose energy unless it keeps building.", s.id });
    }

    // Back-to-back sections of the same type at the same energy.
    for (size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i].type == sorted[i - 1].type && std::abs (sorted[i].energy - sorted[i - 1].energy) < 0.05f
            && sorted[i].type != SectionType::Vamp)
            issues.push_back ({ Issue::Severity::Info, sectionLabel (p, sorted[i]) + " repeats the previous section with no lift. "
                                "Add energy (drums, a key change, more instruments) or vary it.", sorted[i].id });

    const double total = tm.beatsToSeconds (sorted.back().endBeat());
    if (total > 7 * 60)
        issues.push_back ({ Issue::Severity::Info, "The song is over 7 minutes. That's fine for worship sets; for streaming, consider a radio edit.", {} });
    if (sorted.back().type != SectionType::Outro && sorted.back().type != SectionType::Vamp)
        issues.push_back ({ Issue::Severity::Info, "The song ends without an outro. A short outro gives a cleaner ending.", sorted.back().id });

    if (issues.empty())
        issues.push_back ({ Issue::Severity::Info, "The structure looks good.", {} });
    return issues;
}

} // namespace studio::arrange
