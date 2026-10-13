#include "Synth.h"
#include <cmath>

namespace studio
{

namespace
{
    constexpr double twoPi = 2.0 * dsp::pi;

    inline float polyBlep (double t, double dt) noexcept
    {
        if (t < dt)       { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    inline double noteHz (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

    // Organ drawbar footages as ratios of the 8' fundamental.
    constexpr double drawbarRatios[9] { 0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0 };
}

//==============================================================================
void SynthInstrument::Env::set (const SynthPatch::Adsr& a, double sr)
{
    attackRate = a.a <= 0.0005f ? 1.0f : (float) (1.0 / (a.a * sr));
    decayCoeff = (float) std::exp (-1.0 / (juce::jmax (0.001f, a.d) * 0.33 * sr));
    releaseCoeff = (float) std::exp (-1.0 / (juce::jmax (0.001f, a.r) * 0.33 * sr));
    sustain = a.s;
}

float SynthInstrument::Env::next()
{
    switch (stage)
    {
        case Stage::Idle: return 0.0f;
        case Stage::Attack:
            value += attackRate;
            if (value >= 1.0f) { value = 1.0f; stage = Stage::Decay; }
            break;
        case Stage::Decay:
            value = sustain + (value - sustain) * decayCoeff;
            if (std::abs (value - sustain) < 1.0e-4f) { value = sustain; stage = Stage::Sustain; }
            if (sustain <= 0.0f && value < 1.0e-4f) { value = 0; stage = Stage::Idle; }
            break;
        case Stage::Sustain: break;
        case Stage::Release:
            value *= releaseCoeff;
            if (value < 1.0e-4f) { value = 0; stage = Stage::Idle; }
            break;
    }
    return value;
}

//==============================================================================
SynthInstrument::SynthInstrument (juce::String id, SynthPatch p)
    : instrumentId (std::move (id)), patch (std::move (p))
{
}

void SynthInstrument::prepare (double sampleRate, int)
{
    sr = sampleRate;
    voices.assign ((size_t) (patch.mono ? 1 : patch.maxVoices), Voice {});
    for (auto& v : voices)
    {
        v.ks.assign ((size_t) (sr / 20.0) + 4, 0.0f);
        v.amp.set (patch.ampEnv, sr);
        v.filt.set (patch.filterEnv, sr);
    }
}

void SynthInstrument::setParameter (const juce::String& name, float value)
{
    if (name == "cutoff")          patch.cutoff = value;
    else if (name == "resonance")  patch.resonance = value;
    else if (name == "attack")     patch.ampEnv.a = value;
    else if (name == "release")    patch.ampEnv.r = value;
    else if (name == "gain")       patch.gain = value;
    else if (name == "vowel")      patch.vowel = value;
    else if (name == "leslie")     patch.leslieRate = value;
    else if (name == "brightness") patch.brightness = value;
    for (auto& v : voices) { v.amp.set (patch.ampEnv, sr); v.filt.set (patch.filterEnv, sr); }
}

int SynthInstrument::getActiveVoices() const
{
    int n = 0;
    for (auto& v : voices) if (v.amp.stage != Env::Stage::Idle) ++n;
    return n;
}

void SynthInstrument::startVoice (Voice& v, int note, float velocity)
{
    const bool legato = patch.mono && v.amp.stage != Env::Stage::Idle && v.amp.stage != Env::Stage::Release;
    v.note = note;
    v.velocity = velocity;
    v.held = true;
    v.sustained = false;
    v.age = ++ageCounter;
    v.targetFreq = noteHz (note);
    if (! legato || patch.glide <= 0) v.freq = v.targetFreq;
    v.pan = juce::jlimit (-1.0f, 1.0f, ((note - 60) / 30.0f) * patch.stereoSpread);

    if (legato)
        return; // keep envelopes running, just glide

    v.time = 0;
    v.amp.set (patch.ampEnv, sr);
    v.filt.set (patch.filterEnv, sr);
    v.amp.value = 0;
    v.amp.trigger();
    v.filt.value = 0;
    v.filt.trigger();
    v.svfLow = v.svfBand = v.svfLow2 = v.svfBand2 = 0;
    v.formantState.fill (0);
    for (auto& ph : v.phases) ph = rng.nextDouble();
    v.fmPhase = 0;
    v.vibPhase = 0;

    if (patch.mode == SynthPatch::Mode::Modal)
    {
        const double f = v.targetFreq;
        v.numPartials = 0;
        const float bright = juce::jlimit (0.05f, 1.0f, patch.brightness * (0.5f + 0.7f * velocity));
        for (int n = 1; n <= juce::jmin (patch.partials, 24); ++n)
        {
            const double fn = f * n * std::sqrt (1.0 + patch.inharmonicity * n * n);
            if (fn > sr * 0.45) break;
            const int i = v.numPartials++;
            const double w = twoPi * fn / sr;
            v.pCos[(size_t) i] = (float) std::cos (w);
            v.pSin[(size_t) i] = (float) std::sin (w);
            v.pRe[(size_t) i] = 0;
            v.pIm[(size_t) i] = 1;
            // Higher partials are quieter (brightness sets the roll-off) and die faster.
            v.pAmp[(size_t) i] = (float) (std::pow ((double) n, -1.0 - 2.0 * (1.0 - bright)) * (n == 1 ? 1.0 : 0.8));
            const double t60 = patch.decaySeconds * std::pow (261.6 / f, 0.35) / (1.0 + 0.35 * (n - 1));
            v.pDecay[(size_t) i] = (float) std::pow (10.0, -3.0 / (juce::jmax (0.05, t60) * sr));
        }
        v.noiseState = patch.hammer * velocity;
    }
    else if (patch.mode == SynthPatch::Mode::Pluck)
    {
        v.ksLen = (float) juce::jlimit (2.0, (double) v.ks.size() - 2, sr / v.targetFreq);
        std::fill (v.ks.begin(), v.ks.end(), 0.0f);
        // Excite with filtered noise; pick position cuts some harmonics (comb).
        float lp = 0;
        const float bright = juce::jlimit (0.05f, 1.0f, patch.pluckBrightness * (0.4f + 0.8f * velocity));
        const int len = (int) v.ksLen;
        for (int i = 0; i < len; ++i)
        {
            const float nse = rng.nextFloat() * 2.0f - 1.0f;
            lp += bright * (nse - lp);
            v.ks[(size_t) i] = lp;
        }
        const int pick = juce::jmax (1, (int) (len * patch.pickPosition));
        for (int i = len - 1; i >= pick; --i)
            v.ks[(size_t) i] -= v.ks[(size_t) (i - pick)] * 0.8f;
        v.ksPos = len; // the next write goes after the excitation
        v.ksLast = 0;
    }
    else if (patch.mode == SynthPatch::Mode::Organ)
    {
        v.noiseState = patch.percussion; // percussion click level, decays quickly
    }
}

void SynthInstrument::noteOn (int note, float velocity)
{
    if (voices.empty())
        return;

    velocity = juce::jlimit (0.0f, 1.0f, velocity);
    const float vel = 1.0f - patch.velocitySensitivity + patch.velocitySensitivity * velocity;

    if (patch.mono)
    {
        lastMonoNote = note;
        startVoice (voices[0], note, vel);
        return;
    }

    // Retrigger the same note on its own voice, else pick a free or the oldest voice.
    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.note == note && v.amp.stage != Env::Stage::Idle) { target = &v; break; }
    if (target == nullptr)
        for (auto& v : voices)
            if (v.amp.stage == Env::Stage::Idle) { target = &v; break; }
    if (target == nullptr)
    {
        juce::uint32 oldest = std::numeric_limits<juce::uint32>::max();
        for (auto& v : voices)
        {
            const bool released = ! v.held;
            const juce::uint32 score = v.age - (released ? 1000000u : 0u);
            if (score < oldest) { oldest = score; target = &v; }
        }
    }
    startVoice (*target, note, vel);
}

void SynthInstrument::noteOff (int note)
{
    for (auto& v : voices)
    {
        if (v.note != note || ! v.held) continue;
        v.held = false;
        if (sustainDown) { v.sustained = true; continue; }
        v.amp.release();
        v.filt.release();
    }
}

void SynthInstrument::setSustain (bool down)
{
    sustainDown = down;
    if (! down)
        for (auto& v : voices)
            if (v.sustained) { v.sustained = false; v.amp.release(); v.filt.release(); }
}

void SynthInstrument::allNotesOff (bool immediately)
{
    for (auto& v : voices)
    {
        v.held = false;
        v.sustained = false;
        if (immediately) { v.amp.stage = Env::Stage::Idle; v.amp.value = 0; v.note = -1; }
        else { v.amp.release(); v.filt.release(); }
    }
}

float SynthInstrument::oscillator (SynthPatch::Wave w, double& phase, double inc) noexcept
{
    float y = 0;
    switch (w)
    {
        case SynthPatch::Wave::Sine:     y = (float) std::sin (twoPi * phase); break;
        case SynthPatch::Wave::Saw:      y = (float) (2.0 * phase - 1.0) - polyBlep (phase, inc); break;
        case SynthPatch::Wave::Square:
        case SynthPatch::Wave::Pulse:
        {
            const double pw = w == SynthPatch::Wave::Pulse ? 0.25 : 0.5;
            y = phase < pw ? 1.0f : -1.0f;
            y += polyBlep (phase, inc);
            double t2 = phase - pw; if (t2 < 0) t2 += 1.0;
            y -= polyBlep (t2, inc);
            break;
        }
        case SynthPatch::Wave::Triangle: y = (float) (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase); break;
        case SynthPatch::Wave::Noise:
            noiseSeed = noiseSeed * 1664525u + 1013904223u;
            y = (float) ((noiseSeed >> 9) * (1.0 / 4194304.0)) - 1.0f;
            break;
    }
    phase += inc;
    if (phase >= 1.0) phase -= 1.0;
    return y;
}

float SynthInstrument::renderVoice (Voice& v, float& outR) noexcept
{
    const float ampEnv = v.amp.next();
    const float filtEnv = v.filt.next();

    // Pitch: glide, bend, pitch envelope, delayed vibrato.
    if (patch.glide > 0)
        v.freq += (v.targetFreq - v.freq) * (1.0 - std::exp (-1.0 / (patch.glide * sr)));
    double semis = bend + patch.pitchEnvSemis * std::exp (-v.time / juce::jmax (0.001f, patch.pitchEnvDecay));
    if (patch.vibratoDepth > 0)
    {
        const double fade = juce::jlimit (0.0, 1.0, (v.time - patch.vibratoDelay) / 0.4);
        semis += patch.vibratoDepth * fade * std::sin (twoPi * v.vibPhase);
        v.vibPhase += patch.vibratoRate / sr;
        if (v.vibPhase >= 1) v.vibPhase -= 1;
    }
    const double f = v.freq * std::pow (2.0, semis / 12.0);
    v.time += 1.0 / sr;

    float s = 0;
    switch (patch.mode)
    {
        case SynthPatch::Mode::Subtractive:
        case SynthPatch::Mode::Formant:
        {
            const int uni = juce::jlimit (1, 5, patch.unison);
            for (int o = 0; o < 3; ++o)
            {
                auto& osc = patch.osc[(size_t) o];
                if (osc.level <= 0) continue;
                const double base = f * std::pow (2.0, osc.octave + osc.detuneCents / 1200.0);
                const int nUni = o == 0 ? uni : 1;
                float sum = 0;
                for (int u = 0; u < nUni; ++u)
                {
                    const double spread = nUni > 1 ? (u / (double) (nUni - 1) - 0.5) * 2.0 * patch.unisonDetune : 0.0;
                    const double inc = base * std::pow (2.0, spread / 1200.0) / sr;
                    sum += oscillator (osc.wave, v.phases[(size_t) (o == 0 ? u : 4 + o)], inc);
                }
                s += sum * osc.level / std::sqrt ((float) nUni);
            }
            if (patch.noiseLevel > 0)
            {
                double dummy = 0;
                s += oscillator (SynthPatch::Wave::Noise, dummy, 0) * patch.noiseLevel;
            }
            break;
        }
        case SynthPatch::Mode::FM:
        {
            const double env = std::exp (-v.time * patch.fmIndexDecay);
            const double index = patch.fmIndex * (1.0 - patch.fmVelToIndex + patch.fmVelToIndex * v.velocity) * env;
            const double mod = std::sin (twoPi * v.fmPhase) * index;
            v.fmPhase += f * patch.fmRatio / sr;
            if (v.fmPhase >= 1) v.fmPhase -= 1;
            s = (float) std::sin (twoPi * v.phases[0] + mod);
            v.phases[0] += f / sr;
            if (v.phases[0] >= 1) v.phases[0] -= 1;
            // A quiet second operator at the octave gives the bell/tine body.
            if (patch.osc[1].level > 0)
            {
                s += patch.osc[1].level * (float) std::sin (twoPi * v.phases[1]);
                v.phases[1] += f * std::pow (2.0, patch.osc[1].octave) / sr;
                if (v.phases[1] >= 1) v.phases[1] -= 1;
            }
            break;
        }
        case SynthPatch::Mode::Modal:
        {
            for (int i = 0; i < v.numPartials; ++i)
            {
                const float re = v.pRe[(size_t) i], im = v.pIm[(size_t) i];
                const float d = v.pDecay[(size_t) i];
                v.pRe[(size_t) i] = (re * v.pCos[(size_t) i] - im * v.pSin[(size_t) i]) * d;
                v.pIm[(size_t) i] = (re * v.pSin[(size_t) i] + im * v.pCos[(size_t) i]) * d;
                s += v.pRe[(size_t) i] * v.pAmp[(size_t) i];
            }
            if (v.noiseState > 1.0e-4f)
            {
                double dummy = 0;
                s += oscillator (SynthPatch::Wave::Noise, dummy, 0) * v.noiseState * 0.3f;
                v.noiseState *= 0.995f;
            }
            break;
        }
        case SynthPatch::Mode::Organ:
        {
            for (int d = 0; d < 9; ++d)
            {
                const float lvl = patch.drawbars[(size_t) d];
                auto& ph = v.phases[(size_t) (d % 8)];
                if (lvl > 0)
                    s += lvl * (float) std::sin (twoPi * (ph * drawbarRatios[d] + d * 0.11));
            }
            v.phases[0] += f / sr; // all drawbars derive from phase 0 (tonewheel lock)
            for (int k = 1; k < 8; ++k) v.phases[(size_t) k] = v.phases[0];
            if (v.phases[0] >= 1.0) v.phases[0] -= 1.0;
            if (v.noiseState > 1.0e-4f)
            {
                s += v.noiseState * (float) std::sin (twoPi * v.phases[0] * 3.0);
                v.noiseState *= 0.9995f;
            }
            s *= 0.25f;
            break;
        }
        case SynthPatch::Mode::Pluck:
        {
            const float len = (float) juce::jlimit (2.0, (double) v.ks.size() - 2, sr / f);
            const int size = (int) v.ks.size();
            float readPos = (float) v.ksPos - len;
            while (readPos < 0) readPos += (float) size;
            const int i0 = (int) readPos;
            const float fr = readPos - (float) i0;
            const float a = v.ks[(size_t) (i0 % size)], b = v.ks[(size_t) ((i0 + 1) % size)];
            const float y = a + (b - a) * fr;
            const float damp = 0.996f + 0.0039f * (1.0f - patch.pluckDamping) - patch.mute * 0.02f;
            const float filtered = (y + v.ksLast) * 0.5f * damp;
            v.ksLast = y;
            v.ks[(size_t) v.ksPos] = filtered;
            v.ksPos = (v.ksPos + 1) % size;
            s = y;
            break;
        }
    }

    // Filter: TPT state-variable low-pass (two cascaded for 24 dB on subtractive).
    if (patch.mode == SynthPatch::Mode::Subtractive || patch.mode == SynthPatch::Mode::Pluck || patch.mode == SynthPatch::Mode::FM)
    {
        double fc = patch.cutoff * std::pow (2.0, patch.filterEnvAmount * filtEnv * 5.0)
                  * std::pow (f / 261.6, patch.keyTrack) * (1.0 - patch.velToCutoff + patch.velToCutoff * v.velocity * 1.5);
        fc = juce::jlimit (30.0, sr * 0.45, fc);
        if (fc < sr * 0.44)
        {
            const float g = (float) std::tan (dsp::pi * fc / sr);
            const float k = 2.0f - 1.9f * patch.resonance;
            const float a1 = 1.0f / (1.0f + g * (g + k));
            auto stage = [&] (float x, float& low, float& band)
            {
                const float hp = (x - (k + g) * band - low) * a1;
                const float bp = g * hp + band;
                const float lp = g * bp + low;
                band = g * hp + bp;
                low = g * bp + lp;
                return lp;
            };
            s = stage (s, v.svfLow, v.svfBand);
            if (patch.mode == SynthPatch::Mode::Subtractive)
                s = stage (s, v.svfLow2, v.svfBand2);
        }
    }
    else if (patch.mode == SynthPatch::Mode::Formant)
    {
        // Three formant band-passes, morphing from "ah" to "oo".
        const float vw = juce::jlimit (0.0f, 1.0f, patch.vowel);
        const float formants[3] { 800 + (350 - 800) * vw, 1150 + (600 - 1150) * vw, 2900 + (2700 - 2900) * vw };
        const float gains[3] { 1.0f, 0.5f + (0.25f - 0.5f) * vw, 0.25f };
        float out = 0;
        for (int i = 0; i < 3; ++i)
        {
            const float g = (float) std::tan (dsp::pi * formants[i] / sr);
            const float k = 1.0f / 6.0f;
            const float a1 = 1.0f / (1.0f + g * (g + k));
            float& low = v.formantState[(size_t) (i * 2)];
            float& band = v.formantState[(size_t) (i * 2 + 1)];
            const float hp = (s - (k + g) * band - low) * a1;
            const float bp = g * hp + band;
            band = g * hp + bp;
            low = g * bp + (g * bp + low);
            out += bp * gains[i];
        }
        s = out * 0.6f;
    }

    s *= ampEnv * v.velocity * patch.gain;

    // Rotary speaker: amplitude and pan modulation.
    float pan = v.pan;
    if (patch.leslieRate > 0)
    {
        const float rot = (float) std::sin (lesliePhase + v.note * 0.05);
        s *= 0.85f + 0.15f * rot;
        pan = juce::jlimit (-1.0f, 1.0f, pan + 0.5f * rot);
    }

    const float l = s * std::sqrt (0.5f * (1.0f - pan));
    outR = s * std::sqrt (0.5f * (1.0f + pan));
    return l;
}

void SynthInstrument::render (juce::AudioBuffer<float>& out, int start, int num)
{
    auto* L = out.getWritePointer (0);
    auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

    for (auto& v : voices)
    {
        if (v.amp.stage == Env::Stage::Idle)
            continue;
        for (int i = start; i < start + num; ++i)
        {
            float r = 0;
            const float l = renderVoice (v, r);
            L[i] += l;
            if (R != nullptr) R[i] += r;
            if (v.amp.stage == Env::Stage::Idle) { v.note = -1; break; }
        }
    }

    if (patch.leslieRate > 0)
        lesliePhase = std::fmod (lesliePhase + twoPi * patch.leslieRate * num / sr, twoPi);
}

} // namespace studio
