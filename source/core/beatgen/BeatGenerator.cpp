#include "BeatGenerator.h"
#include "../arrange/Arrangement.h"
#include "../instruments/InstrumentLibrary.h"
#include <algorithm>

namespace studio::beatgen
{

using namespace studio::instruments::gm;

namespace
{
    constexpr double stepBeats = 0.25; // a 16th note

    float velocityFor (juce::juce_wchar c)
    {
        switch (c)
        {
            case 'X': return 1.0f;
            case 'x': return 0.8f;
            case 'o': return 0.55f;
            case 'g': return 0.3f;
            case '?': return 0.45f;
            case 'm': return 0.6f;
            default:  return 0.0f;
        }
    }

    juce::int64 mix (juce::int64 a, juce::int64 b) { return (a * 6364136223846793005LL) ^ (b + 0x9E3779B97F4A7C15LL + (a << 6) + (a >> 2)); }

    struct Ctx
    {
        const Project& project;
        const StyleDef& style;
        const Section& section;
        int sectionIndex;
        juce::Random structural;
        juce::Random detail;
        float density;
        int feel;

        double swing (int step) const
        {
            return (step % 2 == 1) ? style.swing * stepBeats : 0.0;
        }

        /** Small human timing/velocity variation. */
        double humanTime() { return (detail.nextFloat() - 0.5f) * 0.02; }
        float humanVel (float v) { return juce::jlimit (0.05f, 1.0f, v * (0.92f + 0.16f * detail.nextFloat())); }
        float energyScale() const { return 0.8f + 0.25f * section.energy; }
        int bars() const { return juce::jmax (1, (int) std::round (section.lengthBeats / project.beatsPerBar())); }
        double barBeats() const { return project.beatsPerBar(); }
    };

    struct ChordSpan { double start; double length; Chord chord; };

    std::vector<ChordSpan> chordsIn (const Project& p, const Section& s)
    {
        std::vector<ChordSpan> out;
        for (auto& c : p.chords)
        {
            const double a = std::max (c.beat, s.startBeat), b = std::min (c.beat + c.lengthBeats, s.endBeat());
            if (b > a + 1.0e-6)
                out.push_back ({ a - s.startBeat, b - a, Chord::fromName (c.chord) });
        }
        if (out.empty())
            out.push_back ({ 0.0, s.lengthBeats, Chord { p.keyAt (s.startBeat).tonic, p.keyAt (s.startBeat).isMinor() ? ChordQuality::Minor : ChordQuality::Major, -1 } });
        return out;
    }

    const ChordSpan* chordAtRel (const std::vector<ChordSpan>& spans, double rel)
    {
        for (auto& c : spans)
            if (rel >= c.start - 1.0e-6 && rel < c.start + c.length - 1.0e-6) return &c;
        return spans.empty() ? nullptr : &spans.back();
    }

    int bassNote (int pc, int lowest = 28) { return lowest + ((pc - lowest % 12) % 12 + 12) % 12; }

    int thirdOf (const Chord& c)
    {
        auto iv = c.intervals();
        return iv.size() > 1 ? iv[1] : 4;
    }

    void add (std::vector<MidiNote>& notes, int pitch, float vel, double start, double len, double sectionLen)
    {
        if (start < 0 || start >= sectionLen - 1.0e-6 || pitch < 0 || pitch > 127) return;
        notes.push_back ({ pitch, juce::jlimit (0.05f, 1.0f, vel), start, std::max (0.05, std::min (len, sectionLen - start)) });
    }

    bool partActive (const juce::String& part, const StyleDef& style, const Section& s)
    {
        const auto t = s.type;
        if (part == "drums")
            return ! (t == SectionType::Intro && s.energy < 0.3f);
        if (part == "bass")
            return ! (t == SectionType::Intro && s.energy < 0.45f);
        if (part == "keys")
            return true;
        if (part == "guitar")
            return t == SectionType::Chorus || t == SectionType::Vamp || t == SectionType::Instrumental
                || (t == SectionType::Verse && s.energy >= 0.5f)
                || (style.guitarStyle == GuitarStyle::HighlifeArp && t != SectionType::Outro);
        if (part == "percussion")
            return (t == SectionType::Verse && s.energy >= 0.45f) || t == SectionType::Chorus || t == SectionType::Vamp
                || t == SectionType::Instrumental || (t == SectionType::Intro && style.percStyle == PercStyle::Highlife);
        if (part == "pad")
            return t != SectionType::Instrumental || style.keysStyle == KeysStyle::Sustained;
        return true;
    }

    const Groove& grooveFor (const StyleDef& style, const Section& s, int feel)
    {
        const auto t = s.type;
        if ((t == SectionType::Intro || t == SectionType::Outro) && ! style.intro.lanes.empty())
            return style.intro;
        if (t == SectionType::Chorus || t == SectionType::Vamp || t == SectionType::Instrumental)
            return style.chorus[(size_t) (feel % (int) style.chorus.size())];
        return style.verse[(size_t) (feel % (int) style.verse.size())];
    }

    //==========================================================================
    std::vector<MidiNote> drums (Ctx& c)
    {
        std::vector<MidiNote> notes;
        const auto& groove = grooveFor (c.style, c.section, c.feel);
        const int bars = c.bars();
        const double L = c.section.lengthBeats;
        float density = c.density;
        if (c.section.type == SectionType::Bridge) density *= 0.8f;
        if (c.section.type == SectionType::Vamp) density = juce::jmin (1.0f, density + 0.15f);
        const int gBars = groove.bars();
        const bool fills = bars >= 4;
        const int fillKind = c.structural.nextInt (3);

        for (int bar = 0; bar < bars; ++bar)
        {
            const bool lastBar = bar == bars - 1;
            const double barStart = bar * c.barBeats();
            for (auto& [note, pattern] : groove.lanes)
            {
                if (pattern.isEmpty()) continue;
                const int offset = (bar % gBars) * 16;
                for (int step = 0; step < 16; ++step)
                {
                    if (fills && lastBar && step >= 12 && note != kick && note != crash)
                        continue; // the fill replaces the last beat
                    const int idx = offset + step;
                    juce::juce_wchar ch = idx < pattern.length() ? pattern[idx] : (juce::juce_wchar) pattern[idx % juce::jmax (1, pattern.length())];
                    float v = velocityFor (ch);
                    if (v <= 0)
                    {
                        // Busier: add ghost hats/snares in the gaps.
                        const bool hat = note == closedHat || note == shaker;
                        if (hat && density > 0.7f && step % 2 == 1 && c.detail.nextFloat() < (density - 0.7f) * 1.6f) v = 0.3f;
                        else continue;
                    }
                    if (ch == '?' && c.detail.nextFloat() > density * 0.9f) continue;
                    if (ch == 'g' && c.detail.nextFloat() > density + 0.1f) continue;
                    // Simpler: thin out non-accented hits.
                    if (density < 0.35f && ch != 'X' && note != kick && note != snare && note != clap && step % 4 != 0
                        && c.detail.nextFloat() < (0.35f - density) * 2.5f)
                        continue;
                    const double t = barStart + step * stepBeats + c.swing (step) + c.humanTime() * 0.5;
                    add (notes, note, c.humanVel (v * c.energyScale()), juce::jmax (0.0, t), 0.2, L);
                }
            }

            if (fills && lastBar)
            {
                const double fb = barStart + 3.0 * (c.barBeats() / 4.0);
                if (fillKind == 0 || density < 0.4f)
                {
                    for (int k = 0; k < 4; ++k)
                        add (notes, snare, c.humanVel (0.5f + 0.12f * k), fb + k * stepBeats, 0.2, L);
                }
                else if (fillKind == 1)
                {
                    const int toms[] { highTom, midTom, midLowTom, lowTom };
                    for (int k = 0; k < 4; ++k)
                        add (notes, toms[k], c.humanVel (0.75f + 0.05f * k), fb + k * stepBeats, 0.25, L);
                }
                else
                {
                    add (notes, snare, c.humanVel (0.8f), fb, 0.2, L);
                    add (notes, snare, c.humanVel (0.6f), fb + 0.5, 0.2, L);
                    add (notes, midTom, c.humanVel (0.8f), fb + 0.75, 0.25, L);
                }
                if (c.section.type == SectionType::PreChorus)
                    for (int k = 0; k < 8; ++k)
                        add (notes, snare, c.humanVel (0.4f + 0.07f * k), barStart + k * 0.5 - (c.barBeats() > 4 ? 0 : 0), 0.2, L);
            }
        }

        if ((c.section.type == SectionType::Chorus || c.section.type == SectionType::Vamp || c.section.type == SectionType::Instrumental)
            && c.section.energy > 0.55f)
            add (notes, crash, 0.85f, 0.0, 2.0, L);
        return notes;
    }

    //==========================================================================
    std::vector<MidiNote> bass (Ctx& c, const std::vector<MidiNote>& drumNotes)
    {
        std::vector<MidiNote> notes;
        const auto spans = chordsIn (c.project, c.section);
        const double L = c.section.lengthBeats;
        const float d = c.density;
        const int low = c.style.bassStyle == BassStyle::LogDrum ? 33 : (c.style.bassStyle == BassStyle::Trap808 ? 24 : 28);

        auto rootAt = [&] (double rel) { auto* s = chordAtRel (spans, rel); return s != nullptr ? s->chord : Chord {}; };
        auto bassFor = [&] (const Chord& ch) { return bassNote (ch.bass >= 0 ? ch.bass : ch.root, low); };

        switch (c.style.bassStyle)
        {
            case BassStyle::Ballad:
            {
                for (auto& s : spans)
                {
                    const int r = bassFor (s.chord);
                    add (notes, r, c.humanVel (0.75f), s.start, s.length - 0.1, L);
                    if (d > 0.45f && s.length >= 4)
                        add (notes, r + 7, c.humanVel (0.6f), s.start + s.length / 2, s.length / 2 - 0.1, L);
                    if (d > 0.65f)
                    {
                        // Chromatic approach into the next chord.
                        const int next = bassFor (rootAt (s.start + s.length + 0.01));
                        add (notes, next + (next > r ? -1 : 1), c.humanVel (0.55f), s.start + s.length - 0.5, 0.45, L);
                    }
                }
                break;
            }
            case BassStyle::Driving:
            {
                const double step = d < 0.4f || c.section.type == SectionType::Verse ? 1.0 : 0.5;
                for (double t = 0; t < L - 1.0e-6; t += step)
                {
                    const auto ch = rootAt (t);
                    int pitch = bassFor (ch);
                    if (d > 0.75f && std::fmod (t, 1.0) > 0.4 && c.detail.nextFloat() < 0.3f) pitch += 12;
                    const bool down = std::fmod (t, 1.0) < 0.01;
                    add (notes, pitch, c.humanVel (down ? 0.85f : 0.65f), t, step * 0.85, L);
                }
                break;
            }
            case BassStyle::Makossa:
            {
                // Bouncing root-octave-fifth lines with syncopation, two variants.
                static const int stepsA[] { 0, 3, 6, 8, 10, 12, 14 };
                static const int ivA[]    { 0, 12, 7, 0, 12, 7, 5 };
                static const int stepsB[] { 0, 2, 4, 7, 8, 11, 12, 14 };
                static const int ivB[]    { 0, 7, 12, 10, 0, 7, 12, 4 };
                const bool varB = c.feel % 2 == 1;
                const int n = varB ? 8 : 7;
                for (int bar = 0; bar < c.bars(); ++bar)
                    for (int k = 0; k < n; ++k)
                    {
                        const int step = varB ? stepsB[k] : stepsA[k];
                        if (step != 0 && step != 8 && c.detail.nextFloat() > 0.45f + d * 0.6f) continue;
                        const double t = bar * c.barBeats() + step * stepBeats + c.swing (step);
                        const auto ch = rootAt (t);
                        int iv = varB ? ivB[k] : ivA[k];
                        if (iv == 4 || iv == 3) iv = thirdOf (ch);
                        add (notes, bassFor (ch) + iv, c.humanVel (step % 4 == 0 ? 0.85f : 0.65f), t, 0.4, L);
                    }
                break;
            }
            case BassStyle::Afrobeats:
            {
                static const int steps[] { 0, 3, 6, 10, 12, 13 };
                static const int iv[]    { 0, 0, 12, 0, 7, 5 };
                for (int bar = 0; bar < c.bars(); ++bar)
                    for (int k = 0; k < 6; ++k)
                    {
                        if (k >= 4 && c.detail.nextFloat() > d) continue;
                        const double t = bar * c.barBeats() + steps[k] * stepBeats + c.swing (steps[k]);
                        const auto ch = rootAt (t);
                        add (notes, bassFor (ch) + iv[k], c.humanVel (k == 0 ? 0.9f : 0.7f), t, k == 2 ? 0.5 : 0.7, L);
                    }
                break;
            }
            case BassStyle::LogDrum:
            {
                // Amapiano log drum: syncopated, melodic, with octave jumps.
                static const int stepsA[] { 0, 3, 6, 10, 12, 14 };
                static const int stepsB[] { 2, 5, 8, 11, 13 };
                const bool varB = c.feel % 2 == 1;
                for (int bar = 0; bar < c.bars(); ++bar)
                {
                    const int n = varB ? 5 : 6;
                    for (int k = 0; k < n; ++k)
                    {
                        const int step = varB ? stepsB[k] : stepsA[k];
                        if (k > 2 && c.detail.nextFloat() > d + 0.2f) continue;
                        const double t = bar * c.barBeats() + step * stepBeats + c.swing (step);
                        const auto ch = rootAt (t);
                        const int choices[] { 0, 12, 7, 0, 10 };
                        add (notes, bassFor (ch) + choices[c.detail.nextInt (5)], c.humanVel (0.85f), t, 0.45, L);
                    }
                }
                break;
            }
            case BassStyle::GospelWalk:
            {
                for (double t = 0; t < L - 1.0e-6; t += 1.0)
                {
                    const auto ch = rootAt (t);
                    const int r = bassFor (ch);
                    const int beatInChord = (int) std::round (std::fmod (t - (chordAtRel (spans, t) ? chordAtRel (spans, t)->start : 0), 4.0));
                    const auto nextCh = rootAt (t + 1.0);
                    const bool changeNext = ! (nextCh == ch);
                    int pitch = r;
                    if (changeNext && t > 0) { const int nr = bassFor (nextCh); pitch = nr + (c.detail.nextBool() ? -1 : 1); }
                    else if (beatInChord == 1) pitch = r + thirdOf (ch);
                    else if (beatInChord == 2) pitch = r + 7;
                    else if (beatInChord == 3) pitch = r + 12 - (c.detail.nextBool() ? 2 : 0);
                    add (notes, pitch, c.humanVel (beatInChord == 0 ? 0.85f : 0.68f), t, 0.85, L);
                    if (d > 0.6f && c.detail.nextFloat() < (d - 0.6f) * 2.0f)
                        add (notes, pitch, 0.35f, t + 0.5 + c.style.swing * 0.5, 0.2, L); // ghosted swing 8th
                }
                break;
            }
            case BassStyle::Trap808:
            {
                std::vector<double> hits;
                for (auto& n : drumNotes) if (n.pitch == kick) hits.push_back (n.startBeat);
                std::sort (hits.begin(), hits.end());
                if (hits.empty()) for (double t = 0; t < L; t += 4) hits.push_back (t);
                for (size_t i = 0; i < hits.size(); ++i)
                {
                    const double t = hits[i];
                    const double next = i + 1 < hits.size() ? hits[i + 1] : L;
                    const auto ch = rootAt (t);
                    int pitch = bassFor (ch);
                    if (c.detail.nextFloat() < 0.15f + 0.2f * d) pitch += 12;
                    // Overlap into the next note gives the 808 glide on mono patches.
                    add (notes, pitch, 0.9f, t, (next - t) + (c.detail.nextFloat() < 0.25f ? 0.1 : -0.05), L);
                }
                break;
            }
            case BassStyle::OneDrop:
            {
                static const int steps[] { 2, 4, 6, 10, 12, 14 };
                static const int iv[]    { 0, 7, 12, 7, 5, 4 };
                for (int bar = 0; bar < c.bars(); ++bar)
                    for (int k = 0; k < 6; ++k)
                    {
                        if (k > 3 && c.detail.nextFloat() > d) continue;
                        const double t = bar * c.barBeats() + steps[k] * stepBeats + c.swing (steps[k]);
                        const auto ch = rootAt (t);
                        int interval = iv[k] == 4 ? thirdOf (ch) : iv[k];
                        add (notes, bassFor (ch) + interval, c.humanVel (0.75f), t, 0.45, L);
                    }
                break;
            }
        }
        return notes;
    }

    //==========================================================================
    Chord colour (const Chord& ch, bool extended, juce::Random& r)
    {
        if (! extended) return ch;
        Chord c = ch;
        if (ch.quality == ChordQuality::Major) c.quality = r.nextBool() ? ChordQuality::Major7 : ChordQuality::Add9;
        else if (ch.quality == ChordQuality::Minor) c.quality = r.nextBool() ? ChordQuality::Minor7 : ChordQuality::Minor9;
        return c;
    }

    std::vector<MidiNote> keys (Ctx& c, bool isPad)
    {
        std::vector<MidiNote> notes;
        const auto spans = chordsIn (c.project, c.section);
        const double L = c.section.lengthBeats;
        const float d = c.density;
        const bool ext = c.style.extendedChords || c.project.mood == "reflective";
        auto style = isPad ? KeysStyle::Sustained : c.style.keysStyle;
        if (c.section.type == SectionType::Intro || c.section.type == SectionType::Outro)
            if (style == KeysStyle::AfroChops || style == KeysStyle::OffbeatStabs) style = KeysStyle::Sustained;

        auto voicingAt = [&] (const ChordSpan& s, int centre, int voices)
        {
            juce::Random r (mix (c.structural.getSeed(), (juce::int64) (s.start * 16)));
            return colour (s.chord, ext, r).voicing (centre, voices);
        };

        for (auto& s : spans)
        {
            switch (style)
            {
                case KeysStyle::Sustained:
                {
                    auto v = voicingAt (s, isPad ? 60 : 62, isPad ? 5 : 4);
                    const double restrike = isPad ? s.length : juce::jmin (s.length, c.barBeats());
                    for (double t = 0; t < s.length - 1.0e-6; t += restrike)
                        for (auto n : v)
                            add (notes, n, c.humanVel ((isPad ? 0.5f : 0.6f) * c.energyScale()), s.start + t, restrike - 0.05, L);
                    if (! isPad)
                        add (notes, bassNote (s.chord.root, 40), c.humanVel (0.6f), s.start, s.length - 0.05, L);
                    break;
                }
                case KeysStyle::BalladArp:
                {
                    auto v = voicingAt (s, 66, 4);
                    add (notes, bassNote (s.chord.root, 40), c.humanVel (0.6f), s.start, s.length - 0.05, L);
                    add (notes, bassNote (s.chord.root, 40) + 12, c.humanVel (0.45f), s.start, s.length - 0.05, L);
                    const double step = d < 0.4f ? 1.0 : (d > 0.8f ? 0.25 : 0.5);
                    static const int order[] { 0, 1, 2, 3, 2, 1, 0, 2 };
                    int k = 0;
                    for (double t = 0; t < s.length - 1.0e-6; t += step, ++k)
                    {
                        const int n = v[(size_t) order[k % 8] % v.size()];
                        add (notes, n, c.humanVel (0.5f + (k % 4 == 0 ? 0.15f : 0.0f)), s.start + t + c.humanTime(), step * 1.6, L);
                    }
                    break;
                }
                case KeysStyle::GospelComp:
                {
                    auto v = voicingAt (s, 63, 5);
                    static const double hitsA[] { 0.0, 1.5, 2.5, 3.5 };
                    static const double hitsB[] { 0.0, 0.75, 2.0, 2.75, 3.5 };
                    const bool varB = c.feel % 2 == 1;
                    const int n = varB ? 5 : 4;
                    for (double bar = 0; bar < s.length - 1.0e-6; bar += c.barBeats())
                        for (int k = 0; k < n; ++k)
                        {
                            const double t = bar + (varB ? hitsB[k] : hitsA[k]);
                            if (t >= s.length) break;
                            if (k > 0 && c.detail.nextFloat() > 0.4f + d * 0.6f) continue;
                            const double len = k == 0 ? 1.2 : 0.4;
                            for (auto note : v) add (notes, note, c.humanVel (k == 0 ? 0.7f : 0.55f), s.start + t + c.swing (1) * (k % 2), len, L);
                        }
                    // Passing chord a half step above the next chord, on the last 16th.
                    if (d > 0.6f && s.start + s.length < L - 0.5)
                        if (auto* next = chordAtRel (spans, s.start + s.length + 0.01))
                        {
                            Chord passing = next->chord;
                            passing.root = (passing.root + 1) % 12;
                            for (auto note : passing.voicing (63, 4)) add (notes, note, 0.5f, s.start + s.length - 0.25, 0.2, L);
                        }
                    break;
                }
                case KeysStyle::OffbeatStabs:
                {
                    auto v = voicingAt (s, 67, 3);
                    for (double t = 0.5; t < s.length - 1.0e-6; t += 1.0)
                    {
                        if (d < 0.3f && std::fmod (t, 2.0) < 1.0) continue;
                        for (auto note : v) add (notes, note, c.humanVel (0.6f), s.start + t + c.swing (1), 0.2, L);
                    }
                    break;
                }
                case KeysStyle::AfroChops:
                {
                    auto v = voicingAt (s, 65, 4);
                    static const char* patterns[] { "..x..x....x..x..", "x..x..x...x..x..", "..x...x.x...x.x." };
                    const juce::String pat = patterns[c.feel % 3];
                    for (double bar = 0; bar < s.length - 1.0e-6; bar += c.barBeats())
                        for (int step = 0; step < 16; ++step)
                        {
                            if (pat[step] != 'x') continue;
                            if (c.detail.nextFloat() > 0.5f + d * 0.5f) continue;
                            const double t = bar + step * stepBeats + c.swing (step);
                            if (t >= s.length) continue;
                            for (auto note : v) add (notes, note, c.humanVel (0.6f), s.start + t, 0.22, L);
                        }
                    break;
                }
                case KeysStyle::AmapianoKeys:
                {
                    auto v = voicingAt (s, 62, 5);
                    for (double bar = 0; bar < s.length - 1.0e-6; bar += c.barBeats())
                    {
                        for (auto note : v) add (notes, note, c.humanVel (0.55f), s.start + bar, 1.4, L);
                        for (auto note : v) add (notes, note, c.humanVel (0.5f), s.start + bar + 1.5, 0.9, L);
                        if (d > 0.5f) for (auto note : v) add (notes, note, c.humanVel (0.45f), s.start + bar + 2.5 + c.swing (1), 0.3, L);
                        if (d > 0.7f) for (auto note : v) add (notes, note, c.humanVel (0.45f), s.start + bar + 3.5, 0.3, L);
                    }
                    break;
                }
            }
        }
        return notes;
    }

    //==========================================================================
    std::vector<MidiNote> guitar (Ctx& c)
    {
        std::vector<MidiNote> notes;
        const auto spans = chordsIn (c.project, c.section);
        const double L = c.section.lengthBeats;
        const float d = c.density;

        switch (c.style.guitarStyle)
        {
            case GuitarStyle::None: break;
            case GuitarStyle::HighlifeArp:
            {
                const juce::String pat = c.feel % 2 == 0 ? "x.xxx.x.x.xxx.x." : "xx.x.xx.x.xx.x.x";
                static const int seq[] { 0, 1, 2, 3, 2, 1, 2, 3 };
                int k = 0;
                for (double t = 0; t < L - 1.0e-6; t += stepBeats)
                {
                    const int step = (int) std::round (std::fmod (t, 4.0) / stepBeats) % 16;
                    if (pat[step] != 'x') continue;
                    if (c.detail.nextFloat() > 0.55f + d * 0.45f) continue;
                    auto* s = chordAtRel (spans, t);
                    if (s == nullptr) continue;
                    auto v = s->chord.voicing (70, 4);
                    add (notes, v[(size_t) seq[k++ % 8] % v.size()], c.humanVel (step % 2 == 0 ? 0.7f : 0.55f), t + c.swing (step), 0.22, L);
                }
                break;
            }
            case GuitarStyle::MutedSkank:
            {
                const juce::String pat = d > 0.65f ? "..x..xx...x..xx." : "..x...x...x...x.";
                for (double t = 0; t < L - 1.0e-6; t += stepBeats)
                {
                    const int step = (int) std::round (std::fmod (t, 4.0) / stepBeats) % 16;
                    if (pat[step] != 'x') continue;
                    auto* s = chordAtRel (spans, t);
                    if (s == nullptr) continue;
                    auto v = s->chord.voicing (66, 3);
                    for (size_t i = 1; i < v.size(); ++i)
                        add (notes, v[i], c.humanVel (0.6f), t + c.swing (step), 0.15, L);
                }
                break;
            }
            case GuitarStyle::Strum:
            {
                // D . D U . U D U in 8ths
                static const int dirs[] { 1, 0, 1, -1, 0, -1, 1, -1 };
                for (double t = 0; t < L - 1.0e-6; t += 0.5)
                {
                    const int idx = (int) std::round (std::fmod (t, 4.0) / 0.5) % 8;
                    if (dirs[idx] == 0) continue;
                    auto* s = chordAtRel (spans, t);
                    if (s == nullptr) continue;
                    auto v = s->chord.voicing (57, 6);
                    if (dirs[idx] < 0) std::reverse (v.begin(), v.end());
                    for (size_t i = 0; i < v.size(); ++i)
                        add (notes, v[i], c.humanVel (dirs[idx] > 0 ? 0.65f : 0.5f), t + i * 0.02, 0.45, L);
                }
                break;
            }
            case GuitarStyle::AmbientSwell:
            {
                if (c.section.type != SectionType::Chorus && c.section.type != SectionType::Vamp && c.section.type != SectionType::Bridge)
                    break;
                for (auto& s : spans)
                {
                    const int r = 72 + ((s.chord.root - 72 % 12) % 12 + 12) % 12;
                    add (notes, r, c.humanVel (0.45f), s.start, s.length - 0.1, L);
                    add (notes, r + 7, c.humanVel (0.4f), s.start + 0.5, s.length - 0.6, L);
                }
                break;
            }
        }
        return notes;
    }

    //==========================================================================
    std::vector<MidiNote> percussion (Ctx& c)
    {
        std::vector<MidiNote> notes;
        const double L = c.section.lengthBeats;
        const float d = c.density;

        auto lane = [&] (int note, const juce::String& pat, float velScale = 1.0f)
        {
            for (int bar = 0; bar < c.bars(); ++bar)
                for (int step = 0; step < 16; ++step)
                {
                    const auto ch = pat[step % pat.length()];
                    float v = velocityFor (ch);
                    if (v <= 0) continue;
                    if ((ch == 'o' || ch == 'g') && c.detail.nextFloat() > d + 0.2f) continue;
                    add (notes, note, c.humanVel (v * velScale * c.energyScale()), bar * c.barBeats() + step * stepBeats + c.swing (step), 0.2, L);
                }
        };

        auto talkingDrumPhrase = [&] (double at)
        {
            // A short call: low-high-high-low with the drum's pitch bend.
            static const int pat[] { talkingDrumLow, talkingDrumHigh, talkingDrumHigh, talkingDrumLow, talkingDrumHigh };
            static const double times[] { 0.0, 0.5, 0.75, 1.25, 1.5 };
            for (int k = 0; k < 5; ++k)
                if (k < 3 || d > 0.5f)
                    add (notes, pat[k], c.humanVel (0.75f), at + times[k], 0.3, L);
        };

        switch (c.style.percStyle)
        {
            case PercStyle::None: break;
            case PercStyle::Light:
                if (c.section.type == SectionType::Chorus || c.section.type == SectionType::Vamp)
                    lane (shaker, "x.o.x.o.x.o.x.o.", 0.7f);
                break;
            case PercStyle::Praise:
                lane (shekere, "oxoXoxoXoxoXoxoX", 0.8f);
                lane (c.feel % 2 == 0 ? congaHigh : djembeTone, "....m...m...x.x.", 0.85f);
                lane (congaLow, "......x.......x.", 0.8f);
                for (int bar = 1; bar < c.bars(); bar += 2)
                    if (c.detail.nextFloat() < 0.4f + d * 0.6f)
                        talkingDrumPhrase (bar * c.barBeats() + 2.0);
                break;
            case PercStyle::Afro:
                lane (congaHigh, "..m...x...m.x.x.", 0.8f);
                lane (djembeSlap, "............x...", 0.7f);
                if (d > 0.55f)
                    for (int bar = 3; bar < c.bars(); bar += 4) talkingDrumPhrase (bar * c.barBeats() + 2.0);
                break;
            case PercStyle::Amapiano:
                lane (shekere, "oxoxoxoxoxoxoxox", 0.6f);
                lane (congaHigh, "...x..x....x..x.", 0.7f);
                lane (djembeTone, "......x.......o.", 0.6f);
                break;
            case PercStyle::Highlife:
                lane (agogoHigh, "x.x.xx.x.x.x.x..", 0.8f);
                lane (agogoLow, "..........x.....", 0.7f);
                lane (shekere, "x.o.x.o.x.o.x.o.", 0.7f);
                lane (congaHigh, "....m...m...x.x.", 0.75f);
                break;
        }
        return notes;
    }

    juce::String instrumentFor (const StyleDef& s, const juce::String& part)
    {
        if (part == "drums") return s.drumKit;
        if (part == "bass") return s.bass;
        if (part == "keys") return s.keys;
        if (part == "guitar") return s.guitar;
        if (part == "percussion") return s.percussion;
        if (part == "pad") return s.pad;
        return {};
    }

    TrackRole roleFor (const juce::String& part)
    {
        if (part == "drums") return TrackRole::Drums;
        if (part == "bass") return TrackRole::Bass;
        if (part == "keys") return TrackRole::Keys;
        if (part == "guitar") return TrackRole::Guitar;
        if (part == "percussion") return TrackRole::Percussion;
        if (part == "pad") return TrackRole::Pad;
        return TrackRole::Unknown;
    }

    float defaultVolume (const juce::String& part)
    {
        if (part == "drums") return -4;
        if (part == "bass") return -5;
        if (part == "keys") return -8;
        if (part == "guitar") return -11;
        if (part == "percussion") return -12;
        if (part == "pad") return -15;
        return -8;
    }

    void writeChords (Project& p, juce::int64 seed)
    {
        const auto* style = findStyle (p.style);
        if (style == nullptr) style = &styles().front();
        juce::Random r (seed);

        auto mood = p.mood;
        auto it = style->progressions.find (mood);
        if (it == style->progressions.end()) it = style->progressions.begin();
        const auto& progs = it->second;

        const auto& verseProg = progs[(size_t) r.nextInt ((int) progs.size())];
        const auto& chorusProg = progs[(size_t) r.nextInt ((int) progs.size())];
        juce::StringArray bridgeProg { "IV", "V", "vi", "vi" };
        if (auto b = style->progressions.find ("reflective"); b != style->progressions.end())
            bridgeProg = b->second[(size_t) r.nextInt ((int) b->second.size())];

        p.chords.clear();
        for (auto& s : p.sections)
        {
            const juce::StringArray* prog = &verseProg;
            if (s.type == SectionType::Chorus || s.type == SectionType::Vamp || s.type == SectionType::Intro
                || s.type == SectionType::Outro || s.type == SectionType::Instrumental)
                prog = &chorusProg;
            else if (s.type == SectionType::Bridge)
                prog = &bridgeProg;
            const Key key = p.keyAt (s.startBeat + 0.01);
            const double hr = style->harmonicRhythm;
            int k = 0;
            for (double t = 0; t < s.lengthBeats - 1.0e-6; t += hr, ++k)
            {
                auto chord = BeatGenerator::romanToChord (key, (*prog)[k % prog->size()], false);
                // Outro ends on the tonic.
                if (s.type == SectionType::Outro && t + hr >= s.lengthBeats - 1.0e-6)
                    chord = BeatGenerator::romanToChord (key, "I", false);
                p.chords.push_back ({ s.startBeat + t, std::min (hr, s.lengthBeats - t), chord.name() });
            }
        }
    }
}

//==============================================================================
juce::StringArray BeatGenerator::partNames() { return { "drums", "bass", "keys", "guitar", "percussion", "pad" }; }

Chord BeatGenerator::romanToChord (const Key& keyIn, const juce::String& numeralIn, bool extended)
{
    // Minor keys: numerals refer to the relative major (so "vi" is the minor tonic).
    Key key = keyIn;
    if (key.isMinor()) { key.tonic = (key.tonic + 3) % 12; key.scale = ScaleType::Major; }
    else key.scale = ScaleType::Major;

    auto s = numeralIn.trim();
    int inversion = 0;
    if (s.endsWith ("/3")) { inversion = 1; s = s.dropLastCharacters (2); }
    if (s.endsWith ("/5")) { inversion = 2; s = s.dropLastCharacters (2); }

    int accidental = 0;
    if (s.startsWith ("b")) { accidental = -1; s = s.substring (1); }
    else if (s.startsWith ("#")) { accidental = 1; s = s.substring (1); }

    static const char* numerals[] { "VII", "VI", "IV", "V", "III", "II", "I" };
    static const int degrees[]    { 6, 5, 3, 4, 2, 1, 0 };
    int degree = 0;
    bool upper = true;
    juce::String suffix;
    for (int i = 0; i < 7; ++i)
    {
        const juce::String n (numerals[i]);
        if (s.toUpperCase().startsWith (n))
        {
            degree = degrees[i];
            upper = s.substring (0, n.length()) == n;
            suffix = s.substring (n.length());
            break;
        }
    }

    Chord c = theory::diatonicChord (key, degree);
    c.root = (c.root + accidental + 12) % 12;
    if (accidental != 0 || suffix.isNotEmpty())
        c.quality = upper ? ChordQuality::Major : ChordQuality::Minor;
    if (suffix == "o" || suffix == "dim") c.quality = ChordQuality::Diminished;
    else if (suffix == "7") c.quality = upper ? ChordQuality::Dominant7 : ChordQuality::Minor7;
    else if (suffix == "maj7") c.quality = ChordQuality::Major7;
    else if (suffix == "9") c.quality = upper ? ChordQuality::Dominant9 : ChordQuality::Minor9;
    else if (suffix == "sus4") c.quality = ChordQuality::Sus4;
    else if (suffix == "sus2") c.quality = ChordQuality::Sus2;
    else if (suffix == "add9") c.quality = ChordQuality::Add9;

    if (extended)
    {
        if (c.quality == ChordQuality::Major) c.quality = ChordQuality::Major7;
        else if (c.quality == ChordQuality::Minor) c.quality = ChordQuality::Minor7;
    }

    if (inversion > 0)
    {
        auto iv = c.intervals();
        c.bass = (c.root + iv[(size_t) juce::jmin (inversion, (int) iv.size() - 1)]) % 12;
    }
    return c;
}

void BeatGenerator::renderPart (Project& p, Track& track)
{
    const auto* style = findStyle (p.style);
    if (style == nullptr || track.generator.part.isEmpty())
        return;
    const auto part = track.generator.part;
    track.clips.clear();

    // Drums are needed for the 808 to follow the kick; render them on the side.
    std::vector<std::vector<MidiNote>> drumBySection;
    if (part == "bass" && style->bassStyle == BassStyle::Trap808)
    {
        const Track* drumTrack = nullptr;
        for (auto& t : p.tracks) if (t.generator.part == "drums") drumTrack = &t;
        for (size_t i = 0; i < p.sections.size(); ++i)
        {
            std::vector<MidiNote> dn;
            if (drumTrack != nullptr)
                for (auto& c : drumTrack->clips)
                    if (std::abs (c.startBeat - p.sections[i].startBeat) < 1.0e-3) dn = c.notes;
            drumBySection.push_back (dn);
        }
    }

    for (size_t i = 0; i < p.sections.size(); ++i)
    {
        const auto& sec = p.sections[i];
        if (! partActive (part, *style, sec))
            continue;

        Ctx c { p, *style, sec, (int) i,
                juce::Random (mix (track.generator.seed, (juce::int64) i)),
                juce::Random (mix (track.generator.variation, (juce::int64) i * 7 + 3)),
                juce::jlimit (0.0f, 1.0f, track.generator.density * (0.75f + 0.5f * sec.energy)),
                track.generator.feel };

        std::vector<MidiNote> notes;
        if (part == "drums")           notes = drums (c);
        else if (part == "bass")       notes = bass (c, i < drumBySection.size() ? drumBySection[i] : std::vector<MidiNote> {});
        else if (part == "keys")       notes = keys (c, false);
        else if (part == "pad")        notes = keys (c, true);
        else if (part == "guitar")     notes = guitar (c);
        else if (part == "percussion") notes = percussion (c);
        if (notes.empty()) continue;

        std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
        Clip clip;
        clip.id = makeId ("clip");
        clip.name = track.name + " - " + arrange::sectionLabel (p, sec);
        clip.isMidi = true;
        clip.startBeat = sec.startBeat;
        clip.lengthBeats = sec.lengthBeats;
        clip.notes = std::move (notes);
        track.clips.push_back (std::move (clip));
    }
}

void BeatGenerator::generate (Project& p, const Settings& settings)
{
    const auto* style = findStyle (settings.style);
    if (style == nullptr) style = &styles().front();

    juce::int64 seed = settings.seed != 0 ? settings.seed : juce::Random::getSystemRandom().nextInt64();
    if (seed == 0) seed = 1;

    p.style = style->id;
    p.mood = settings.mood.isNotEmpty() ? settings.mood : juce::String ("joyful");
    p.bpm = settings.bpm > 0 ? settings.bpm : style->bpmDefault;
    if (settings.halfTime && style->halfTimeAvailable) p.bpm = juce::jlimit (60.0, 95.0, p.bpm);
    p.key = Key::fromString (settings.key.isNotEmpty() ? settings.key : style->defaultKey);
    p.tempoEvents.clear();
    p.keyEvents.clear();
    p.sections = arrange::suggestStructure (style->id, settings.lengthSeconds, p.bpm, p.beatsPerBar());

    writeChords (p, mix (seed, 99));

    // Replace earlier generated tracks, keep the user's own.
    p.tracks.erase (std::remove_if (p.tracks.begin(), p.tracks.end(), [] (const Track& t) { return t.generator.part.isNotEmpty(); }),
                    p.tracks.end());

    juce::String drumBus, musicBus;
    for (auto& t : p.tracks)
    {
        if (t.type != TrackType::Bus) continue;
        if (t.name.containsIgnoreCase ("drum")) drumBus = t.id;
        if (t.name.containsIgnoreCase ("music") || t.name.containsIgnoreCase ("band")) musicBus = t.id;
    }

    int partIndex = 0;
    for (auto& part : partNames())
    {
        ++partIndex;
        const auto inst = instrumentFor (*style, part);
        if (inst.isEmpty()) continue;
        juce::String name = part.substring (0, 1).toUpperCase() + part.substring (1);
        if (auto* info = instruments::find (inst)) name = name + " (" + info->name + ")";
        auto& t = p.addTrack (TrackType::Midi, name, roleFor (part));
        t.instrument.id = inst;
        t.volumeDb = defaultVolume (part);
        t.generator.part = part;
        t.generator.seed = mix (seed, partIndex);
        t.generator.variation = mix (seed, partIndex * 101);
        t.generator.density = settings.mood == "intense" ? 0.65f : (settings.mood == "reflective" ? 0.4f : 0.55f);
        t.generator.feel = 0;
        if ((part == "drums" || part == "percussion") && drumBus.isNotEmpty()) t.outputBusId = drumBus;
        else if (part != "drums" && part != "percussion" && musicBus.isNotEmpty()) t.outputBusId = musicBus;
        if (part == "percussion") t.pan = 0.2f;
        if (part == "guitar") t.pan = -0.25f;
    }

    // Drums first so the 808 can follow the kick.
    for (auto& t : p.tracks) if (t.generator.part == "drums") renderPart (p, t);
    for (auto& t : p.tracks) if (t.generator.part.isNotEmpty() && t.generator.part != "drums") renderPart (p, t);
}

void BeatGenerator::regenerateChords (Project& p, juce::int64 seed)
{
    writeChords (p, seed != 0 ? seed : juce::Random::getSystemRandom().nextInt64());
    for (auto& t : p.tracks) if (t.generator.part == "drums") renderPart (p, t);
    for (auto& t : p.tracks) if (t.generator.part.isNotEmpty() && t.generator.part != "drums") renderPart (p, t);
}

void BeatGenerator::adjustPart (Project& p, const juce::String& trackId, Adjustment a)
{
    auto* t = p.findTrack (trackId);
    if (t == nullptr || t->generator.part.isEmpty())
        return;
    auto& g = t->generator;
    auto& rng = juce::Random::getSystemRandom();
    switch (a)
    {
        case Adjustment::Regenerate:   g.seed = rng.nextInt64(); g.variation = rng.nextInt64(); break;
        case Adjustment::Simpler:      g.density = juce::jmax (0.05f, g.density - 0.15f); break;
        case Adjustment::Busier:       g.density = juce::jmin (1.0f, g.density + 0.15f); break;
        case Adjustment::SwapFeel:     g.feel = (g.feel + 1) % 4; break;
        case Adjustment::MoreLikeThis: g.variation = rng.nextInt64(); break;
    }
    renderPart (p, *t);
    if (t->generator.part == "drums")
        for (auto& other : p.tracks)
            if (other.generator.part == "bass")
                if (const auto* style = findStyle (p.style); style != nullptr && style->bassStyle == BassStyle::Trap808)
                    renderPart (p, other);
}

} // namespace studio::beatgen
