#pragma once

#include <memory>
#include "../dsp/BuiltinEffects.h"
#include "../dsp/SpectralEffects.h"
#include "../dsp/VoiceEffects.h"
#include "../model/Project.h"

namespace studio
{

/** The automatic vocal cleanup chain, in the spec's order. Each step can be
    bypassed. Timing alignment (step 5) works on whole takes, so it runs offline
    through vocal::alignTiming() rather than in this real-time chain.

    1 noise removal, 2 de-reverb, 3 breath & click control, 4 pitch correction,
    6 EQ, 7 compression, 8 de-esser, 9 saturation, 10 reverb and delay sends. */
class VocalChain : public dsp::Effect
{
public:
    struct Step
    {
        juce::String id;
        juce::String name;
        juce::String description;
        std::vector<dsp::Effect*> effects; // non-owning
    };

    VocalChain();

    juce::String getType() const override { return "vocalchain"; }
    juce::String getDisplayName() const override { return "Vocal chain"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    int getLatencySamples() const override;

    const std::vector<Step>& getSteps() const noexcept { return steps; }
    const Step* findStep (const juce::String& id) const;
    void setStepBypassed (const juce::String& id, bool bypassed);
    bool isStepBypassed (const juce::String& id) const;

    /** Applies preset + amount + key + per-parameter overrides. Message thread. */
    void applySettings (const VocalChainSettings& settings, const Key& songKey);

    static juce::StringArray presetNames();

    /** Values a preset sets ("step.param" -> value), before the amount knob. */
    static std::map<juce::String, float> presetValues (const juce::String& preset);

    /** "step.param" -> effect parameter (for the advanced view). */
    dsp::Effect* effectForStep (const juce::String& stepId, int index = 0) const;

    float getSpaceReverbSend() const noexcept { return reverbSend.load(); }
    float getSpaceDelaySend() const noexcept { return delaySend.load(); }

private:
    dsp::NoiseReductionEffect denoise;
    dsp::DeReverbEffect dereverb;
    dsp::BreathControlEffect breath;
    dsp::PitchCorrectEffect pitch;
    dsp::EqEffect eq;
    dsp::CompressorEffect comp;
    dsp::DeEsserEffect deesser;
    dsp::SaturationEffect saturation;
    dsp::ReverbEffect reverb;
    dsp::DelayEffect delay;

    std::vector<Step> steps;
    std::map<juce::String, std::atomic<bool>*> bypassFlags;
    std::array<std::atomic<bool>, 10> bypassStorage {};

    std::atomic<float> reverbSend { 0.0f }, delaySend { 0.0f };
    juce::AudioBuffer<float> sendBuffer, delayBuffer;

    // Latency compensation: steps with latency are always in the signal path
    // (even when bypassed the delay is kept) so toggling never shifts timing.
    juce::AudioBuffer<float> bypassDelay;
};

} // namespace studio
