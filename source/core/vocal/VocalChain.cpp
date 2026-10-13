#include "VocalChain.h"

namespace studio
{

VocalChain::VocalChain()
{
    addParam ({ "amount", "Amount", 0, 1, 0.7f, "%", {}, false, "One knob for the whole chain: 0 = untouched, 100% = full preset" });
    addParam ({ "reverbSend", "Reverb send", 0, 1, 0.2f, "%" });
    addParam ({ "delaySend", "Delay send", 0, 1, 0.1f, "%" });

    steps = {
        { "denoise",    "Noise removal",           "Removes hum, fans, generator noise and room hiss", { &denoise } },
        { "dereverb",   "De-reverb",               "Reduces the echo of an untreated room", { &dereverb } },
        { "breath",     "Breath & click control",  "Turns breaths and mouth clicks down instead of cutting them", { &breath } },
        { "pitch",      "Pitch correction",        "Corrects to the song's key, from natural to robotic", { &pitch } },
        { "eq",         "EQ",                      "Removes rumble, adds clarity and air", { &eq } },
        { "comp",       "Compression",             "Keeps loud and soft lines at an even level", { &comp } },
        { "deesser",    "De-esser",                "Softens harsh 's' and 'sh' sounds", { &deesser } },
        { "saturation", "Saturation",              "Adds warmth", { &saturation } },
        { "space",      "Reverb & delay",          "Places the voice in a space", { &reverb, &delay } },
    };

    for (size_t i = 0; i < steps.size(); ++i)
    {
        bypassStorage[i].store (false);
        bypassFlags[steps[i].id] = &bypassStorage[i];
    }

    // Send effects run 100 % wet on a parallel bus.
    reverb.setParam ("mix", 1.0f);
    delay.setParam ("mix", 1.0f);
}

void VocalChain::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = juce::jmax (2, ch);
    for (auto& s : steps)
        for (auto* e : s.effects)
            e->prepare (sr, block, channels);
    sendBuffer.setSize (channels, block);
    delayBuffer.setSize (channels, block);
    reset();
}

void VocalChain::reset()
{
    for (auto& s : steps)
        for (auto* e : s.effects)
            e->reset();
}

int VocalChain::getLatencySamples() const
{
    return denoise.getLatencySamples() + dereverb.getLatencySamples() + breath.getLatencySamples() + pitch.getLatencySamples();
}

const VocalChain::Step* VocalChain::findStep (const juce::String& id) const
{
    for (auto& s : steps)
        if (s.id == id) return &s;
    return nullptr;
}

void VocalChain::setStepBypassed (const juce::String& id, bool b)
{
    if (auto it = bypassFlags.find (id); it != bypassFlags.end())
        it->second->store (b);
}

bool VocalChain::isStepBypassed (const juce::String& id) const
{
    if (auto it = bypassFlags.find (id); it != bypassFlags.end())
        return it->second->load();
    return false;
}

dsp::Effect* VocalChain::effectForStep (const juce::String& stepId, int index) const
{
    if (auto* s = findStep (stepId))
        if (index >= 0 && index < (int) s->effects.size())
            return s->effects[(size_t) index];
    return nullptr;
}

void VocalChain::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sc)
{
    const int n = buffer.getNumSamples();

    // Latency-bearing steps always run; when bypassed their processing is
    // neutralised via parameters (see applySettings), keeping timing constant.
    denoise.process (buffer, sc);
    dereverb.process (buffer, sc);
    breath.process (buffer, sc);
    pitch.process (buffer, sc);

    if (! isStepBypassed ("eq"))         eq.process (buffer, sc);
    if (! isStepBypassed ("comp"))       comp.process (buffer, sc);
    if (! isStepBypassed ("deesser"))    deesser.process (buffer, sc);
    if (! isStepBypassed ("saturation")) saturation.process (buffer, sc);

    if (! isStepBypassed ("space") && buffer.getNumChannels() >= 1)
    {
        const float rs = p (1) * p (0), ds = p (2) * p (0);
        if (rs > 0.0001f || ds > 0.0001f)
        {
            const int nch = juce::jmin (buffer.getNumChannels(), sendBuffer.getNumChannels());
            for (int ch = 0; ch < nch; ++ch)
            {
                sendBuffer.copyFrom (ch, 0, buffer, ch, 0, n);
                delayBuffer.copyFrom (ch, 0, buffer, ch, 0, n);
            }
            juce::AudioBuffer<float> rv (sendBuffer.getArrayOfWritePointers(), nch, n);
            juce::AudioBuffer<float> dl (delayBuffer.getArrayOfWritePointers(), nch, n);
            delay.process (dl, nullptr);
            // Delay feeds the reverb a little, as on a real send setup.
            for (int ch = 0; ch < nch; ++ch)
                rv.addFrom (ch, 0, dl, ch, 0, n, 0.3f);
            reverb.process (rv, nullptr);
            for (int ch = 0; ch < nch; ++ch)
            {
                // The reverb leaves (1 - mix/2) dry in; at mix=1 that is half the dry, so remove it.
                rv.addFrom (ch, 0, buffer, ch, 0, n, -0.5f);
                // The delay at mix 1 is dry + wet; keep only the wet part.
                dl.addFrom (ch, 0, buffer, ch, 0, n, -1.0f);
                buffer.addFrom (ch, 0, rv, ch, 0, n, rs);
                buffer.addFrom (ch, 0, dl, ch, 0, n, ds);
            }
        }
    }
    reverbSend = p (1);
    delaySend = p (2);
}

//==============================================================================
juce::StringArray VocalChain::presetNames()
{
    return { "Worship lead", "Praise lead", "Afro pop", "Rap", "Spoken word", "Choir stack", "Podcast voice" };
}

std::map<juce::String, float> VocalChain::presetValues (const juce::String& preset)
{
    // Shared starting point, then each preset adjusts it.
    std::map<juce::String, float> v {
        { "denoise.amount", 15 }, { "denoise.sensitivity", 1.5f },
        { "dereverb.amount", 0.35f }, { "dereverb.roomDecay", 0.6f },
        { "breath.breath", 8 }, { "breath.click", 0.5f },
        { "pitch.strength", 0.6f }, { "pitch.speed", 120 }, { "pitch.humanize", 0.4f },
        { "eq.hpOn", 1 }, { "eq.hpFreq", 90 }, { "eq.p1Freq", 300 }, { "eq.p1Gain", -2.5f }, { "eq.p1Q", 1.2f },
        { "eq.p2Freq", 3200 }, { "eq.p2Gain", 2 }, { "eq.p2Q", 1.0f }, { "eq.highFreq", 11000 }, { "eq.highGain", 3 },
        { "comp.threshold", -20 }, { "comp.ratio", 3 }, { "comp.attack", 8 }, { "comp.release", 120 }, { "comp.makeup", 4 },
        { "deesser.frequency", 6500 }, { "deesser.threshold", -30 }, { "deesser.range", 8 },
        { "saturation.drive", 4 }, { "saturation.mix", 0.4f }, { "saturation.character", 0 },
        { "reverb.type", 1 }, { "reverb.decay", 2.4f }, { "reverb.preDelay", 30 }, { "reverb.damping", 0.4f },
        { "delay.sync", 2 }, { "delay.feedback", 0.25f },
        { "chain.reverbSend", 0.25f }, { "chain.delaySend", 0.08f },
    };

    auto set = [&v] (std::initializer_list<std::pair<const char*, float>> changes)
    {
        for (auto& [k, val] : changes) v[k] = val;
    };

    if (preset == "Worship lead")
        set ({ { "reverb.type", 2 }, { "reverb.decay", 3.2f }, { "reverb.preDelay", 40 }, { "chain.reverbSend", 0.32f },
               { "delay.sync", 3 }, { "chain.delaySend", 0.12f }, { "pitch.speed", 150 }, { "eq.highGain", 3.5f } });
    else if (preset == "Praise lead")
        set ({ { "comp.ratio", 4 }, { "comp.threshold", -22 }, { "eq.p2Gain", 3 }, { "reverb.type", 1 }, { "reverb.decay", 1.8f },
               { "chain.reverbSend", 0.2f }, { "saturation.drive", 6 }, { "pitch.speed", 90 } });
    else if (preset == "Afro pop")
        set ({ { "pitch.strength", 0.9f }, { "pitch.speed", 25 }, { "pitch.humanize", 0.1f }, { "comp.ratio", 4 },
               { "eq.highGain", 4.5f }, { "reverb.type", 3 }, { "reverb.decay", 1.4f }, { "chain.reverbSend", 0.18f },
               { "delay.sync", 4 }, { "chain.delaySend", 0.14f }, { "saturation.drive", 5 } });
    else if (preset == "Rap")
        set ({ { "pitch.strength", 0.2f }, { "comp.ratio", 5 }, { "comp.threshold", -24 }, { "comp.attack", 4 },
               { "eq.hpFreq", 110 }, { "eq.p2Freq", 2500 }, { "eq.p2Gain", 3.5f }, { "reverb.type", 0 }, { "reverb.decay", 0.8f },
               { "chain.reverbSend", 0.08f }, { "chain.delaySend", 0.05f }, { "saturation.drive", 7 }, { "saturation.character", 1 } });
    else if (preset == "Spoken word")
        set ({ { "pitch.strength", 0 }, { "dereverb.amount", 0.5f }, { "comp.ratio", 3 }, { "eq.hpFreq", 80 },
               { "eq.highGain", 1.5f }, { "reverb.type", 0 }, { "reverb.decay", 0.6f }, { "chain.reverbSend", 0.06f },
               { "chain.delaySend", 0 }, { "saturation.mix", 0.25f } });
    else if (preset == "Choir stack")
        set ({ { "pitch.strength", 0.5f }, { "pitch.speed", 200 }, { "comp.ratio", 2.5f }, { "comp.threshold", -18 },
               { "eq.hpFreq", 140 }, { "eq.p1Gain", -4 }, { "eq.highGain", 2 }, { "deesser.range", 12 },
               { "reverb.type", 2 }, { "reverb.decay", 3.8f }, { "chain.reverbSend", 0.38f }, { "chain.delaySend", 0 } });
    else if (preset == "Podcast voice")
        set ({ { "pitch.strength", 0 }, { "denoise.amount", 22 }, { "dereverb.amount", 0.6f }, { "breath.breath", 12 },
               { "comp.ratio", 4 }, { "comp.threshold", -24 }, { "eq.hpFreq", 75 }, { "eq.p2Freq", 2800 }, { "eq.p2Gain", 2.5f },
               { "reverb.decay", 0.4f }, { "chain.reverbSend", 0 }, { "chain.delaySend", 0 }, { "saturation.mix", 0.2f } });
    return v;
}

void VocalChain::applySettings (const VocalChainSettings& settings, const Key& songKey)
{
    const float amount = juce::jlimit (0.0f, 1.0f, settings.amount);
    setParam ("amount", 1.0f); // the amount is folded into each parameter below

    // Neutral values (amount = 0) for parameters that should fade out; anything
    // not listed here (frequencies, times, types) takes the preset value as is.
    static const std::map<juce::String, float> neutral {
        { "denoise.amount", 0 }, { "dereverb.amount", 0 }, { "breath.breath", 0 }, { "breath.click", 0 },
        { "pitch.strength", 0 }, { "eq.p1Gain", 0 }, { "eq.p2Gain", 0 }, { "eq.highGain", 0 }, { "eq.lowGain", 0 },
        { "comp.ratio", 1 }, { "comp.makeup", 0 }, { "deesser.range", 0 }, { "saturation.mix", 0 },
        { "chain.reverbSend", 0 }, { "chain.delaySend", 0 },
    };

    auto values = presetValues (settings.preset);
    for (auto& [k, v] : values)
        if (auto it = neutral.find (k); it != neutral.end())
            v = it->second + (v - it->second) * amount;

    for (auto& [k, v] : settings.params)  // advanced-view overrides win
        values[k] = v;

    for (auto& [k, v] : values)
    {
        auto dot = k.indexOfChar ('.');
        auto stepName = k.substring (0, dot);
        auto param = k.substring (dot + 1);
        dsp::Effect* target = nullptr;
        if (stepName == "chain")           { setParam (param, v); continue; }
        if (stepName == "denoise")         target = &denoise;
        else if (stepName == "dereverb")   target = &dereverb;
        else if (stepName == "breath")     target = &breath;
        else if (stepName == "pitch")      target = &pitch;
        else if (stepName == "eq")         target = &eq;
        else if (stepName == "comp")       target = &comp;
        else if (stepName == "deesser")    target = &deesser;
        else if (stepName == "saturation") target = &saturation;
        else if (stepName == "reverb")     target = &reverb;
        else if (stepName == "delay")      target = &delay;
        if (target != nullptr) target->setParam (param, v);
    }

    pitch.setParam ("tonic", (float) songKey.tonic);
    pitch.setParam ("scale", (float) (int) songKey.scale);
    reverb.setParam ("mix", 1.0f);
    delay.setParam ("mix", 1.0f);

    for (auto& s : steps)
    {
        bool b = false;
        if (auto it = settings.bypass.find (s.id); it != settings.bypass.end()) b = it->second;
        setStepBypassed (s.id, b);
    }

    // Latency-bearing steps stay in the path; bypass them by neutralising.
    if (isStepBypassed ("denoise"))  { denoise.setParam ("amount", 0); denoise.setParam ("hum", 0); }
    if (isStepBypassed ("dereverb")) dereverb.setParam ("amount", 0);
    if (isStepBypassed ("breath"))   { breath.setParam ("breath", 0); breath.setParam ("click", 0); }
    if (isStepBypassed ("pitch"))    pitch.setParam ("strength", 0);
}

} // namespace studio
