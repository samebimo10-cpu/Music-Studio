#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../analysis/PitchDetector.h"
#include "../model/Project.h"

namespace studio::vocal
{

// ---- comping --------------------------------------------------------------------
/** Puts one take in charge of [startBeat, endBeat) in the comp, splitting others. */
void setCompSegment (Track& t, const juce::String& takeId, double startBeat, double endBeat);

/** Comp starts as the latest take across its whole range. */
void resetCompToLatest (Track& t);

/** Rebuilds the track's clips from the comp, joining phrases with crossfades. */
void buildCompClips (const Project& p, Track& t, double crossfadeBeats = 0.06);

/** Adds recorded takes to a track: keeps every take for comping and updates the comp. */
void addTakes (const Project& p, Track& t, const std::vector<Take>& takes);

// ---- pitch ----------------------------------------------------------------------------
/** A note in the pitch editor: detected pitch and the target the user (or auto) chose. */
struct PitchNote
{
    double startSeconds = 0, endSeconds = 0;
    float detectedMidi = 60;
    float targetMidi = 60;
    bool edited = false;
};

struct PitchAnalysis
{
    std::vector<PitchFrame> frames;
    std::vector<PitchNote> notes;
    double sampleRate = 48000;
};

/** Detects notes and proposes targets snapped to the key. */
PitchAnalysis analyseForEditor (const juce::AudioBuffer<float>& audio, double sampleRate, const Key& key);

/** Renders the pitch edits (and automatic correction of unedited notes by `strength`). */
juce::AudioBuffer<float> applyPitchEdits (const juce::AudioBuffer<float>& audio, const PitchAnalysis& analysis, float strength = 1.0f);

/** Time-varying pitch shift: semitone curve sampled every `hop` samples. */
juce::AudioBuffer<float> shiftByCurve (const juce::AudioBuffer<float>& audio, double sampleRate,
                                       const std::vector<float>& semitonesPerHop, int hop);

// ---- harmony, doubling ------------------------------------------------------------------
/** Creates harmony parts from a lead vocal, in key. Intervals are scale steps:
    +2 = a third above, +4 = a fifth above, -2 = a third below, -3 = a fourth below. */
std::vector<juce::AudioBuffer<float>> generateHarmonies (const juce::AudioBuffer<float>& lead, double sampleRate,
                                                         const Key& key, const std::vector<int>& scaleSteps);

/** Default harmony stacks for 2, 3 or 4 parts. */
std::vector<int> harmonyPreset (int parts);

juce::AudioBuffer<float> renderDouble (const juce::AudioBuffer<float>& vocal, double sampleRate, float amount = 0.6f);

// ---- voice to MIDI --------------------------------------------------------------------
/** Hum a melody: returns MIDI notes (beats, relative to the audio start). */
std::vector<MidiNote> voiceToMidi (const juce::AudioBuffer<float>& audio, double sampleRate, double bpm,
                                   const Key* snapKey = nullptr, double quantizeGrid = 0.25);

// ---- timing ----------------------------------------------------------------------------
/** Tightens a backing vocal or double to the lead (dynamic time warping of energy
    envelopes, then a smooth time-stretch). strength 0..1. */
juce::AudioBuffer<float> alignTiming (const juce::AudioBuffer<float>& lead, const juce::AudioBuffer<float>& backing,
                                      double sampleRate, float strength = 0.8f);

// ---- stem separation ---------------------------------------------------------------------
struct Stems
{
    juce::AudioBuffer<float> vocals, drums, bass, other;
};

/** Splits a song into vocals, drums, bass and other. This is the built-in DSP
    separator (harmonic/percussive split, centre extraction, low band); the ML
    separator slot can replace it without changing callers. */
Stems separateStems (const juce::AudioBuffer<float>& mix, double sampleRate, std::function<bool (float)> progress = {});

} // namespace studio::vocal
