#include "MusicTheory.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace studio
{

namespace
{
    const std::vector<int>& scaleIntervals (ScaleType s)
    {
        static const std::vector<int> major          { 0, 2, 4, 5, 7, 9, 11 };
        static const std::vector<int> naturalMinor   { 0, 2, 3, 5, 7, 8, 10 };
        static const std::vector<int> harmonicMinor  { 0, 2, 3, 5, 7, 8, 11 };
        static const std::vector<int> dorian         { 0, 2, 3, 5, 7, 9, 10 };
        static const std::vector<int> mixolydian     { 0, 2, 4, 5, 7, 9, 10 };
        static const std::vector<int> majorPenta     { 0, 2, 4, 7, 9 };
        static const std::vector<int> minorPenta     { 0, 3, 5, 7, 10 };
        static const std::vector<int> blues          { 0, 3, 5, 6, 7, 10 };

        switch (s)
        {
            case ScaleType::Major:            return major;
            case ScaleType::NaturalMinor:     return naturalMinor;
            case ScaleType::HarmonicMinor:    return harmonicMinor;
            case ScaleType::Dorian:           return dorian;
            case ScaleType::Mixolydian:       return mixolydian;
            case ScaleType::MajorPentatonic:  return majorPenta;
            case ScaleType::MinorPentatonic:  return minorPenta;
            case ScaleType::Blues:            return blues;
        }
        return major;
    }

    int mod12 (int v) noexcept { return ((v % 12) + 12) % 12; }

    struct QualityInfo
    {
        ChordQuality quality;
        const char* suffix;
        std::vector<int> intervals;
    };

    const std::vector<QualityInfo>& qualityTable()
    {
        static const std::vector<QualityInfo> table {
            { ChordQuality::Major,           "",      { 0, 4, 7 } },
            { ChordQuality::Minor,           "m",     { 0, 3, 7 } },
            { ChordQuality::Diminished,      "dim",   { 0, 3, 6 } },
            { ChordQuality::Augmented,       "aug",   { 0, 4, 8 } },
            { ChordQuality::Sus2,            "sus2",  { 0, 2, 7 } },
            { ChordQuality::Sus4,            "sus4",  { 0, 5, 7 } },
            { ChordQuality::Dominant7,       "7",     { 0, 4, 7, 10 } },
            { ChordQuality::Major7,          "maj7",  { 0, 4, 7, 11 } },
            { ChordQuality::Minor7,          "m7",    { 0, 3, 7, 10 } },
            { ChordQuality::HalfDiminished7, "m7b5",  { 0, 3, 6, 10 } },
            { ChordQuality::Add9,            "add9",  { 0, 4, 7, 14 } },
            { ChordQuality::Major9,          "maj9",  { 0, 4, 7, 11, 14 } },
            { ChordQuality::Minor9,          "m9",    { 0, 3, 7, 10, 14 } },
            { ChordQuality::Dominant9,       "9",     { 0, 4, 7, 10, 14 } },
        };
        return table;
    }

    const QualityInfo& infoFor (ChordQuality q)
    {
        for (auto& i : qualityTable())
            if (i.quality == q)
                return i;
        return qualityTable().front();
    }
}

//==============================================================================
namespace theory
{
    juce::String pitchClassName (int pc, bool preferFlats)
    {
        static const char* flats[]  { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        static const char* sharps[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return preferFlats ? flats[mod12 (pc)] : sharps[mod12 (pc)];
    }

    juce::String noteName (int midiNote, bool preferFlats)
    {
        return pitchClassName (midiNote, preferFlats) + juce::String (midiNote / 12 - 1);
    }

    int pitchClassFromName (const juce::String& nameIn)
    {
        auto name = nameIn.trim();
        if (name.isEmpty())
            return -1;

        static const std::map<juce::juce_wchar, int> naturals {
            { 'C', 0 }, { 'D', 2 }, { 'E', 4 }, { 'F', 5 }, { 'G', 7 }, { 'A', 9 }, { 'B', 11 }
        };

        auto letter = juce::CharacterFunctions::toUpperCase (name[0]);
        auto it = naturals.find (letter);
        if (it == naturals.end())
            return -1;

        int pc = it->second;
        for (int i = 1; i < name.length(); ++i)
        {
            auto c = name[i];
            if (c == '#' || c == 0x266F) ++pc;
            else if (c == 'b' || c == 0x266D) --pc;
            else break;
        }
        return mod12 (pc);
    }

    double midiToHz (double midiNote) noexcept { return 440.0 * std::pow (2.0, (midiNote - 69.0) / 12.0); }
    double hzToMidi (double hz) noexcept       { return hz > 0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : 0.0; }

    juce::String scaleName (ScaleType s)
    {
        switch (s)
        {
            case ScaleType::Major:            return "major";
            case ScaleType::NaturalMinor:     return "minor";
            case ScaleType::HarmonicMinor:    return "harmonic minor";
            case ScaleType::Dorian:           return "dorian";
            case ScaleType::Mixolydian:       return "mixolydian";
            case ScaleType::MajorPentatonic:  return "major pentatonic";
            case ScaleType::MinorPentatonic:  return "minor pentatonic";
            case ScaleType::Blues:            return "blues";
        }
        return "major";
    }

    ScaleType scaleFromName (const juce::String& sIn)
    {
        auto s = sIn.trim().toLowerCase();
        if (s == "m" || s == "min" || s == "minor" || s == "natural minor" || s == "aeolian") return ScaleType::NaturalMinor;
        if (s == "harmonic minor")    return ScaleType::HarmonicMinor;
        if (s == "dorian")            return ScaleType::Dorian;
        if (s == "mixolydian")        return ScaleType::Mixolydian;
        if (s == "major pentatonic")  return ScaleType::MajorPentatonic;
        if (s == "minor pentatonic")  return ScaleType::MinorPentatonic;
        if (s == "blues")             return ScaleType::Blues;
        return ScaleType::Major;
    }
}

//==============================================================================
juce::String Key::toString() const
{
    // Flat keys read better for most gospel/worship charts, except the usual sharp keys.
    const bool sharpKey = (scale == ScaleType::Major && (tonic == 7 || tonic == 2 || tonic == 9 || tonic == 4 || tonic == 11))
                       || (isMinor() && (tonic == 4 || tonic == 11 || tonic == 6 || tonic == 1 || tonic == 9));
    return theory::pitchClassName (tonic, ! sharpKey) + " " + theory::scaleName (scale);
}

Key Key::fromString (const juce::String& sIn)
{
    Key k;
    auto s = sIn.trim();
    k.tonic = juce::jmax (0, theory::pitchClassFromName (s));

    // Split off the root (letter + accidentals) and parse the rest as a scale.
    int i = 1;
    while (i < s.length() && (s[i] == '#' || s[i] == 'b' || s[i] == 0x266F || s[i] == 0x266D))
        ++i;
    auto rest = s.substring (i).trim();
    k.scale = rest.isEmpty() ? ScaleType::Major : theory::scaleFromName (rest);
    return k;
}

bool Key::isMinor() const noexcept
{
    return scale == ScaleType::NaturalMinor || scale == ScaleType::HarmonicMinor
        || scale == ScaleType::Dorian || scale == ScaleType::MinorPentatonic || scale == ScaleType::Blues;
}

const std::vector<int>& Key::intervals() const { return scaleIntervals (scale); }

bool Key::containsPitchClass (int pc) const
{
    auto rel = mod12 (pc - tonic);
    auto& iv = intervals();
    return std::find (iv.begin(), iv.end(), rel) != iv.end();
}

int Key::snapToScale (int midiNote) const
{
    for (int d = 0; d < 7; ++d)
    {
        if (containsPitchClass (midiNote - d)) return midiNote - d;
        if (containsPitchClass (midiNote + d)) return midiNote + d;
    }
    return midiNote;
}

int Key::degreeToNote (int degree, int octave) const
{
    auto& iv = intervals();
    const int n = (int) iv.size();
    int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
    int idx = degree - oct * n;
    return (octave + 1 + oct) * 12 + tonic + iv[(size_t) idx];
}

Key Key::transposed (int semitones) const
{
    Key k = *this;
    k.tonic = mod12 (tonic + semitones);
    return k;
}

//==============================================================================
std::vector<int> Chord::intervals() const { return infoFor (quality).intervals; }

juce::String Chord::name (bool preferFlats) const
{
    auto n = theory::pitchClassName (root, preferFlats) + infoFor (quality).suffix;
    if (bass >= 0 && mod12 (bass) != mod12 (root))
        n << "/" << theory::pitchClassName (bass, preferFlats);
    return n;
}

Chord Chord::fromName (const juce::String& nameIn)
{
    Chord c;
    auto name = nameIn.trim();
    auto slash = name.indexOfChar ('/');
    if (slash > 0)
    {
        c.bass = theory::pitchClassFromName (name.substring (slash + 1));
        name = name.substring (0, slash);
    }

    c.root = juce::jmax (0, theory::pitchClassFromName (name));
    int i = 1;
    while (i < name.length() && (name[i] == '#' || name[i] == 'b'))
        ++i;
    auto suffix = name.substring (i);

    if (suffix == "min" || suffix == "-") suffix = "m";
    if (suffix == "M7" || suffix == "Maj7") suffix = "maj7";

    // Longest matching suffix wins (so "m7b5" beats "m").
    size_t bestLen = 0;
    c.quality = ChordQuality::Major;
    for (auto& q : qualityTable())
    {
        juce::String s (q.suffix);
        if (suffix == s && (size_t) s.length() >= bestLen)
        {
            bestLen = (size_t) s.length();
            c.quality = q.quality;
        }
    }
    return c;
}

std::vector<int> Chord::voicing (int centreNote, int numVoices) const
{
    std::vector<int> pcs;
    for (auto iv : intervals())
        pcs.push_back (mod12 (root + iv));

    // Pick, for each chord tone, the octave nearest to the centre, then add
    // doublings (root first) until the requested number of voices.
    std::vector<int> notes;
    for (auto pc : pcs)
    {
        int n = centreNote - mod12 (centreNote - pc);
        if (centreNote - n > 6) n += 12;
        notes.push_back (n);
    }
    for (size_t i = 0; (int) notes.size() < numVoices; ++i)
        notes.push_back (notes[i % pcs.size()] + 12);

    std::sort (notes.begin(), notes.end());
    while ((int) notes.size() > numVoices)
        notes.erase (notes.begin() + 1); // drop inner voices, keep top and bottom

    if (bass >= 0)
    {
        int b = notes.front() - mod12 (notes.front() - bass);
        if (b == notes.front()) b -= 12;
        notes.insert (notes.begin(), b);
    }
    return notes;
}

//==============================================================================
namespace theory
{
    Chord diatonicChord (const Key& key, int degree, bool seventh)
    {
        auto& iv = key.intervals();
        const int n = (int) iv.size();

        auto pcAt = [&] (int d) { return mod12 (key.tonic + iv[(size_t) (((d % n) + n) % n)]); };

        int r = pcAt (degree);
        int third = mod12 (pcAt (degree + 2) - r);
        int fifth = mod12 (pcAt (degree + 4) - r);
        int sev = mod12 (pcAt (degree + 6) - r);

        Chord c;
        c.root = r;
        if (n < 7)
        {
            // Pentatonic/blues: fall back to the parallel major/minor scale.
            Key k7 = key;
            k7.scale = key.isMinor() ? ScaleType::NaturalMinor : ScaleType::Major;
            return diatonicChord (k7, degree, seventh);
        }

        if (third == 4 && fifth == 7)       c.quality = seventh ? (sev == 11 ? ChordQuality::Major7 : ChordQuality::Dominant7) : ChordQuality::Major;
        else if (third == 3 && fifth == 7)  c.quality = seventh ? ChordQuality::Minor7 : ChordQuality::Minor;
        else if (third == 3 && fifth == 6)  c.quality = seventh ? ChordQuality::HalfDiminished7 : ChordQuality::Diminished;
        else if (third == 4 && fifth == 8)  c.quality = ChordQuality::Augmented;
        else                                c.quality = ChordQuality::Major;
        return c;
    }

    bool detectChord (const std::vector<int>& midiNotes, Chord& result)
    {
        std::array<bool, 12> present {};
        int lowest = 128;
        for (auto n : midiNotes)
        {
            present[(size_t) mod12 (n)] = true;
            lowest = std::min (lowest, n);
        }

        int count = 0;
        for (auto p : present) count += p ? 1 : 0;
        if (count < 2)
            return false;

        // Score every root/quality: +2 per matched chord tone, -1 per extra
        // note, -1 per missing tone, small bonus when the root is in the bass.
        int bestScore = -1000;
        for (int root = 0; root < 12; ++root)
        {
            for (auto& q : qualityTable())
            {
                int score = 0;
                std::array<bool, 12> tone {};
                for (auto iv : q.intervals)
                    tone[(size_t) mod12 (root + iv)] = true;

                for (int pc = 0; pc < 12; ++pc)
                {
                    if (present[(size_t) pc] && tone[(size_t) pc]) score += 2;
                    else if (present[(size_t) pc]) score -= 1;
                    else if (tone[(size_t) pc] && pc != mod12 (root + 7)) score -= 1; // the fifth may be omitted
                }
                if (mod12 (lowest) == root) score += 1;
                if (q.intervals.size() > 3) score -= 1; // prefer simpler names on ties

                if (score > bestScore)
                {
                    bestScore = score;
                    result.root = root;
                    result.quality = q.quality;
                    result.bass = mod12 (lowest) == root ? -1 : mod12 (lowest);
                }
            }
        }
        return true;
    }

    juce::String romanNumeral (const Key& key, const Chord& chord)
    {
        static const char* upper[] { "I", "II", "III", "IV", "V", "VI", "VII" };
        static const char* lower[] { "i", "ii", "iii", "iv", "v", "vi", "vii" };

        Key k7 = key;
        if (key.intervals().size() != 7)
            k7.scale = key.isMinor() ? ScaleType::NaturalMinor : ScaleType::Major;

        for (int d = 0; d < 7; ++d)
        {
            auto dc = diatonicChord (k7, d);
            if (dc.root != chord.root)
                continue;
            const bool minorish = chord.quality == ChordQuality::Minor || chord.quality == ChordQuality::Minor7
                               || chord.quality == ChordQuality::Minor9 || chord.quality == ChordQuality::Diminished
                               || chord.quality == ChordQuality::HalfDiminished7;
            juce::String r = minorish ? lower[d] : upper[d];
            if (chord.quality == ChordQuality::Diminished || chord.quality == ChordQuality::HalfDiminished7) r << "°";
            if (chord.quality == ChordQuality::Dominant7) r << "7";
            return r;
        }
        return {};
    }
}

} // namespace studio
