#pragma once

#include <array>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>

namespace studio
{

enum class ScaleType
{
    Major,
    NaturalMinor,
    HarmonicMinor,
    Dorian,
    Mixolydian,
    MajorPentatonic,
    MinorPentatonic,
    Blues
};

/** A key such as "E♭ major". Tonic is a pitch class (0 = C). */
struct Key
{
    int tonic = 0;
    ScaleType scale = ScaleType::Major;

    bool operator== (const Key& other) const noexcept { return tonic == other.tonic && scale == other.scale; }

    juce::String toString() const;                 // "Eb major", "F# minor"
    static Key fromString (const juce::String& s); // accepts "Eb", "Ebm", "Eb major", "F# minor"

    bool isMinor() const noexcept;
    const std::vector<int>& intervals() const;     // semitones from the tonic
    bool containsPitchClass (int pitchClass) const;

    /** Snaps a MIDI note to the nearest note in the key (ties go down). */
    int snapToScale (int midiNote) const;

    /** MIDI note for a scale degree (0-based, may exceed the scale length) above a base octave. */
    int degreeToNote (int degree, int octave) const;

    /** Transposes the key by semitones (e.g. the final-chorus lift). */
    Key transposed (int semitones) const;
};

namespace theory
{
    juce::String pitchClassName (int pitchClass, bool preferFlats = true);
    juce::String noteName (int midiNote, bool preferFlats = true); // "C4" = 60
    int pitchClassFromName (const juce::String& name);              // -1 if invalid
    double midiToHz (double midiNote) noexcept;
    double hzToMidi (double hz) noexcept;
    juce::String scaleName (ScaleType s);
    ScaleType scaleFromName (const juce::String& s);
}

enum class ChordQuality
{
    Major, Minor, Diminished, Augmented, Sus2, Sus4,
    Dominant7, Major7, Minor7, HalfDiminished7, Add9, Major9, Minor9, Dominant9
};

/** A chord with a root pitch class, a quality and an optional slash bass. */
struct Chord
{
    int root = 0;
    ChordQuality quality = ChordQuality::Major;
    int bass = -1; // -1 = root position

    std::vector<int> intervals() const;
    juce::String name (bool preferFlats = true) const; // "Cmaj7", "F#m/A"
    static Chord fromName (const juce::String& name);

    /** Voices the chord around a centre note, keeping the voicing compact. */
    std::vector<int> voicing (int centreNote, int numVoices = 4) const;

    bool operator== (const Chord& o) const noexcept { return root == o.root && quality == o.quality && bass == o.bass; }
};

namespace theory
{
    /** Diatonic chord on a scale degree (0-based). Seventh adds the diatonic 7th. */
    Chord diatonicChord (const Key& key, int degree, bool seventh = false);

    /** Detects the most likely chord in a set of MIDI notes. Returns false if fewer than 2 pitch classes. */
    bool detectChord (const std::vector<int>& midiNotes, Chord& result);

    /** Roman numeral label (e.g. "IV", "vi") for a chord in a key, or "" if not diatonic. */
    juce::String romanNumeral (const Key& key, const Chord& chord);
}

} // namespace studio
