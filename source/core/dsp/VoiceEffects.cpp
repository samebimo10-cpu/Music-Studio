#include "VoiceEffects.h"

namespace studio::dsp
{

//==============================================================================
// PitchShifter
//==============================================================================
void PitchShifter::prepare (double sampleRate, float grainMs)
{
    sr = sampleRate;
    grain = targetGrain = (float) (grainMs * 0.001 * sr);
    minGrain = (float) (0.008 * sr);
    maxGrain = (float) (0.08 * sr);
    line.prepare ((int) (maxGrain * 2.5f) + 16);
    reset();
}

void PitchShifter::reset()
{
    line.reset();
    phase = 0;
}

void PitchShifter::setGrainSamples (float samples) noexcept
{
    targetGrain = juce::jlimit (minGrain, maxGrain, samples);
}

float PitchShifter::process (float x) noexcept
{
    line.push (x);
    if (std::abs (ratio - 1.0f) < 1.0e-4f)
    {
        // Bypass path keeps the same latency so switching does not jump in time.
        return line.read (grain * 0.5f + 1.0f);
    }

    // Each tap's delay ramps from grain to 0 (pitch up) or 0 to grain (pitch down);
    // the two taps are half a grain apart and crossfaded with a sine window.
    phase += (1.0f - ratio) / grain;
    if (phase >= 1.0f) { phase -= 1.0f; grain = targetGrain; }
    if (phase < 0.0f)  { phase += 1.0f; grain = targetGrain; }

    float p2 = phase + 0.5f;
    if (p2 >= 1.0f) p2 -= 1.0f;

    const float d1 = phase * grain + 1.0f;
    const float d2 = p2 * grain + 1.0f;
    const float w1 = std::sin ((float) pi * phase);
    const float w2 = std::sin ((float) pi * p2);
    // sin^2 windows offset by half a period sum to 1; use them as amplitude weights.
    return line.read (d1) * w1 * w1 + line.read (d2) * w2 * w2;
}

//==============================================================================
// PitchCorrectEffect
//==============================================================================
PitchCorrectEffect::PitchCorrectEffect()
{
    addParam ({ "tonic", "Key", 0, 11, 0, "", { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" } });
    addParam ({ "scale", "Scale", 0, 7, 0, "", { "Major", "Minor", "Harmonic minor", "Dorian", "Mixolydian", "Major pentatonic", "Minor pentatonic", "Blues" } });
    addParam ({ "strength", "Strength", 0, 1, 0.8f, "%", {}, false, "How far notes are pulled to the key" });
    addParam ({ "speed", "Speed", 0, 400, 80, "ms", {}, false, "0 = hard robotic snap, higher = natural" });
    addParam ({ "humanize", "Humanize", 0, 1, 0.3f, "%", {}, false, "Leaves long notes a little freedom" });
    addParam ({ "mix", "Mix", 0, 1, 1, "%" });
}

void PitchCorrectEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    detector.prepare (sr, sr > 60000 ? 4096 : 2048, 70.0f, 1000.0f);
    analysis.assign ((size_t) detector.getWindowSize(), 0.0f);
    for (auto& s : shifters) s.prepare (sr, 30.0f);
    reset();
}

void PitchCorrectEffect::reset()
{
    std::fill (analysis.begin(), analysis.end(), 0.0f);
    analysisPos = hopCounter = 0;
    correction = targetCorrection = 0;
    heldNote = -1;
    for (auto& s : shifters) s.reset();
}

void PitchCorrectEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    Key key;
    key.tonic = juce::jlimit (0, 11, (int) std::round (p (tonic)));
    key.scale = (ScaleType) juce::jlimit (0, 7, (int) std::round (p (scale)));
    const float str = p (strength);
    const float wet = p (mix);
    const float smooth = p (speed) <= 1.0f ? 0.0f : timeCoeff (p (speed), sampleRate);
    const int hop = 256;
    const int w = (int) analysis.size();
    const int nch = juce::jmin (2, buffer.getNumChannels());

    // Detection works on an unwrapped copy of the analysis ring; it lives on the stack.
    float unwrapped[4096];

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float mono = 0;
        for (int ch = 0; ch < nch; ++ch) mono += buffer.getSample (ch, i);
        mono /= (float) nch;

        analysis[(size_t) analysisPos] = mono;
        analysisPos = (analysisPos + 1) % w;

        if (++hopCounter >= hop)
        {
            hopCounter = 0;
            for (int k = 0; k < w; ++k)
                unwrapped[k] = analysis[(size_t) ((analysisPos + k) % w)];

            auto r = detector.detect (unwrapped);
            if (r.voiced() && r.confidence > 0.75f)
            {
                const float midi = (float) theory::hzToMidi (r.frequency);
                // Hysteresis: stay on the held note until clearly closer to another.
                int nearest = key.snapToScale ((int) std::round (midi));
                if (heldNote >= 0 && std::abs (midi - heldNote) < 0.65f && key.containsPitchClass ((int) heldNote))
                    nearest = (int) heldNote;
                heldNote = (float) nearest;

                float corr = ((float) nearest - midi) * str;
                // Humanize: leave small deviations alone.
                const float dead = p (humanize) * 0.25f;
                if (std::abs (corr) < dead) corr = 0;
                targetCorrection = corr;
                detectedMidi = midi;
                targetMidi = (float) nearest;

                for (auto& s : shifters)
                    s.setGrainSamples ((float) (sampleRate / r.frequency) * 3.0f);
            }
            else
            {
                targetCorrection = 0;
                heldNote = -1;
                detectedMidi = 0;
            }
        }

        correction = smooth * correction + (1 - smooth) * targetCorrection;
        const float ratio = std::pow (2.0f, correction / 12.0f);

        for (int ch = 0; ch < nch; ++ch)
        {
            auto& sh = shifters[(size_t) ch];
            sh.setRatio (ratio);
            const float dry = buffer.getSample (ch, i);
            const float y = sh.process (dry);
            buffer.setSample (ch, i, y * wet + dry * (1 - wet));
        }
    }
}

//==============================================================================
// DoublerEffect
//==============================================================================
DoublerEffect::DoublerEffect()
{
    addParam ({ "mix", "Amount", 0, 1, 0.5f, "%" });
    addParam ({ "detune", "Detune", 0, 30, 9, "cents" });
    addParam ({ "delay", "Delay", 5, 50, 18, "ms" });
    addParam ({ "width", "Width", 0, 1, 0.8f, "%" });
}

void DoublerEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    for (auto& l : lines) l.prepare ((int) (0.1 * sr));
    for (auto& s : shifters) s.prepare (sr, 35.0f);
    reset();
}

void DoublerEffect::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& s : shifters) s.reset();
    drift.fill (0);
    driftTarget.fill (0);
    driftCounter = 0;
}

void DoublerEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    const float cents = p (detune);
    shifters[0].setRatio (std::pow (2.0f, cents / 1200.0f));
    shifters[1].setRatio (std::pow (2.0f, -cents / 1200.0f));
    const float baseDelay = (float) (p (delayMs) * 0.001 * sampleRate);
    const float wet = p (mix), w = p (width);
    const int nch = buffer.getNumChannels();
    const float driftCoeff = timeCoeff (150.0f, sampleRate);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        // Slow random timing drift, like a singer who is never exactly on top of themselves.
        if (--driftCounter <= 0)
        {
            driftCounter = (int) (0.12 * sampleRate);
            for (auto& t : driftTarget) t = (rng.nextFloat() - 0.5f) * 0.35f * baseDelay;
        }

        const float inL = buffer.getSample (0, i);
        const float inR = nch > 1 ? buffer.getSample (1, i) : inL;
        const float mono = 0.5f * (inL + inR);

        float v[2];
        for (int k = 0; k < 2; ++k)
        {
            drift[(size_t) k] = driftCoeff * drift[(size_t) k] + (1 - driftCoeff) * driftTarget[(size_t) k];
            lines[(size_t) k].push (shifters[(size_t) k].process (mono));
            v[k] = lines[(size_t) k].read (baseDelay * (k == 0 ? 1.0f : 1.37f) + drift[(size_t) k]);
        }

        const float l = v[0] * (0.5f + 0.5f * w) + v[1] * (0.5f - 0.5f * w);
        const float r = v[1] * (0.5f + 0.5f * w) + v[0] * (0.5f - 0.5f * w);
        buffer.setSample (0, i, inL + l * wet * 0.7f);
        if (nch > 1) buffer.setSample (1, i, inR + r * wet * 0.7f);
    }
}

//==============================================================================
// BreathControlEffect
//==============================================================================
BreathControlEffect::BreathControlEffect()
{
    addParam ({ "breath", "Breath reduction", 0, 30, 10, "dB", {}, false, "Turns breaths down, keeps them natural" });
    addParam ({ "click", "Click reduction", 0, 1, 0.5f, "%", {}, false, "Softens mouth clicks and lip smacks" });
    addParam ({ "sensitivity", "Sensitivity", 0, 1, 0.5f, "%" });
}

void BreathControlEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    lookahead = (int) (0.01 * sr);
    for (auto& d : delays) d.prepare (lookahead + 8);
    hpDetect.setCoefficients (Biquad::Type::HighPass, sr, 3000, 0.7071);
    lpDetect.setCoefficients (Biquad::Type::LowPass, sr, 1000, 0.7071);
    clickHp.setCoefficients (Biquad::Type::HighPass, sr, 2000, 0.7071);
    reset();
}

void BreathControlEffect::reset()
{
    for (auto& d : delays) d.reset();
    hpDetect.reset(); lpDetect.reset(); clickHp.reset();
    voiceLevel = 0.01f; hiEnv = loEnv = 0; gain = clickGain = 1; clickEnv = 0;
    frameCounter = zeroCrossings = 0; lastSample = 0; frameEnergy = 0;
    breathState = false;
}

void BreathControlEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    const int nch = juce::jmin (2, buffer.getNumChannels());
    const float breathGain = dbToGain (-p (breathReduction));
    const float clickAmt = p (clickReduction);
    const float sens = p (sensitivity);
    const int frameLen = (int) (0.005 * sampleRate);
    const float envC = timeCoeff (8.0f, sampleRate);
    const float voiceRel = timeCoeff (1500.0f, sampleRate);
    const float gainC = timeCoeff (6.0f, sampleRate);
    const float clickC = timeCoeff (0.3f, sampleRate);
    const float clickRel = timeCoeff (3.0f, sampleRate);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float mono = 0;
        for (int ch = 0; ch < nch; ++ch) mono += buffer.getSample (ch, i);
        mono /= (float) nch;

        const float hi = hpDetect.process (mono);
        const float lo = lpDetect.process (mono);
        hiEnv = envC * hiEnv + (1 - envC) * std::abs (hi);
        loEnv = envC * loEnv + (1 - envC) * std::abs (lo);

        frameEnergy += mono * mono;
        if ((mono >= 0) != (lastSample >= 0)) ++zeroCrossings;
        lastSample = mono;

        // Breath detection once per 5 ms frame: noisy (high ZCR, little low end)
        // and well below the singer's recent voiced level, but not silence.
        if (++frameCounter >= frameLen)
        {
            const float rms = std::sqrt (frameEnergy / (float) frameLen);
            const float zcr = (float) zeroCrossings / (float) frameLen;
            const bool voiced = loEnv > hiEnv * 1.2f && zcr < 0.1f;
            if (voiced && rms > voiceLevel) voiceLevel = rms;
            else voiceLevel = voiceRel * voiceLevel + (1 - voiceRel) * rms;

            const float rel = rms / juce::jmax (1.0e-5f, voiceLevel);
            const float upper = 0.12f + 0.25f * sens;   // breaths sit roughly 12-30 dB under the voice
            breathState = ! voiced && zcr > 0.08f && rel < upper && rel > 0.003f;

            frameCounter = 0;
            zeroCrossings = 0;
            frameEnergy = 0;
        }

        const float targetGain = breathState ? breathGain : 1.0f;
        gain = gainC * gain + (1 - gainC) * targetGain;

        // Clicks: very short high-frequency spikes far above the local HF level.
        const float c = std::abs (clickHp.process (mono));
        clickEnv = c > clickEnv ? clickC * clickEnv + (1 - clickC) * c : 0.9995f * clickEnv + 0.0005f * c;
        float clickTarget = 1.0f;
        if (clickAmt > 0 && c > clickEnv * (6.0f - 3.0f * sens) && c > 0.01f)
            clickTarget = 1.0f - 0.85f * clickAmt;
        clickGain = clickTarget < clickGain ? clickTarget : clickRel * clickGain + (1 - clickRel) * clickTarget;

        const float g = gain * clickGain;
        for (int ch = 0; ch < nch; ++ch)
        {
            delays[(size_t) ch].push (buffer.getSample (ch, i));
            buffer.setSample (ch, i, delays[(size_t) ch].readInt (lookahead) * g);
        }
    }
}

} // namespace studio::dsp
