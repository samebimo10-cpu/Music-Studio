#include "Effect.h"
#include "BuiltinEffects.h"
#include "SpectralEffects.h"
#include "VoiceEffects.h"

namespace studio::dsp
{

int Effect::addParam (ParamInfo info)
{
    jassert ((int) infos.size() < maxParams);
    const int idx = (int) infos.size();
    values[(size_t) idx].store (info.defaultValue);
    infos.push_back (std::move (info));
    return idx;
}

int Effect::indexOf (const juce::String& id) const noexcept
{
    for (size_t i = 0; i < infos.size(); ++i)
        if (infos[i].id == id) return (int) i;
    return -1;
}

void Effect::setParam (const juce::String& id, float value) noexcept
{
    setParamByIndex (indexOf (id), value);
}

float Effect::getParam (const juce::String& id) const noexcept
{
    return getParamByIndex (indexOf (id));
}

void Effect::setParamByIndex (int index, float value) noexcept
{
    if (index < 0 || index >= (int) infos.size())
        return;
    auto& info = infos[(size_t) index];
    values[(size_t) index].store (juce::jlimit (info.minValue, info.maxValue, value), std::memory_order_relaxed);
    changeCounter.fetch_add (1, std::memory_order_release);
}

float Effect::getParamByIndex (int index) const noexcept
{
    if (index < 0 || index >= (int) infos.size())
        return 0.0f;
    return values[(size_t) index].load (std::memory_order_relaxed);
}

void Effect::resetParamsToDefaults() noexcept
{
    for (size_t i = 0; i < infos.size(); ++i)
        setParamByIndex ((int) i, infos[i].defaultValue);
}

//==============================================================================
std::unique_ptr<Effect> createEffect (const juce::String& type)
{
    if (type == "eq")            return std::make_unique<EqEffect>();
    if (type == "compressor")    return std::make_unique<CompressorEffect>();
    if (type == "gate")          return std::make_unique<GateEffect>();
    if (type == "limiter")       return std::make_unique<LimiterEffect>();
    if (type == "reverb")        return std::make_unique<ReverbEffect>();
    if (type == "delay")         return std::make_unique<DelayEffect>();
    if (type == "chorus")        return std::make_unique<ChorusEffect>();
    if (type == "saturation")    return std::make_unique<SaturationEffect>();
    if (type == "widener")       return std::make_unique<WidenerEffect>();
    if (type == "deesser")       return std::make_unique<DeEsserEffect>();
    if (type == "gain")          return std::make_unique<GainEffect>();
    if (type == "denoise")       return std::make_unique<NoiseReductionEffect>();
    if (type == "dereverb")      return std::make_unique<DeReverbEffect>();
    if (type == "breath")        return std::make_unique<BreathControlEffect>();
    if (type == "pitchcorrect")  return std::make_unique<PitchCorrectEffect>();
    if (type == "doubler")       return std::make_unique<DoublerEffect>();
    return nullptr;
}

std::vector<std::pair<juce::String, juce::String>> availableEffectTypes()
{
    return {
        { "eq", "EQ" }, { "compressor", "Compressor" }, { "gate", "Gate" }, { "limiter", "Limiter" },
        { "reverb", "Reverb" }, { "delay", "Delay" }, { "chorus", "Chorus" }, { "saturation", "Saturation" },
        { "widener", "Stereo widener" }, { "deesser", "De-esser" }, { "gain", "Gain" },
        { "denoise", "Noise removal" }, { "dereverb", "De-reverb" }, { "breath", "Breath & click control" },
        { "pitchcorrect", "Pitch correction" }, { "doubler", "Doubler" }
    };
}

} // namespace studio::dsp
