#pragma once

#include "../dsp/BuiltinEffects.h"
#include "../analysis/AudioAnalysis.h"
#include "../analysis/LoudnessMeter.h"
#include "../model/Project.h"

namespace studio
{

/** EQ -> 3-band compressor -> stereo imaging -> saturation -> true-peak limiter. */
class MasteringChain : public dsp::Effect
{
public:
    MasteringChain();

    juce::String getType() const override { return "mastering"; }
    juce::String getDisplayName() const override { return "Mastering"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    int getLatencySamples() const override { return limiter.getLatencySamples(); }
    float getGainReductionDb() const override { return limiter.getGainReductionDb(); }

    std::array<float, 3> getBandGainReductionDb() const noexcept;

    /** Applies preset values, then any overrides in settings.params. */
    void applySettings (const MasteringSettings& settings);

    static juce::StringArray presetNames(); // Warm, Bright, Loud, Gentle worship
    static std::map<juce::String, float> presetValues (const juce::String& preset);

private:
    void updateFilters();

    enum
    {
        inputGain, lowShelfGain, lowShelfFreq, midGain, midFreq, highShelfGain, highShelfFreq,
        lowXover, highXover,
        lowThresh, lowRatio, midThresh, midRatio, highThresh, highRatio, mbAttack, mbRelease,
        width, bassMonoFreq, satDrive, satMix, ceiling, limiterRelease, outputGain
    };

    std::array<std::array<dsp::Biquad, 3>, 2> eqFilters;
    std::array<dsp::LinkwitzRiley, 2> xLow, xHigh;
    std::array<dsp::Biquad, 2> lowAllpass; // phase-align the low band with the split upper bands
    std::array<float, 3> bandEnv {}, bandGr {};
    dsp::Biquad sideHp;
    dsp::SaturationEffect saturation;
    dsp::LimiterEffect limiter;
    juce::uint32 seen = 0;
};

//==============================================================================
/** "AI master": analyses the mix and suggests mastering settings, then fits the
    input gain so the result lands on the loudness target. Rule-based analysis of
    tone, dynamics and width against genre reference curves; an ML model can
    replace suggest() later without changing callers. */
namespace mastering
{
    struct Suggestion
    {
        MasteringSettings settings;
        juce::StringArray notes;          // plain-words explanation of what it changed and why
        LoudnessMeter::Summary before, after;
    };

    /** Reference octave-band tilt for a style (relative dB). */
    std::array<float, analysis::SpectrumProfile::numBands> referenceTilt (const juce::String& style);

    Suggestion suggest (const juce::AudioBuffer<float>& mix, double sampleRate, const juce::String& style,
                        float targetLufs = -14.0f, float ceilingDbtp = -1.0f, const juce::String& basePreset = "Warm");

    /** Renders a buffer through a mastering chain (offline). */
    juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& mix, double sampleRate, const MasteringSettings& settings);

    /** Iteratively adjusts inputGain so the master hits targetLufs. Returns the adjusted settings. */
    MasteringSettings fitLoudness (const juce::AudioBuffer<float>& mix, double sampleRate, MasteringSettings settings,
                                   LoudnessMeter::Summary* resultOut = nullptr);
}

} // namespace studio
