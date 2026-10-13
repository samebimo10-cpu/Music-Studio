#pragma once

#include <atomic>
#include "Effect.h"
#include "../analysis/PitchDetector.h"
#include "../theory/MusicTheory.h"

namespace studio::dsp
{

/** Real-time pitch shifter: two crossfaded read taps sweeping through a delay
    line. Grain length follows the detected period when one is given, which keeps
    voice shifting smooth. Latency is about half a grain. */
class PitchShifter
{
public:
    void prepare (double sampleRate, float grainMs = 40.0f);
    void reset();
    void setRatio (float r) noexcept { ratio = juce::jlimit (0.25f, 4.0f, r); }
    void setGrainSamples (float samples) noexcept;
    float process (float x) noexcept;
    int latencySamples() const noexcept { return (int) (grain * 0.5f); }

private:
    DelayLine line;
    double sr = 48000;
    float grain = 1920, targetGrain = 1920, minGrain = 256, maxGrain = 4800;
    float ratio = 1.0f;
    float phase = 0; // 0..1 through the grain
};

//==============================================================================
/** Pitch correction to the song key. Speed runs from natural (slow glide) to
    hard "robotic" (instant snap). The note editor applies manual targets offline
    via vocal::PitchEditor; this effect handles the live and automatic path. */
class PitchCorrectEffect : public Effect
{
public:
    PitchCorrectEffect();
    juce::String getType() const override { return "pitchcorrect"; }
    juce::String getDisplayName() const override { return "Pitch correction"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    int getLatencySamples() const override { return shifters[0].latencySamples(); }

    /** Last detected and target notes, for the tuner display. */
    float getDetectedMidi() const noexcept { return detectedMidi.load(); }
    float getTargetMidi() const noexcept { return targetMidi.load(); }

private:
    enum { tonic, scale, strength, speed, humanize, mix };
    PitchDetector detector;
    std::array<PitchShifter, 2> shifters;
    std::vector<float> analysis;
    int analysisPos = 0, hopCounter = 0;
    float correction = 0;       // semitones, smoothed
    float targetCorrection = 0;
    float heldNote = -1;
    std::atomic<float> detectedMidi { 0 }, targetMidi { 0 };
};

//==============================================================================
/** Thickens a vocal by simulating a second (and third) take: short, slowly
    wandering delays with slight detune, panned apart. */
class DoublerEffect : public Effect
{
public:
    DoublerEffect();
    juce::String getType() const override { return "doubler"; }
    juce::String getDisplayName() const override { return "Doubler"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;

private:
    enum { mix, detune, delayMs, width };
    std::array<DelayLine, 2> lines;
    std::array<PitchShifter, 2> shifters;
    std::array<double, 2> phases { 0.0, 1.7 };
    juce::Random rng { 42 };
    std::array<float, 2> drift {}, driftTarget {};
    int driftCounter = 0;
};

//==============================================================================
/** Turns breaths and mouth clicks down rather than cutting them out. Uses a short
    look-ahead so the gain is already down when a breath starts. */
class BreathControlEffect : public Effect
{
public:
    BreathControlEffect();
    juce::String getType() const override { return "breath"; }
    juce::String getDisplayName() const override { return "Breath & click control"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    int getLatencySamples() const override { return lookahead; }

private:
    enum { breathReduction, clickReduction, sensitivity };
    int lookahead = 480;
    std::array<DelayLine, 2> delays;
    Biquad hpDetect, lpDetect, clickHp;
    float voiceLevel = 0, hiEnv = 0, loEnv = 0, gain = 1, clickGain = 1, clickEnv = 0;
    int frameCounter = 0, zeroCrossings = 0;
    float lastSample = 0;
    float frameEnergy = 0;
    bool breathState = false;
};

} // namespace studio::dsp
