#pragma once

#include "Effect.h"
#include "TruePeak.h"

namespace studio::dsp
{

//==============================================================================
/** High-pass, low shelf, two peaks, high shelf and low-pass. */
class EqEffect : public Effect
{
public:
    EqEffect();
    juce::String getType() const override { return "eq"; }
    juce::String getDisplayName() const override { return "EQ"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;

    /** Combined response for the EQ curve display. */
    double responseDb (double freq) const;

private:
    void updateCoefficients();
    enum { hpOn, hpFreq, lowFreq, lowGain, p1Freq, p1Gain, p1Q, p2Freq, p2Gain, p2Q, highFreq, highGain, lpOn, lpFreq, outGain };
    static constexpr int numBands = 6;
    std::array<std::array<Biquad, numBands>, 2> filters;
    std::array<Biquad, numBands> display;
    juce::uint32 seen = 0;
};

//==============================================================================
/** Feed-forward compressor with soft knee, optional sidechain input and mix. */
class CompressorEffect : public Effect
{
public:
    CompressorEffect();
    juce::String getType() const override { return "compressor"; }
    juce::String getDisplayName() const override { return "Compressor"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override { env = 0; grDb = 0; }
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>* sidechain) override;
    bool usesSidechain() const override { return p (useSidechain) > 0.5f; }
    float getGainReductionDb() const override { return grDb; }

    /** Static curve: output level for an input level (both dB), for the UI graph. */
    float curveDb (float inDb) const;

private:
    enum { threshold, ratio, attack, release, knee, makeup, mix, useSidechain, scHighPass };
    float env = 0, grDb = 0;
    Biquad scFilter;
    juce::uint32 seen = 0;
};

//==============================================================================
class GateEffect : public Effect
{
public:
    GateEffect();
    juce::String getType() const override { return "gate"; }
    juce::String getDisplayName() const override { return "Gate"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override { env = 0; gain = 0; holdCounter = 0; }
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    float getGainReductionDb() const override { return gainToDb (gain); }

private:
    enum { threshold, range, attack, hold, release };
    float env = 0, gain = 0;
    int holdCounter = 0;
};

//==============================================================================
/** Look-ahead brickwall limiter. With truePeak on, the detector runs on a 4x
    oversampled estimate so inter-sample peaks stay under the ceiling. */
class LimiterEffect : public Effect
{
public:
    LimiterEffect();
    juce::String getType() const override { return "limiter"; }
    juce::String getDisplayName() const override { return "Limiter"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    int getLatencySamples() const override { return lookahead; }
    float getGainReductionDb() const override { return grDb; }

private:
    enum { ceiling, release, inputGain, truePeak };
    int lookahead = 64;
    std::array<DelayLine, 2> delays;
    std::array<TruePeakDetector, 2> truePeakDetectors;
    std::vector<float> holdRing, boxRing;   // look-ahead minimum hold and smoothing box
    double boxSum = 0;
    int windowPos = 0;
    float gain = 1, grDb = 0;
};

//==============================================================================
/** 8-line feedback delay network reverb with room, hall, church and plate characters. */
class ReverbEffect : public Effect
{
public:
    ReverbEffect();
    juce::String getType() const override { return "reverb"; }
    juce::String getDisplayName() const override { return "Reverb"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;

private:
    void updateParams();
    enum { type, decay, preDelay, damping, size, mix, width, lowCut };
    static constexpr int N = 8;
    std::array<DelayLine, N> lines;
    std::array<float, N> delaySamples {}, feedbackGain {}, lpState {};
    std::array<DelayLine, 2> preDelays;
    std::array<DelayLine, 4> diffusers;
    std::array<float, 4> diffuserDelay {};
    std::array<Biquad, 2> inputHp;
    float dampCoeff = 0.3f;
    juce::uint32 seen = 0;
};

//==============================================================================
class DelayEffect : public Effect
{
public:
    DelayEffect();
    juce::String getType() const override { return "delay"; }
    juce::String getDisplayName() const override { return "Delay"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;

    /** Tempo for synced delay times (set by the engine from the tempo map). */
    void setTempo (double bpm) noexcept { tempo.store ((float) bpm); }

private:
    enum { timeMs, sync, feedback, mix, highCut, lowCut, pingPong };
    std::array<DelayLine, 2> lines;
    std::array<Biquad, 2> hc, lc;
    std::atomic<float> tempo { 120.0f };
    juce::uint32 seen = 0;
};

//==============================================================================
class ChorusEffect : public Effect
{
public:
    ChorusEffect();
    juce::String getType() const override { return "chorus"; }
    juce::String getDisplayName() const override { return "Chorus"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;

private:
    enum { rate, depth, mix, delayMs };
    std::array<DelayLine, 2> lines;
    double phase = 0;
};

//==============================================================================
/** Tape/tube-style saturation with tone control. 2x oversampled to tame aliasing. */
class SaturationEffect : public Effect
{
public:
    SaturationEffect();
    juce::String getType() const override { return "saturation"; }
    juce::String getDisplayName() const override { return "Saturation"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;

private:
    enum { drive, tone, mix, character, output };
    std::array<Biquad, 2> toneFilter, upFilter, downFilter, dcBlock;
    std::array<float, 2> lastIn {};
    juce::uint32 seen = 0;
};

//==============================================================================
/** Mid/side width with mono-below-frequency for tight low end. */
class WidenerEffect : public Effect
{
public:
    WidenerEffect();
    juce::String getType() const override { return "widener"; }
    juce::String getDisplayName() const override { return "Stereo widener"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override { sideHp.reset(); }
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;

private:
    enum { width, monoBelow };
    Biquad sideHp;
    juce::uint32 seen = 0;
};

//==============================================================================
/** Split-band de-esser: compresses only the sibilant band. */
class DeEsserEffect : public Effect
{
public:
    DeEsserEffect();
    juce::String getType() const override { return "deesser"; }
    juce::String getDisplayName() const override { return "De-esser"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    float getGainReductionDb() const override { return grDb; }

private:
    enum { frequency, threshold, range };
    std::array<LinkwitzRiley, 2> split;
    LinkwitzRiley detectSplit;
    EnvelopeFollower env, fullEnv;
    float grDb = 0;
    juce::uint32 seen = 0;
};

//==============================================================================
/** Utility gain and pan (also used for auto-mix trims). */
class GainEffect : public Effect
{
public:
    GainEffect();
    juce::String getType() const override { return "gain"; }
    juce::String getDisplayName() const override { return "Gain"; }
    void prepare (double sr, int block, int ch) override { sampleRate = sr; maxBlock = block; channels = ch; }
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;

private:
    enum { gainDb, invertPhase };
};

} // namespace studio::dsp
