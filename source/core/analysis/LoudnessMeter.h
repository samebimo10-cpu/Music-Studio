#pragma once

#include <atomic>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../dsp/DspCommon.h"
#include "../dsp/TruePeak.h"

namespace studio
{

/** ITU-R BS.1770-4 / EBU R128 loudness meter: momentary (400 ms), short-term
    (3 s), gated integrated loudness, loudness range and true peak.

    process() is real-time safe after prepare(); the getters can be read from
    the UI thread. */
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels = 2);
    void reset();
    void process (const juce::AudioBuffer<float>& buffer);
    void process (const float* const* channels, int numChannels, int numSamples);

    float getMomentaryLufs() const noexcept   { return momentary.load(); }
    float getShortTermLufs() const noexcept   { return shortTerm.load(); }
    float getIntegratedLufs() const;          // gated; -inf (-120) until enough audio
    float getLoudnessRange() const;           // LU (EBU Tech 3342)
    float getTruePeakDb() const noexcept      { return dsp::gainToDb (truePeak.load()); }
    float getSamplePeakDb() const noexcept    { return dsp::gainToDb (samplePeak.load()); }

    /** Convenience: measure a whole buffer offline. */
    struct Summary
    {
        float integratedLufs = -120;
        float loudnessRange = 0;
        float truePeakDb = -120;
        float samplePeakDb = -120;
        float maxShortTermLufs = -120;
        float maxMomentaryLufs = -120;
    };
    static Summary measure (const juce::AudioBuffer<float>& buffer, double sampleRate);

private:
    void finishBlock() noexcept;

    double sr = 48000;
    int nch = 2;
    std::array<std::array<dsp::Biquad, 2>, 8> kFilters; // pre-filter + RLB per channel
    std::array<dsp::TruePeakDetector, 8> tp;

    int blockSize = 4800;           // 100 ms step
    int blockCounter = 0;
    double blockEnergy = 0;         // sum over channels of mean squares (weighted)
    std::array<double, 30> recentBlocks {}; // last 3 s of 100 ms block energies
    int recentPos = 0, recentCount = 0;

    // Gating blocks are kept for the whole programme. Preallocated in prepare()
    // for several hours; if exceeded, older blocks are folded into a histogram.
    std::vector<float> momentaryBlocks;   // 400 ms block loudness every 100 ms
    std::vector<float> shortTermBlocks;   // 3 s loudness every 100 ms (for LRA)
    size_t momentaryCount = 0, shortTermCount = 0;

    std::atomic<float> momentary { -120.0f }, shortTerm { -120.0f };
    std::atomic<float> truePeak { 0.0f }, samplePeak { 0.0f };
};

/** How each streaming platform will turn the song up or down (approximate
    published normalisation targets; confirm current guidance before launch). */
struct PlatformTarget
{
    juce::String name;
    float targetLufs;
    bool turnsUpQuietSongs;  // some platforms only turn down
};

const std::vector<PlatformTarget>& platformTargets();

/** Gain the platform applies (dB, negative = turned down). */
float platformAdjustmentDb (const PlatformTarget& t, float integratedLufs, float truePeakDb);

} // namespace studio
