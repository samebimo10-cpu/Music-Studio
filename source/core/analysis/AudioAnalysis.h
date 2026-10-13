#pragma once

#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../theory/MusicTheory.h"

namespace studio::analysis
{

/** Octave-band levels and other descriptors used by auto-mix, the mix assistant,
    the AI master and the reference-track comparison. */
struct SpectrumProfile
{
    static constexpr int numBands = 10;
    static const std::array<float, numBands>& bandCentres(); // 31.5 Hz .. 16 kHz

    std::array<float, numBands> bandDb {};   // average power per octave band (dB, relative to full scale)
    float rmsDb = -120;
    float peakDb = -120;
    float crestDb = 0;                      // peak minus RMS
    float centroidHz = 0;
    float stereoCorrelation = 1;            // -1..1 (1 = mono)
    float stereoWidth = 0;                  // side/mid energy ratio (0 = mono)
    float activeFraction = 0;               // fraction of 100 ms blocks above -50 dBFS

    /** Normalised: band levels relative to the overall level, so tone can be compared independent of loudness. */
    std::array<float, numBands> tilt() const;
};

SpectrumProfile analyseSpectrum (const juce::AudioBuffer<float>& buffer, double sampleRate,
                                 int startSample = 0, int numSamples = -1);

/** Estimated key and how confident the estimate is (0..1). */
struct KeyEstimate { Key key; float confidence = 0; };
KeyEstimate detectKey (const juce::AudioBuffer<float>& buffer, double sampleRate);

/** Estimated tempo in BPM (60..180) and confidence. */
struct TempoEstimate { double bpm = 120; float confidence = 0; };
TempoEstimate detectTempo (const juce::AudioBuffer<float>& buffer, double sampleRate);

/** Chord-from-audio: one chord per segment. */
struct ChordAtTime { double timeSeconds = 0; Chord chord; float confidence = 0; };
std::vector<ChordAtTime> detectChords (const juce::AudioBuffer<float>& buffer, double sampleRate, double segmentSeconds = 0.5);

/** 12-bin chroma of a buffer region (used by key and chord detection). */
std::array<float, 12> chroma (const juce::AudioBuffer<float>& buffer, double sampleRate, int start, int length);

//==============================================================================
struct RoomCheckResult
{
    float noiseFloorDb = -120;      // RMS of the silence recording, dBFS
    float humDb = -120;             // energy at mains frequency and harmonics
    int humFrequency = 0;           // 50 or 60, 0 if none
    float hissDb = -120;            // energy above 4 kHz
    float rumbleDb = -120;          // energy below 80 Hz
    bool clipped = false;
    juce::String rating;            // "Great", "Good", "Noisy", "Too noisy"
    juce::StringArray suggestions;
    std::vector<float> noiseProfile; // power per bin for the noise-removal effect (1024-point frames)
};

/** Analyses ~10 seconds of "silence" recorded in the room. */
RoomCheckResult analyseRoom (const juce::AudioBuffer<float>& silence, double sampleRate);

//==============================================================================
/** Input level check for the setup wizard. */
struct InputLevelAdvice
{
    float peakDb = -120;
    float rmsDb = -120;
    bool clipping = false;
    bool tooQuiet = false;
    juce::String message;
};
InputLevelAdvice adviseInputLevel (float peakDb, float rmsDb);

//==============================================================================
float leadingSilenceSeconds (const juce::AudioBuffer<float>& buffer, double sampleRate, float thresholdDb = -60.0f);
float trailingSilenceSeconds (const juce::AudioBuffer<float>& buffer, double sampleRate, float thresholdDb = -60.0f);
int countClippedSamples (const juce::AudioBuffer<float>& buffer, float threshold = 0.9999f);

} // namespace studio::analysis
