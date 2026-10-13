#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>
#include "DspCommon.h"

namespace studio::dsp
{

struct ParamInfo
{
    juce::String id;
    juce::String name;
    float minValue = 0, maxValue = 1, defaultValue = 0;
    juce::String unit;              // "dB", "Hz", "ms", "%", ":1"
    juce::StringArray choices;      // non-empty for choice parameters (value is the index)
    bool logarithmic = false;
    juce::String help;              // tooltip text
};

/** Base class for every built-in effect.

    Parameters are atomics: the UI thread calls setParam(), the audio thread reads
    them once per block. process() must never allocate, lock or touch the disk. */
class Effect
{
public:
    static constexpr int maxParams = 32;

    virtual ~Effect() = default;

    virtual juce::String getType() const = 0;
    virtual juce::String getDisplayName() const = 0;

    /** Called off the audio thread. May allocate. */
    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() {}

    /** Real-time safe. sidechain may be null. */
    virtual void process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain) = 0;

    virtual int getLatencySamples() const { return 0; }
    virtual bool usesSidechain() const { return false; }

    /** Gain reduction in dB for meters (0 when not applicable). */
    virtual float getGainReductionDb() const { return 0.0f; }

    const std::vector<ParamInfo>& getParams() const noexcept { return infos; }
    int indexOf (const juce::String& id) const noexcept;
    void setParam (const juce::String& id, float value) noexcept;
    float getParam (const juce::String& id) const noexcept;
    void setParamByIndex (int index, float value) noexcept;
    float getParamByIndex (int index) const noexcept;
    void resetParamsToDefaults() noexcept;

    bool bypassed() const noexcept { return bypass.load (std::memory_order_relaxed); }
    void setBypassed (bool b) noexcept { bypass.store (b, std::memory_order_relaxed); }

protected:
    /** Registers a parameter; returns its index. Constructor only. */
    int addParam (ParamInfo info);
    inline float p (int index) const noexcept { return values[(size_t) index].load (std::memory_order_relaxed); }

    /** Bumped on every parameter change so effects can recompute coefficients lazily. */
    bool paramsChangedSince (juce::uint32& seen) const noexcept
    {
        auto v = changeCounter.load (std::memory_order_acquire);
        if (v == seen) return false;
        seen = v;
        return true;
    }

    double sampleRate = 48000;
    int maxBlock = 512;
    int channels = 2;

private:
    std::vector<ParamInfo> infos;
    std::array<std::atomic<float>, maxParams> values {};
    std::atomic<juce::uint32> changeCounter { 1 };
    std::atomic<bool> bypass { false };
};

/** Creates any built-in effect by type id. Returns nullptr for unknown types. */
std::unique_ptr<Effect> createEffect (const juce::String& type);

/** The effect types the mixer offers, with display names. */
std::vector<std::pair<juce::String, juce::String>> availableEffectTypes();

} // namespace studio::dsp
