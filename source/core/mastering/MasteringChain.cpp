#include "MasteringChain.h"

namespace studio
{

MasteringChain::MasteringChain()
{
    addParam ({ "inputGain", "Input gain", -12, 24, 0, "dB" });
    addParam ({ "lowShelfGain", "Low", -6, 6, 0, "dB" });
    addParam ({ "lowShelfFreq", "Low freq", 40, 300, 100, "Hz", {}, true });
    addParam ({ "midGain", "Mid", -6, 6, 0, "dB" });
    addParam ({ "midFreq", "Mid freq", 200, 5000, 800, "Hz", {}, true });
    addParam ({ "highShelfGain", "High", -6, 6, 0, "dB" });
    addParam ({ "highShelfFreq", "High freq", 3000, 16000, 9000, "Hz", {}, true });
    addParam ({ "lowXover", "Low/mid split", 60, 400, 150, "Hz", {}, true });
    addParam ({ "highXover", "Mid/high split", 1500, 10000, 4000, "Hz", {}, true });
    addParam ({ "lowThresh", "Low threshold", -40, 0, -18, "dB" });
    addParam ({ "lowRatio", "Low ratio", 1, 8, 2, ":1" });
    addParam ({ "midThresh", "Mid threshold", -40, 0, -18, "dB" });
    addParam ({ "midRatio", "Mid ratio", 1, 8, 1.6f, ":1" });
    addParam ({ "highThresh", "High threshold", -40, 0, -20, "dB" });
    addParam ({ "highRatio", "High ratio", 1, 8, 1.8f, ":1" });
    addParam ({ "mbAttack", "Attack", 1, 100, 20, "ms", {}, true });
    addParam ({ "mbRelease", "Release", 20, 1000, 200, "ms", {}, true });
    addParam ({ "width", "Width", 0, 2, 1.05f, "" });
    addParam ({ "bassMonoFreq", "Mono bass below", 20, 300, 120, "Hz", {}, true });
    addParam ({ "satDrive", "Warmth", 0, 12, 2, "dB" });
    addParam ({ "satMix", "Warmth mix", 0, 1, 0.3f, "%" });
    addParam ({ "ceiling", "Ceiling", -3, 0, -1, "dBTP" });
    addParam ({ "limiterRelease", "Limiter release", 10, 500, 80, "ms", {}, true });
    addParam ({ "outputGain", "Output", -12, 0, 0, "dB" });
}

void MasteringChain::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    saturation.prepare (sr, block, ch);
    limiter.prepare (sr, block, ch);
    seen = 0;
    updateFilters();
    reset();
}

void MasteringChain::reset()
{
    for (auto& c : eqFilters) for (auto& f : c) f.reset();
    for (auto& x : xLow) x.reset();
    for (auto& x : xHigh) x.reset();
    for (auto& f : lowAllpass) f.reset();
    sideHp.reset();
    bandEnv.fill (0);
    bandGr.fill (0);
    saturation.reset();
    limiter.reset();
}

void MasteringChain::updateFilters()
{
    std::array<dsp::Biquad, 3> b;
    b[0].setCoefficients (dsp::Biquad::Type::LowShelf, sampleRate, p (lowShelfFreq), 0.7071, p (lowShelfGain));
    b[1].setCoefficients (dsp::Biquad::Type::Peak, sampleRate, p (midFreq), 0.8, p (midGain));
    b[2].setCoefficients (dsp::Biquad::Type::HighShelf, sampleRate, p (highShelfFreq), 0.7071, p (highShelfGain));
    for (auto& c : eqFilters)
        for (int i = 0; i < 3; ++i) c[(size_t) i].copyCoefficientsFrom (b[(size_t) i]);

    for (auto& x : xLow) x.setup (sampleRate, p (lowXover));
    for (auto& x : xHigh) x.setup (sampleRate, p (highXover));
    // A 2nd-order all-pass at the upper crossover keeps the bands summing flat-ish.
    for (auto& f : lowAllpass) f.setCoefficients (dsp::Biquad::Type::AllPass, sampleRate, p (highXover), 0.7071);
    sideHp.setCoefficients (dsp::Biquad::Type::HighPass, sampleRate, p (bassMonoFreq), 0.7071);

    saturation.setParam ("drive", p (satDrive));
    saturation.setParam ("mix", p (satMix));
    saturation.setParam ("character", 0);
    limiter.setParam ("ceiling", p (ceiling));
    limiter.setParam ("release", p (limiterRelease));
    limiter.setParam ("truePeak", 1);
}

std::array<float, 3> MasteringChain::getBandGainReductionDb() const noexcept
{
    return { -bandGr[0], -bandGr[1], -bandGr[2] };
}

void MasteringChain::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    if (paramsChangedSince (seen))
        updateFilters();

    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (2, buffer.getNumChannels());
    buffer.applyGain (dsp::dbToGain (p (inputGain)));

    const float att = dsp::timeCoeff (p (mbAttack), sampleRate);
    const float rel = dsp::timeCoeff (p (mbRelease), sampleRate);
    const float thr[3] { p (lowThresh), p (midThresh), p (highThresh) };
    const float rat[3] { p (lowRatio), p (midRatio), p (highRatio) };
    const float w = p (width);
    std::array<float, 3> maxGr {};

    for (int i = 0; i < n; ++i)
    {
        float bands[2][3];
        for (int ch = 0; ch < nch; ++ch)
        {
            float x = buffer.getSample (ch, i);
            for (auto& f : eqFilters[(size_t) ch]) x = f.process (x);

            float low, rest, mid, high;
            xLow[(size_t) ch].process (x, low, rest);
            xHigh[(size_t) ch].process (rest, mid, high);
            low = lowAllpass[(size_t) ch].process (low);
            bands[ch][0] = low; bands[ch][1] = mid; bands[ch][2] = high;
        }
        if (nch == 1)
            for (int b = 0; b < 3; ++b) bands[1][b] = bands[0][b];

        // Linked-stereo compression per band.
        float out[2] = { 0, 0 };
        for (int b = 0; b < 3; ++b)
        {
            const float level = std::max (std::abs (bands[0][b]), std::abs (bands[1][b]));
            const float inDb = dsp::gainToDb (level);
            const float over = inDb - thr[b];
            const float target = over > 0 ? over * (1.0f - 1.0f / rat[b]) : 0.0f;
            auto& e = bandEnv[(size_t) b];
            e = target > e ? att * e + (1 - att) * target : rel * e + (1 - rel) * target;
            maxGr[(size_t) b] = std::max (maxGr[(size_t) b], e);
            const float g = dsp::dbToGain (-e);
            out[0] += bands[0][b] * g;
            out[1] += bands[1][b] * g;
        }

        // Imaging: widen the sides, keep the low end mono.
        if (nch == 2)
        {
            const float m = 0.5f * (out[0] + out[1]);
            const float s = sideHp.process (0.5f * (out[0] - out[1])) * w;
            out[0] = m + s;
            out[1] = m - s;
        }

        for (int ch = 0; ch < nch; ++ch)
            buffer.setSample (ch, i, out[ch]);
    }
    bandGr = maxGr;

    saturation.process (buffer, nullptr);
    buffer.applyGain (dsp::dbToGain (p (outputGain)));
    limiter.process (buffer, nullptr);
}

juce::StringArray MasteringChain::presetNames()
{
    return { "Warm", "Bright", "Loud", "Gentle worship" };
}

std::map<juce::String, float> MasteringChain::presetValues (const juce::String& preset)
{
    std::map<juce::String, float> v {
        { "lowShelfGain", 0 }, { "midGain", 0 }, { "highShelfGain", 0 },
        { "lowThresh", -18 }, { "lowRatio", 2 }, { "midThresh", -18 }, { "midRatio", 1.6f },
        { "highThresh", -20 }, { "highRatio", 1.8f }, { "mbAttack", 20 }, { "mbRelease", 200 },
        { "width", 1.05f }, { "satDrive", 2 }, { "satMix", 0.3f }, { "limiterRelease", 80 },
    };
    auto set = [&v] (std::initializer_list<std::pair<const char*, float>> c) { for (auto& [k, val] : c) v[k] = val; };

    if (preset == "Warm")
        set ({ { "lowShelfGain", 1.5f }, { "highShelfGain", -0.5f }, { "midGain", -0.5f }, { "midFreq", 2500 }, { "satDrive", 4 }, { "satMix", 0.45f } });
    else if (preset == "Bright")
        set ({ { "lowShelfGain", -0.5f }, { "highShelfGain", 2.0f }, { "midGain", 0.8f }, { "midFreq", 3000 }, { "width", 1.15f } });
    else if (preset == "Loud")
        set ({ { "lowThresh", -22 }, { "lowRatio", 3 }, { "midThresh", -22 }, { "midRatio", 2.5f }, { "highThresh", -24 },
               { "highRatio", 2.5f }, { "mbAttack", 10 }, { "satDrive", 5 }, { "satMix", 0.5f }, { "limiterRelease", 50 } });
    else if (preset == "Gentle worship")
        set ({ { "lowShelfGain", 0.8f }, { "highShelfGain", 1.0f }, { "lowRatio", 1.5f }, { "midRatio", 1.3f }, { "highRatio", 1.4f },
               { "mbAttack", 35 }, { "mbRelease", 350 }, { "width", 1.12f }, { "satDrive", 1.5f }, { "satMix", 0.2f }, { "limiterRelease", 150 } });
    return v;
}

void MasteringChain::applySettings (const MasteringSettings& settings)
{
    resetParamsToDefaults();
    for (auto& [k, val] : presetValues (settings.preset)) setParam (k, val);
    setParam ("ceiling", settings.ceilingDbtp);
    for (auto& [k, val] : settings.params) setParam (k, val);
}

//==============================================================================
namespace mastering
{
    std::array<float, analysis::SpectrumProfile::numBands> referenceTilt (const juce::String& style)
    {
        // Relative octave-band levels of finished masters (31.5 Hz .. 16 kHz), by style family.
        // Bass-heavy styles (Afrobeats, amapiano, hip hop) carry more sub; worship sits flatter and airier.
        if (style.containsIgnoreCase ("amapiano") || style.containsIgnoreCase ("trap") || style.containsIgnoreCase ("hip"))
            return { 6, 9, 7, 3, 1, -1, -3, -5, -8, -14 };
        if (style.containsIgnoreCase ("afro") || style.containsIgnoreCase ("dancehall") || style.containsIgnoreCase ("reggae"))
            return { 4, 8, 6, 3, 1, -1, -3, -5, -8, -13 };
        if (style.containsIgnoreCase ("worship") || style.containsIgnoreCase ("ballad"))
            return { 1, 5, 5, 3, 1, -1, -3, -4, -7, -11 };
        return { 2, 6, 6, 3, 1, -1, -3, -5, -8, -12 };
    }

    juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& mix, double sr, const MasteringSettings& settings)
    {
        MasteringChain chain;
        const int block = 1024;
        chain.prepare (sr, block, juce::jmax (2, mix.getNumChannels()));
        chain.applySettings (settings);

        const int latency = chain.getLatencySamples();
        const int total = mix.getNumSamples() + latency;
        juce::AudioBuffer<float> work (2, total);
        work.clear();
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, mix, juce::jmin (ch, mix.getNumChannels() - 1), 0, mix.getNumSamples());

        for (int pos = 0; pos < total; pos += block)
        {
            const int len = juce::jmin (block, total - pos);
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, pos, len);
            chain.process (view, nullptr);
        }

        // Drop the limiter look-ahead so the master lines up with the mix.
        juce::AudioBuffer<float> out (2, mix.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, 0, work, ch, latency, mix.getNumSamples());
        return out;
    }

    MasteringSettings fitLoudness (const juce::AudioBuffer<float>& mix, double sr, MasteringSettings settings,
                                   LoudnessMeter::Summary* resultOut)
    {
        // Measure on a representative excerpt for speed: the loudest 60 s (or all).
        const int excerptLen = juce::jmin (mix.getNumSamples(), (int) (60 * sr));
        int bestStart = 0;
        if (excerptLen < mix.getNumSamples())
        {
            double bestE = -1;
            const int step = (int) (5 * sr);
            for (int s = 0; s + excerptLen <= mix.getNumSamples(); s += step)
            {
                double e = 0;
                for (int ch = 0; ch < mix.getNumChannels(); ++ch)
                    e += mix.getRMSLevel (ch, s, excerptLen);
                if (e > bestE) { bestE = e; bestStart = s; }
            }
        }
        juce::AudioBuffer<float> excerpt (mix.getNumChannels(), excerptLen);
        for (int ch = 0; ch < mix.getNumChannels(); ++ch)
            excerpt.copyFrom (ch, 0, mix, ch, bestStart, excerptLen);

        float gain = settings.params.count ("inputGain") ? settings.params["inputGain"] : 0.0f;
        LoudnessMeter::Summary s;
        for (int iter = 0; iter < 6; ++iter)
        {
            settings.params["inputGain"] = gain;
            auto out = render (excerpt, sr, settings);
            s = LoudnessMeter::measure (out, sr);
            const float err = settings.targetLufs - s.integratedLufs;
            if (std::abs (err) < 0.2f || s.integratedLufs <= -100.0f)
                break;
            // The limiter makes gain->loudness less than 1:1 near the ceiling; damp the step.
            gain = juce::jlimit (-12.0f, 24.0f, gain + err * (iter == 0 ? 1.0f : 0.8f));
        }

        if (resultOut != nullptr)
        {
            auto full = render (mix, sr, settings);
            *resultOut = LoudnessMeter::measure (full, sr);
        }
        return settings;
    }

    Suggestion suggest (const juce::AudioBuffer<float>& mix, double sr, const juce::String& style,
                        float targetLufs, float ceilingDbtp, const juce::String& basePreset)
    {
        Suggestion sug;
        sug.before = LoudnessMeter::measure (mix, sr);
        auto prof = analysis::analyseSpectrum (mix, sr);
        auto tilt = prof.tilt();
        auto ref = referenceTilt (style);
        float refMean = 0;
        for (auto v : ref) refMean += v;
        refMean /= (float) ref.size();

        auto diff = [&] (int band) { return (ref[(size_t) band] - refMean) - tilt[(size_t) band]; };

        auto& s = sug.settings;
        s.preset = basePreset;
        s.targetLufs = targetLufs;
        s.ceilingDbtp = ceilingDbtp;
        s.params = MasteringChain::presetValues (basePreset);

        // Tone: nudge the shelves towards the reference (at most 3 dB, half the difference).
        const float lowDiff = 0.5f * (diff (1) + diff (2));
        const float midDiff = 0.5f * (diff (5) + diff (6));
        const float highDiff = 0.5f * (diff (8) + diff (9));
        s.params["lowShelfGain"] = juce::jlimit (-3.0f, 3.0f, s.params["lowShelfGain"] + 0.5f * lowDiff);
        s.params["midGain"] = juce::jlimit (-3.0f, 3.0f, s.params["midGain"] + 0.4f * midDiff);
        s.params["highShelfGain"] = juce::jlimit (-3.0f, 3.0f, s.params["highShelfGain"] + 0.5f * highDiff);

        if (lowDiff > 1.5f)       sug.notes.add ("The low end is a little light for this style, so the master adds some bass.");
        else if (lowDiff < -1.5f) sug.notes.add ("The low end is heavy, so the master trims some bass to keep it clean.");
        if (highDiff > 1.5f)      sug.notes.add ("The mix is slightly dark; the master adds air on top.");
        else if (highDiff < -1.5f) sug.notes.add ("The top end is bright; the master softens it to avoid harshness.");

        // Dynamics: very dynamic mixes get gentler compression first; squashed mixes get less.
        if (prof.crestDb > 18.0f)
        {
            s.params["midRatio"] = s.params["midRatio"] + 0.4f;
            s.params["lowRatio"] = s.params["lowRatio"] + 0.5f;
            sug.notes.add ("The mix is very dynamic, so the master compresses a bit more to hold it together.");
        }
        else if (prof.crestDb < 9.0f)
        {
            s.params["midRatio"] = 1.2f;
            s.params["lowRatio"] = 1.3f;
            s.params["highRatio"] = 1.2f;
            sug.notes.add ("The mix is already compressed, so the master stays light to keep it breathing.");
        }

        // Width: narrow mixes get a gentle widen; wide/phasey mixes are pulled in.
        if (prof.stereoCorrelation < 0.2f)
        {
            s.params["width"] = 0.85f;
            sug.notes.add ("The stereo image is very wide (it may vanish on mono phone speakers), so the master narrows it.");
        }
        else if (prof.stereoWidth < 0.05f)
        {
            s.params["width"] = 1.2f;
            sug.notes.add ("The mix is close to mono, so the master widens it slightly.");
        }

        s = fitLoudness (mix, sr, s, &sug.after);
        sug.notes.add (juce::String::formatted ("Loudness set to %.1f LUFS with peaks at or below %.1f dBTP.",
                                                (double) sug.after.integratedLufs, (double) ceilingDbtp));
        return sug;
    }
}

} // namespace studio
