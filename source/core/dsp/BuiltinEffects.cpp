#include "BuiltinEffects.h"
#include "TruePeak.h"

namespace studio::dsp
{

//==============================================================================
// EQ
//==============================================================================
EqEffect::EqEffect()
{
    addParam ({ "hpOn", "Low cut", 0, 1, 1, "", { "Off", "On" }, false, "Removes rumble below the low-cut frequency" });
    addParam ({ "hpFreq", "Low cut freq", 20, 400, 80, "Hz", {}, true });
    addParam ({ "lowFreq", "Low shelf freq", 40, 400, 120, "Hz", {}, true });
    addParam ({ "lowGain", "Low shelf", -15, 15, 0, "dB" });
    addParam ({ "p1Freq", "Mid 1 freq", 100, 2000, 300, "Hz", {}, true });
    addParam ({ "p1Gain", "Mid 1", -15, 15, 0, "dB", {}, false, "Cut around 250-400 Hz to remove muddiness" });
    addParam ({ "p1Q", "Mid 1 Q", 0.3f, 8, 1.0f, "" });
    addParam ({ "p2Freq", "Mid 2 freq", 800, 8000, 3000, "Hz", {}, true });
    addParam ({ "p2Gain", "Mid 2", -15, 15, 0, "dB", {}, false, "Boost around 3 kHz for clarity" });
    addParam ({ "p2Q", "Mid 2 Q", 0.3f, 8, 1.0f, "" });
    addParam ({ "highFreq", "Air freq", 4000, 16000, 10000, "Hz", {}, true });
    addParam ({ "highGain", "Air", -15, 15, 0, "dB", {}, false, "High shelf that adds air" });
    addParam ({ "lpOn", "High cut", 0, 1, 0, "", { "Off", "On" } });
    addParam ({ "lpFreq", "High cut freq", 2000, 20000, 18000, "Hz", {}, true });
    addParam ({ "outGain", "Output", -12, 12, 0, "dB" });
}

void EqEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    seen = 0;
    updateCoefficients();
    reset();
}

void EqEffect::reset()
{
    for (auto& chFilters : filters)
        for (auto& f : chFilters) f.reset();
}

void EqEffect::updateCoefficients()
{
    std::array<Biquad, numBands> b;
    if (p (hpOn) > 0.5f) b[0].setCoefficients (Biquad::Type::HighPass, sampleRate, p (hpFreq), 0.7071);
    b[1].setCoefficients (Biquad::Type::LowShelf, sampleRate, p (lowFreq), 0.7071, p (lowGain));
    b[2].setCoefficients (Biquad::Type::Peak, sampleRate, p (p1Freq), p (p1Q), p (p1Gain));
    b[3].setCoefficients (Biquad::Type::Peak, sampleRate, p (p2Freq), p (p2Q), p (p2Gain));
    b[4].setCoefficients (Biquad::Type::HighShelf, sampleRate, p (highFreq), 0.7071, p (highGain));
    if (p (lpOn) > 0.5f) b[5].setCoefficients (Biquad::Type::LowPass, sampleRate, p (lpFreq), 0.7071);

    // Copy coefficients only, so each channel keeps its state and nothing clicks.
    for (auto& chFilters : filters)
        for (int i = 0; i < numBands; ++i)
            chFilters[(size_t) i].copyCoefficientsFrom (b[(size_t) i]);
    display = b;
}

double EqEffect::responseDb (double freq) const
{
    double db = p (outGain);
    for (auto& f : display) db += f.magnitudeDb (freq, sampleRate);
    return db;
}

void EqEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    if (paramsChangedSince (seen))
        updateCoefficients();

    const float out = dbToGain (p (outGain));
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        auto& f = filters[(size_t) ch];
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float x = d[i];
            for (auto& band : f) x = band.process (x);
            d[i] = x * out;
        }
    }
}

//==============================================================================
// Compressor
//==============================================================================
CompressorEffect::CompressorEffect()
{
    addParam ({ "threshold", "Threshold", -60, 0, -18, "dB", {}, false, "Level above which the compressor turns the sound down" });
    addParam ({ "ratio", "Ratio", 1, 20, 3, ":1", {}, true, "How strongly loud parts are turned down" });
    addParam ({ "attack", "Attack", 0.1f, 100, 10, "ms", {}, true });
    addParam ({ "release", "Release", 10, 1000, 120, "ms", {}, true });
    addParam ({ "knee", "Knee", 0, 18, 6, "dB" });
    addParam ({ "makeup", "Makeup", -12, 24, 0, "dB" });
    addParam ({ "mix", "Mix", 0, 1, 1, "%", {}, false, "Blend for parallel compression" });
    addParam ({ "sidechain", "Sidechain", 0, 1, 0, "", { "Off", "On" }, false, "Duck this track when the sidechain source plays (e.g. bass under kick)" });
    addParam ({ "scHighPass", "Sidechain HP", 20, 500, 20, "Hz", {}, true });
}

void CompressorEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    scFilter.setCoefficients (Biquad::Type::HighPass, sr, p (scHighPass), 0.7071);
    reset();
}

float CompressorEffect::curveDb (float inDb) const
{
    const float t = p (threshold), r = p (ratio), k = p (knee);
    float outDb;
    if (2 * (inDb - t) < -k)            outDb = inDb;
    else if (2 * std::abs (inDb - t) <= k && k > 0)
    {
        const float x = inDb - t + k / 2;
        outDb = inDb + (1 / r - 1) * x * x / (2 * k);
    }
    else                                outDb = t + (inDb - t) / r;
    return outDb;
}

void CompressorEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain)
{
    if (paramsChangedSince (seen))
        scFilter.setCoefficients (Biquad::Type::HighPass, sampleRate, p (scHighPass), 0.7071);

    const float att = timeCoeff (p (attack), sampleRate);
    const float rel = timeCoeff (p (release), sampleRate);
    const float makeupGain = dbToGain (p (makeup));
    const float wet = p (mix);
    const bool useSc = p (useSidechain) > 0.5f && sidechain != nullptr;
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (2, buffer.getNumChannels());

    float maxGr = 0;
    for (int i = 0; i < n; ++i)
    {
        float level = 0;
        if (useSc)
        {
            for (int ch = 0; ch < juce::jmin (2, sidechain->getNumChannels()); ++ch)
                level = std::max (level, std::abs (sidechain->getSample (ch, juce::jmin (i, sidechain->getNumSamples() - 1))));
            level = std::abs (scFilter.process (level));
        }
        else
            for (int ch = 0; ch < nch; ++ch)
                level = std::max (level, std::abs (buffer.getSample (ch, i)));

        const float inDb = gainToDb (level);
        const float targetGr = inDb - curveDb (inDb); // positive dB of reduction
        env = targetGr > env ? att * env + (1 - att) * targetGr : rel * env + (1 - rel) * targetGr;
        maxGr = std::max (maxGr, env);

        const float g = dbToGain (-env) * makeupGain;
        for (int ch = 0; ch < nch; ++ch)
        {
            auto x = buffer.getSample (ch, i);
            buffer.setSample (ch, i, x * (1 - wet) + x * g * wet);
        }
    }
    grDb = -maxGr;
}

//==============================================================================
// Gate
//==============================================================================
GateEffect::GateEffect()
{
    addParam ({ "threshold", "Threshold", -80, 0, -45, "dB", {}, false, "Sounds quieter than this are turned down" });
    addParam ({ "range", "Range", -80, 0, -40, "dB" });
    addParam ({ "attack", "Attack", 0.1f, 50, 1, "ms", {}, true });
    addParam ({ "hold", "Hold", 0, 500, 40, "ms" });
    addParam ({ "release", "Release", 5, 1000, 150, "ms", {}, true });
}

void GateEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    reset();
}

void GateEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    const float thr = dbToGain (p (threshold));
    const float floorGain = dbToGain (p (range));
    const float att = timeCoeff (p (attack), sampleRate);
    const float rel = timeCoeff (p (release), sampleRate);
    const float envRel = timeCoeff (20.0f, sampleRate);
    const int holdSamples = (int) (p (hold) * 0.001 * sampleRate);
    const int nch = juce::jmin (2, buffer.getNumChannels());

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float level = 0;
        for (int ch = 0; ch < nch; ++ch)
            level = std::max (level, std::abs (buffer.getSample (ch, i)));
        env = level > env ? level : envRel * env + (1 - envRel) * level;

        float target;
        if (env >= thr) { holdCounter = holdSamples; target = 1.0f; }
        else if (holdCounter > 0) { --holdCounter; target = 1.0f; }
        else target = floorGain;

        gain = target > gain ? att * gain + (1 - att) * target : rel * gain + (1 - rel) * target;
        for (int ch = 0; ch < nch; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * gain);
    }
}

//==============================================================================
// Limiter
//==============================================================================
LimiterEffect::LimiterEffect()
{
    addParam ({ "ceiling", "Ceiling", -6, 0, -1, "dBTP", {}, false, "Peaks never go above this level" });
    addParam ({ "release", "Release", 10, 1000, 100, "ms", {}, true });
    addParam ({ "inputGain", "Input gain", 0, 24, 0, "dB", {}, false, "Pushes the level into the limiter to make it louder" });
    addParam ({ "truePeak", "True peak", 0, 1, 1, "", { "Off", "On" }, false, "Also catches peaks between samples" });
}

void LimiterEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    lookahead = juce::jmax (8, (int) (0.0015 * sr));
    for (auto& d : delays) d.prepare (lookahead + TruePeakDetector::delay + 8);
    holdRing.assign ((size_t) lookahead, 1.0f);
    boxRing.assign ((size_t) lookahead, 1.0f);
    reset();
}

void LimiterEffect::reset()
{
    for (auto& d : delays) d.reset();
    for (auto& t : truePeakDetectors) t.reset();
    std::fill (holdRing.begin(), holdRing.end(), 1.0f);
    std::fill (boxRing.begin(), boxRing.end(), 1.0f);
    boxSum = (double) boxRing.size();
    windowPos = 0;
    gain = 1;
    grDb = 0;
}

void LimiterEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    // Stage 1: required gain per sample (true-peak aware), stage 2: hold the
    // minimum over the look-ahead window, stage 3: smooth with a box filter of
    // the same length so the gain is fully down before the peak leaves the delay.
    const float ceil = dbToGain (p (ceiling));
    const float in = dbToGain (p (inputGain));
    const float rel = timeCoeff (p (release), sampleRate);
    const bool usetp = p (truePeak) > 0.5f;
    const int nch = juce::jmin (2, buffer.getNumChannels());
    const int L = lookahead;
    const int D = TruePeakDetector::delay;

    float minGain = 1.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float peak = 0;
        for (int ch = 0; ch < nch; ++ch)
        {
            const float x = buffer.getSample (ch, i) * in;
            delays[(size_t) ch].push (x);
            const float tpv = truePeakDetectors[(size_t) ch].process (x);
            peak = std::max (peak, usetp ? tpv : std::abs (delays[(size_t) ch].readInt (D + 1)));
        }

        const float required = peak > ceil ? ceil / peak : 1.0f;
        holdRing[(size_t) windowPos] = required;
        float held = 1.0f;
        for (int k = 0; k < L; ++k) held = std::min (held, holdRing[(size_t) k]);

        // Instant attack, smooth release.
        gain = held < gain ? held : rel * gain + (1 - rel) * held;

        boxSum += gain - boxRing[(size_t) windowPos];
        boxRing[(size_t) windowPos] = gain;
        windowPos = (windowPos + 1) % L;
        const float g = (float) juce::jmin (1.0, boxSum / L);
        minGain = std::min (minGain, g);

        for (int ch = 0; ch < nch; ++ch)
        {
            float y = delays[(size_t) ch].readInt (L - 1 + D + 1) * g;
            buffer.setSample (ch, i, juce::jlimit (-ceil, ceil, y)); // final safety clip at the ceiling
        }
    }
    grDb = gainToDb (minGain);
}

//==============================================================================
// Reverb
//==============================================================================
ReverbEffect::ReverbEffect()
{
    addParam ({ "type", "Space", 0, 3, 1, "", { "Room", "Hall", "Church", "Plate" } });
    addParam ({ "decay", "Decay", 0.2f, 12, 2.2f, "s", {}, true, "How long the reverb tail rings" });
    addParam ({ "preDelay", "Pre-delay", 0, 200, 20, "ms", {}, false, "Gap before the reverb starts; keeps vocals clear" });
    addParam ({ "damping", "Damping", 0, 1, 0.4f, "%", {}, false, "Darkens the tail" });
    addParam ({ "size", "Size", 0.2f, 1.5f, 1, "" });
    addParam ({ "mix", "Mix", 0, 1, 0.25f, "%" });
    addParam ({ "width", "Width", 0, 1, 1, "%" });
    addParam ({ "lowCut", "Low cut", 20, 1000, 200, "Hz", {}, true, "Keeps the reverb from muddying the low end" });
}

void ReverbEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    const double scale = sr / 48000.0;
    for (auto& l : lines) l.prepare ((int) (6000 * 1.5 * scale * 1.6));
    for (auto& d : preDelays) d.prepare ((int) (0.25 * sr));
    for (auto& d : diffusers) d.prepare ((int) (1200 * scale * 1.6));
    seen = 0;
    updateParams();
    reset();
}

void ReverbEffect::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& d : preDelays) d.reset();
    for (auto& d : diffusers) d.reset();
    for (auto& f : inputHp) f.reset();
    lpState.fill (0);
}

void ReverbEffect::updateParams()
{
    // Mutually prime-ish base lengths (samples at 48 kHz).
    static const float base[N] { 1123, 1291, 1453, 1621, 1777, 1949, 2113, 2293 };
    static const float typeSize[4]    { 0.55f, 1.25f, 1.6f, 0.8f };
    static const float typeDamp[4]    { 0.15f, 0.0f, 0.15f, -0.25f };
    static const float diffBase[4]    { 142, 107, 379, 277 };

    const int t = juce::jlimit (0, 3, (int) std::round (p (type)));
    const double scale = sampleRate / 48000.0 * typeSize[t] * p (size);
    const float rt60 = p (decay) * (t == 0 ? 0.6f : 1.0f);

    for (int i = 0; i < N; ++i)
    {
        delaySamples[(size_t) i] = juce::jlimit (8.0f, (float) lines[(size_t) i].capacity() - 4.0f, (float) (base[i] * scale));
        feedbackGain[(size_t) i] = (float) std::pow (10.0, -3.0 * delaySamples[(size_t) i] / (rt60 * sampleRate));
    }
    for (int i = 0; i < 4; ++i)
        diffuserDelay[(size_t) i] = juce::jlimit (4.0f, (float) diffusers[(size_t) i].capacity() - 4.0f,
                                                  (float) (diffBase[i] * sampleRate / 48000.0 * (t == 3 ? 0.6 : 1.0)));

    dampCoeff = juce::jlimit (0.0f, 0.95f, p (damping) * 0.85f + typeDamp[t]);
    for (auto& f : inputHp)
        f.setCoefficients (Biquad::Type::HighPass, sampleRate, p (lowCut), 0.7071);
}

void ReverbEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    if (paramsChangedSince (seen))
        updateParams();

    const float wet = p (mix);
    const float w = p (width);
    const float pre = juce::jmax (1.0f, (float) (p (preDelay) * 0.001 * sampleRate));
    const int nch = juce::jmin (2, buffer.getNumChannels());
    const float diffG = 0.6f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float inL = buffer.getSample (0, i);
        const float inR = nch > 1 ? buffer.getSample (1, i) : inL;

        preDelays[0].push (inputHp[0].process (inL));
        preDelays[1].push (inputHp[1].process (inR));
        float x = 0.5f * (preDelays[0].read (pre) + preDelays[1].read (pre));

        // Series all-pass diffusion smears the input into a dense wash.
        for (int d = 0; d < 4; ++d)
        {
            const float delayed = diffusers[(size_t) d].read (diffuserDelay[(size_t) d]);
            const float v = x + diffG * delayed;
            diffusers[(size_t) d].push (v);
            x = delayed - diffG * v;
        }

        std::array<float, N> outs;
        float sum = 0;
        for (int k = 0; k < N; ++k)
        {
            float y = lines[(size_t) k].read (delaySamples[(size_t) k]);
            lpState[(size_t) k] = y + dampCoeff * (lpState[(size_t) k] - y);
            outs[(size_t) k] = lpState[(size_t) k] * feedbackGain[(size_t) k];
            sum += outs[(size_t) k];
        }

        // Householder feedback matrix: y_k - (2/N) * sum.
        const float h = sum * (2.0f / N);
        for (int k = 0; k < N; ++k)
            lines[(size_t) k].push (outs[(size_t) k] - h + x * ((k & 1) ? -0.35f : 0.35f));

        float wl = 0, wr = 0;
        for (int k = 0; k < N; k += 2) { wl += outs[(size_t) k]; wr += outs[(size_t) k + 1]; }
        wl *= 0.5f; wr *= 0.5f;

        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * w;
        wl = mid + side; wr = mid - side;

        buffer.setSample (0, i, inL * (1 - wet * 0.5f) + wl * wet);
        if (nch > 1) buffer.setSample (1, i, inR * (1 - wet * 0.5f) + wr * wet);
    }
}

//==============================================================================
// Delay
//==============================================================================
DelayEffect::DelayEffect()
{
    addParam ({ "time", "Time", 10, 2000, 375, "ms", {}, true });
    addParam ({ "sync", "Sync", 0, 6, 2, "", { "Off", "1/4", "1/8", "1/8 dotted", "1/8 triplet", "1/16", "1/2" }, false, "Locks the echo to the song tempo" });
    addParam ({ "feedback", "Feedback", 0, 0.95f, 0.3f, "%" });
    addParam ({ "mix", "Mix", 0, 1, 0.2f, "%" });
    addParam ({ "highCut", "High cut", 1000, 20000, 6000, "Hz", {}, true });
    addParam ({ "lowCut", "Low cut", 20, 1000, 250, "Hz", {}, true });
    addParam ({ "pingPong", "Ping-pong", 0, 1, 0, "", { "Off", "On" } });
}

void DelayEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    for (auto& l : lines) l.prepare ((int) (2.5 * sr));
    seen = 0;
    reset();
}

void DelayEffect::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& f : hc) f.reset();
    for (auto& f : lc) f.reset();
}

void DelayEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    if (paramsChangedSince (seen))
    {
        for (auto& f : hc) f.setCoefficients (Biquad::Type::LowPass, sampleRate, p (highCut), 0.7071);
        for (auto& f : lc) f.setCoefficients (Biquad::Type::HighPass, sampleRate, p (lowCut), 0.7071);
    }

    static const double beatsPerChoice[] { 0, 1.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 2.0 };
    const int s = juce::jlimit (0, 6, (int) std::round (p (sync)));
    double ms = p (timeMs);
    if (s > 0)
        ms = beatsPerChoice[s] * 60000.0 / juce::jmax (20.0f, tempo.load());
    const float d = (float) juce::jlimit (1.0, 2.4 * sampleRate, ms * 0.001 * sampleRate);
    const float fb = p (feedback), wet = p (mix);
    const bool pp = p (pingPong) > 0.5f;
    const int nch = juce::jmin (2, buffer.getNumChannels());

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float inL = buffer.getSample (0, i);
        const float inR = nch > 1 ? buffer.getSample (1, i) : inL;
        const float dl = lc[0].process (hc[0].process (lines[0].read (d)));
        const float dr = lc[1].process (hc[1].process (lines[1].read (d)));

        if (pp)
        {
            lines[0].push (0.5f * (inL + inR) + dr * fb);
            lines[1].push (dl * fb);
        }
        else
        {
            lines[0].push (inL + dl * fb);
            lines[1].push (inR + dr * fb);
        }

        buffer.setSample (0, i, inL + dl * wet);
        if (nch > 1) buffer.setSample (1, i, inR + dr * wet);
    }
}

//==============================================================================
// Chorus
//==============================================================================
ChorusEffect::ChorusEffect()
{
    addParam ({ "rate", "Rate", 0.05f, 5, 0.8f, "Hz", {}, true });
    addParam ({ "depth", "Depth", 0, 1, 0.4f, "%" });
    addParam ({ "mix", "Mix", 0, 1, 0.4f, "%" });
    addParam ({ "delay", "Delay", 5, 30, 12, "ms" });
}

void ChorusEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    for (auto& l : lines) l.prepare ((int) (0.06 * sr));
    reset();
}

void ChorusEffect::reset()
{
    for (auto& l : lines) l.reset();
    phase = 0;
}

void ChorusEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    const double inc = 2.0 * pi * p (rate) / sampleRate;
    const float base = (float) (p (delayMs) * 0.001 * sampleRate);
    const float depthS = (float) (p (depth) * 0.005 * sampleRate);
    const float wet = p (mix);
    const int nch = juce::jmin (2, buffer.getNumChannels());

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        for (int ch = 0; ch < nch; ++ch)
        {
            const float lfo = (float) std::sin (phase + (ch == 0 ? 0.0 : pi / 2));
            const float x = buffer.getSample (ch, i);
            lines[(size_t) ch].push (x);
            const float y = lines[(size_t) ch].read (base + depthS * (0.5f + 0.5f * lfo));
            buffer.setSample (ch, i, x * (1 - wet * 0.5f) + y * wet);
        }
        phase += inc;
        if (phase > 2 * pi) phase -= 2 * pi;
    }
}

//==============================================================================
// Saturation
//==============================================================================
SaturationEffect::SaturationEffect()
{
    addParam ({ "drive", "Drive", 0, 24, 6, "dB", {}, false, "More drive adds more warmth and grit" });
    addParam ({ "tone", "Tone", -1, 1, 0, "", {}, false, "Darker to brighter" });
    addParam ({ "mix", "Mix", 0, 1, 1, "%" });
    addParam ({ "character", "Character", 0, 2, 0, "", { "Tape", "Tube", "Clip" } });
    addParam ({ "output", "Output", -24, 12, 0, "dB" });
}

void SaturationEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    for (auto& f : upFilter)   f.setCoefficients (Biquad::Type::LowPass, sr * 2, sr * 0.45, 0.7071);
    for (auto& f : downFilter) f.setCoefficients (Biquad::Type::LowPass, sr * 2, sr * 0.45, 0.7071);
    for (auto& f : dcBlock)    f.setCoefficients (Biquad::Type::HighPass, sr, 10, 0.7071);
    seen = 0;
    reset();
}

void SaturationEffect::reset()
{
    for (auto* arr : { &toneFilter, &upFilter, &downFilter, &dcBlock })
        for (auto& f : *arr) f.reset();
    lastIn.fill (0);
}

void SaturationEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    if (paramsChangedSince (seen))
    {
        const float t = p (tone);
        for (auto& f : toneFilter)
            f.setCoefficients (t >= 0 ? Biquad::Type::HighShelf : Biquad::Type::LowShelf,
                               sampleRate, t >= 0 ? 3000.0 : 400.0, 0.7071, std::abs (t) * 6.0 * (t >= 0 ? 1 : 1));
    }

    const float driveGain = dbToGain (p (drive));
    const float comp = 1.0f / std::sqrt (driveGain); // rough loudness compensation
    const float outGain = dbToGain (p (output));
    const float wet = p (mix);
    const int mode = juce::jlimit (0, 2, (int) std::round (p (character)));
    const int nch = juce::jmin (2, buffer.getNumChannels());

    auto shape = [mode] (float x) noexcept
    {
        switch (mode)
        {
            case 1:  return std::tanh (x + 0.15f * x * x) - std::tanh (0.0f); // tube: even harmonics
            case 2:  return juce::jlimit (-1.0f, 1.0f, x - (x * x * x) / 3.0f * (std::abs (x) < 1.5f ? 1.0f : 0.0f));
            default: return std::tanh (x);
        }
    };

    for (int ch = 0; ch < nch; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        auto& up = upFilter[(size_t) ch];
        auto& down = downFilter[(size_t) ch];
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float dry = d[i];
            const float x = dry * driveGain;
            // 2x oversample: interpolate a midpoint, shape both, filter, keep one.
            const float mid = 0.5f * (lastIn[(size_t) ch] + x);
            lastIn[(size_t) ch] = x;
            down.process (shape (up.process (mid)));
            const float y = down.process (shape (up.process (x)));
            float s = dcBlock[(size_t) ch].process (y) * comp;
            s = toneFilter[(size_t) ch].process (s);
            d[i] = (dry * (1 - wet) + s * wet) * outGain;
        }
    }
}

//==============================================================================
// Widener
//==============================================================================
WidenerEffect::WidenerEffect()
{
    addParam ({ "width", "Width", 0, 2, 1.2f, "", {}, false, "0 = mono, 1 = unchanged, 2 = extra wide" });
    addParam ({ "monoBelow", "Mono below", 20, 300, 120, "Hz", {}, true, "Keeps bass and kick centred" });
}

void WidenerEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    seen = 0;
    reset();
}

void WidenerEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    if (buffer.getNumChannels() < 2)
        return;
    if (paramsChangedSince (seen))
        sideHp.setCoefficients (Biquad::Type::HighPass, sampleRate, p (monoBelow), 0.7071);

    const float w = p (width);
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float m = 0.5f * (l[i] + r[i]);
        const float s = sideHp.process (0.5f * (l[i] - r[i])) * w;
        l[i] = m + s;
        r[i] = m - s;
    }
}

//==============================================================================
// De-esser
//==============================================================================
DeEsserEffect::DeEsserEffect()
{
    addParam ({ "frequency", "Frequency", 3000, 12000, 6500, "Hz", {}, true, "Where the harsh 's' sounds sit" });
    addParam ({ "threshold", "Threshold", -60, 0, -28, "dB" });
    addParam ({ "range", "Max reduction", 0, 24, 10, "dB" });
}

void DeEsserEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    env.setTimes (0.5f, 60.0f, sr);
    fullEnv.setTimes (0.5f, 60.0f, sr);
    seen = 0;
    reset();
}

void DeEsserEffect::reset()
{
    for (auto& s : split) s.reset();
    detectSplit.reset();
    env.reset();
    fullEnv.reset();
    grDb = 0;
}

void DeEsserEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    if (paramsChangedSince (seen))
    {
        for (auto& s : split) s.setup (sampleRate, p (frequency));
        detectSplit.setup (sampleRate, p (frequency));
    }

    const float thrDb = p (threshold), maxRed = p (range);
    const int nch = juce::jmin (2, buffer.getNumChannels());
    float maxGr = 0;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float mono = 0;
        for (int ch = 0; ch < nch; ++ch) mono += buffer.getSample (ch, i);
        mono /= (float) nch;

        float lo, hi;
        detectSplit.process (mono, lo, hi);
        const float hiDb = gainToDb (env.process (hi));
        const float fullDb = gainToDb (fullEnv.process (mono));

        // Reduce when the sibilant band is both loud and dominant.
        float red = 0;
        if (hiDb > thrDb && hiDb > fullDb - 6.0f)
            red = juce::jmin (maxRed, (hiDb - thrDb) * 0.75f);
        maxGr = std::max (maxGr, red);
        const float g = dbToGain (-red);

        for (int ch = 0; ch < nch; ++ch)
        {
            float l, h;
            split[(size_t) ch].process (buffer.getSample (ch, i), l, h);
            buffer.setSample (ch, i, l + h * g);
        }
    }
    grDb = -maxGr;
}

//==============================================================================
// Gain
//==============================================================================
GainEffect::GainEffect()
{
    addParam ({ "gain", "Gain", -48, 24, 0, "dB" });
    addParam ({ "invert", "Phase", 0, 1, 0, "", { "Normal", "Inverted" } });
}

void GainEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    const float g = dbToGain (p (gainDb)) * (p (invertPhase) > 0.5f ? -1.0f : 1.0f);
    buffer.applyGain (g);
}

} // namespace studio::dsp
