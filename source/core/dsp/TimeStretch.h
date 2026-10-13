#pragma once

#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

namespace studio::dsp
{

/** Offline time-stretching and pitch-shifting.

    Time-stretch uses WSOLA (waveform-similarity overlap-add): each output grain
    is taken from near its nominal input position, shifted to the offset that best
    matches the previous grain, so pitch is kept and phasing artefacts stay low.
    Pitch-shift stretches by the pitch ratio and then resamples back to length. */
namespace timestretch
{
    /** ratio > 1 makes the audio longer (slower). */
    juce::AudioBuffer<float> stretch (const juce::AudioBuffer<float>& input, double ratio, double sampleRate);

    juce::AudioBuffer<float> pitchShift (const juce::AudioBuffer<float>& input, double semitones, double sampleRate);

    /** Stretch and pitch in one pass (used for clips and the loop browser's tempo/key matching). */
    juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input, double ratio, double semitones, double sampleRate);

    /** Plain resampling (changes pitch and length together). */
    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& input, double sourceRate, double targetRate);

    /** Piecewise warp: maps source times to target times (seconds, both ascending, same size >= 2). */
    juce::AudioBuffer<float> warp (const juce::AudioBuffer<float>& input, double sampleRate,
                                   const std::vector<double>& sourceTimes, const std::vector<double>& targetTimes);
}

} // namespace studio::dsp
