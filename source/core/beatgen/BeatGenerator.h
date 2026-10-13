#pragma once

#include "Styles.h"
#include "../model/Project.h"

namespace studio::beatgen
{

struct Settings
{
    juce::String style = "afrobeats";
    double bpm = 0;               // 0 = style default
    juce::String key;             // "" = style default
    juce::String mood = "joyful"; // joyful, reflective, intense
    double lengthSeconds = 180;
    juce::int64 seed = 0;         // 0 = random
    bool halfTime = false;
};

/** Writes a complete, editable groove: sections, a chord progression, and drums,
    bass, keys, guitar, percussion and pad parts as MIDI on their own tracks.
    Rule-based style templates give reliable, genre-accurate results; the variation
    layer adds human detail. Everything stays editable. */
class BeatGenerator
{
public:
    /** Fills a project (clearing existing generated tracks, keeping vocals and audio). */
    static void generate (Project& project, const Settings& settings);

    /** New chord progression over the existing sections, then re-renders every generated part. */
    static void regenerateChords (Project& project, juce::int64 seed);

    enum class Adjustment { Regenerate, Simpler, Busier, SwapFeel, MoreLikeThis };

    /** One-click part controls from the Beat lab. */
    static void adjustPart (Project& project, const juce::String& trackId, Adjustment adjustment);

    /** Re-renders a generated part from its GeneratorInfo (after edits to chords or sections). */
    static void renderPart (Project& project, Track& track);

    /** Roman numeral to chord in a key ("vi", "IVmaj7", "V/3", "bVII"). Minor keys use the relative major's numerals. */
    static Chord romanToChord (const Key& key, const juce::String& numeral, bool extended);

    static juce::StringArray partNames(); // drums, bass, keys, guitar, percussion, pad
};

} // namespace studio::beatgen
