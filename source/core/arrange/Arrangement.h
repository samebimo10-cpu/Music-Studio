#pragma once

#include "../model/Project.h"

namespace studio::arrange
{

/** Timeline region operations. Every track follows: clips, automation, chords,
    markers, comments, tempo and key events are moved, split or trimmed together. */
void insertTime (Project& p, double atBeat, double lengthBeats);
void removeTime (Project& p, double startBeat, double endBeat);

/** A copied region: clips per track id (positions relative to the region start). */
struct Region
{
    double lengthBeats = 0;
    std::map<juce::String, std::vector<Clip>> clips;
    std::map<juce::String, std::vector<AutomationLane>> automation;
    std::vector<ChordEvent> chords;
};
Region copyRegion (const Project& p, double startBeat, double endBeat);
void pasteRegion (Project& p, const Region& r, double atBeat);

/** Splits clips on every track at a beat. */
void splitAt (Project& p, double beat);
/** Splits one clip; returns the id of the new right-hand clip or "". */
juce::String splitClip (Project& p, Track& t, const juce::String& clipId, double beat);

// ---- sections ------------------------------------------------------------------
juce::String duplicateSection (Project& p, const juce::String& sectionId);
void moveSection (Project& p, const juce::String& sectionId, int newIndex);
void resizeSection (Project& p, const juce::String& sectionId, double newLengthBeats);
void deleteSection (Project& p, const juce::String& sectionId);
void sortSections (Project& p);
juce::String sectionLabel (const Project& p, const Section& s);   // "Verse 2"

/** Vamp builder: repeats a chorus with rising energy (velocity, density, a crash at
    each repeat), optionally lifting the key for the last repeat. */
struct VampOptions
{
    int repeats = 3;
    bool keyLiftOnLast = false;
    int liftSemitones = 1;
};
std::vector<juce::String> buildVamp (Project& p, const juce::String& chorusSectionId, const VampOptions& options);

// ---- assistant ---------------------------------------------------------------------
/** Suggested section layout for a style and target length. */
std::vector<Section> suggestStructure (const juce::String& styleId, double targetSeconds, double bpm, double beatsPerBar = 4);

struct Issue
{
    enum class Severity { Info, Warning };
    Severity severity = Severity::Info;
    juce::String message;
    juce::String sectionId;
};
/** Flags sections that drag, a late first chorus, missing parts, very long intros... */
std::vector<Issue> analyse (const Project& p);

} // namespace studio::arrange
