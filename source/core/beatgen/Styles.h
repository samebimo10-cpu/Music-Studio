#pragma once

#include <map>
#include <vector>
#include <juce_core/juce_core.h>

namespace studio::beatgen
{

/** Drum lanes for one or two bars: GM note -> step string (16 steps per bar).
    'X' accent, 'x' normal, 'o' soft, 'g' ghost, '?' density-dependent soft hit, '.' rest. */
struct Groove
{
    std::map<int, juce::String> lanes;
    int bars() const;
};

enum class BassStyle    { Ballad, Driving, Makossa, Afrobeats, LogDrum, GospelWalk, Trap808, OneDrop };
enum class KeysStyle    { BalladArp, Sustained, GospelComp, OffbeatStabs, AfroChops, AmapianoKeys };
enum class GuitarStyle  { None, HighlifeArp, MutedSkank, Strum, AmbientSwell };
enum class PercStyle    { None, Praise, Afro, Amapiano, Highlife, Light };

/** A rule-based style template, written to be genre-accurate. */
struct StyleDef
{
    juce::String id, name, description;
    double bpmMin = 90, bpmMax = 120, bpmDefault = 100;
    float swing = 0;                       // fraction of a 16th that off-16ths are delayed
    bool halfTimeAvailable = false;        // e.g. trap at 140-180 written half-time

    juce::String drumKit, bass, keys, guitar, percussion, pad;
    BassStyle bassStyle = BassStyle::Driving;
    KeysStyle keysStyle = KeysStyle::Sustained;
    GuitarStyle guitarStyle = GuitarStyle::None;
    PercStyle percStyle = PercStyle::None;

    std::vector<Groove> verse, chorus;     // feel variants ("swap feel" cycles)
    Groove intro;
    double harmonicRhythm = 4;             // beats per chord
    bool extendedChords = false;           // 7ths and 9ths
    std::map<juce::String, std::vector<juce::StringArray>> progressions; // mood -> roman-numeral progressions
    juce::String defaultKey = "C major";
};

const std::vector<StyleDef>& styles();
const StyleDef* findStyle (const juce::String& id);

/** The four styles from build phase 2, then the rest. */
juce::StringArray phase2Styles();

juce::StringArray moods(); // joyful, reflective, intense

} // namespace studio::beatgen
