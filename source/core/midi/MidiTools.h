#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../model/Project.h"

namespace studio::midi
{

/** Smart quantize: pulls notes toward the grid by `strength`, leaves notes that are
    already within `window` of the grid alone, and keeps swing. Human feel survives. */
void quantize (std::vector<MidiNote>& notes, double gridBeats, float strength = 0.7f, float swing = 0.0f,
               double windowBeats = 0.0, bool quantizeEnds = false);

/** Small random timing and velocity variation. */
void humanize (std::vector<MidiNote>& notes, double timingBeats = 0.015, float velocityAmount = 0.08f, juce::int64 seed = 1);

void transpose (std::vector<MidiNote>& notes, int semitones);
void scaleVelocity (std::vector<MidiNote>& notes, float factor, float offset = 0.0f);
void snapToKey (std::vector<MidiNote>& notes, const Key& key);
void legato (std::vector<MidiNote>& notes);

/** Detects chords over time in MIDI (for the chord track), one per `stepBeats`. */
std::vector<ChordEvent> detectChords (const std::vector<MidiNote>& notes, double clipStart, double lengthBeats, double stepBeats = 2.0);

/** Chord pads: the chord for pad index 0..7 in a key (I ii iii IV V vi vii° and bVII), voiced for one finger. */
Chord chordPad (const Key& key, int padIndex, bool sevenths = false);
juce::StringArray chordPadLabels (const Key& key, bool sevenths = false);

/** Arpeggiator: turns held chords into a pattern. */
struct ArpSettings
{
    enum class Mode { Up, Down, UpDown, Random, AsPlayed } mode = Mode::Up;
    double rateBeats = 0.25;   // 1/16
    int octaves = 1;
    float gate = 0.8f;
    float swing = 0.0f;
};
std::vector<MidiNote> arpeggiate (const std::vector<MidiNote>& chords, const ArpSettings& s);

/** Strum engine: spreads chord notes like a guitar strum, alternating down/up. */
struct StrumSettings
{
    double strumTimeBeats = 0.06;  // time from first to last string
    juce::String pattern = "D.DU.UDU"; // 8ths: D down, U up, '.' rest
    double stepBeats = 0.5;
    float upVelocity = 0.75f;
};
std::vector<MidiNote> strum (const std::vector<MidiNote>& chords, const StrumSettings& s);

// ---- MIDI files ---------------------------------------------------------------
/** Writes the MIDI tracks of a project (or one track) as a type-1 .mid with tempo map. */
bool exportMidiFile (const Project& p, const juce::File& file, const juce::String& onlyTrackId = {});

/** Imports a .mid into new MIDI tracks of the project (one per MIDI track/channel). */
int importMidiFile (Project& p, const juce::File& file, double atBeat = 0);

} // namespace studio::midi
