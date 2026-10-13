#include "Drums.h"
#include <cmath>
#include "../dsp/DspCommon.h"

namespace studio
{

namespace
{
    constexpr double twoPi = 2.0 * dsp::pi;
    inline float svfG (double fc, double sr) { return (float) std::tan (dsp::pi * juce::jlimit (20.0, sr * 0.45, fc) / sr); }
}

DrumKit::DrumKit (juce::String id, std::map<int, DrumPiece> p)
    : kitId (std::move (id)), pieces (std::move (p))
{
}

std::unique_ptr<DrumKit> DrumKit::pitched (juce::String id, DrumPiece piece)
{
    piece.pitched = true;
    auto kit = std::make_unique<DrumKit> (std::move (id), std::map<int, DrumPiece> {});
    kit->allPitched = true;
    kit->pitchedPiece = piece;
    return kit;
}

void DrumKit::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& v : voices) v = Voice {};
}

int DrumKit::getActiveVoices() const
{
    int n = 0;
    for (auto& v : voices) if (v.active) ++n;
    return n;
}

void DrumKit::allNotesOff (bool immediately)
{
    if (immediately)
        for (auto& v : voices) v.active = false;
}

void DrumKit::noteOn (int note, float velocity)
{
    const DrumPiece* piece = nullptr;
    if (allPitched)
        piece = &pitchedPiece;
    else if (auto it = pieces.find (note); it != pieces.end())
        piece = &it->second;
    if (piece == nullptr)
        return;

    // Choke: closed hat cuts the open hat, etc.
    if (piece->chokeGroup != 0)
        for (auto& v : voices)
            if (v.active && v.piece != nullptr && v.piece->chokeGroup == piece->chokeGroup && v.piece != piece)
                v.active = false;

    Voice* target = nullptr;
    for (auto& v : voices) if (! v.active) { target = &v; break; }
    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices) if (v.age < target->age) target = &v;
    }

    *target = Voice {};
    target->piece = piece;
    target->velocity = juce::jlimit (0.0f, 1.0f, velocity);
    target->active = true;
    target->age = ++ageCounter;
    target->freq = piece->pitched ? 440.0 * std::pow (2.0, (note - 69) / 12.0) : piece->freq;
    // Slight random variation per hit so repeats never sound machine-identical (round-robin feel).
    target->freq *= 1.0 + (noise() * 0.004);
}

float DrumKit::renderVoice (Voice& v, double dt) noexcept
{
    const auto& p = *v.piece;
    const double t = v.t;
    v.t += dt;
    const float vel = 0.25f + 0.75f * v.velocity;
    const float bright = juce::jlimit (0.05f, 1.0f, p.brightness * (0.6f + 0.6f * v.velocity));
    float s = 0;

    auto env = [] (double time, double decay) { return (float) std::exp (-time / juce::jmax (0.001, decay)); };
    auto osc = [&] (int i, double f) { v.phases[(size_t) i] += f * dt; if (v.phases[(size_t) i] > 1) v.phases[(size_t) i] -= 1; return (float) std::sin (twoPi * v.phases[(size_t) i]); };
    float bp = 0, hp = 0;
    // Filter cutoffs are fixed for a hit: compute each coefficient once.
    auto cachedG = [dt] (float& slot, double fc) { if (slot < 0) slot = svfG (fc, 1.0 / dt); return slot; };

    switch (p.type)
    {
        case DrumPiece::Type::Kick:
        case DrumPiece::Type::Tom:
        case DrumPiece::Type::LogDrum:
        {
            const double f = v.freq * std::pow (2.0, p.pitchDrop * std::exp (-t / p.pitchDecay) / 12.0);
            float body = osc (0, f);
            if (p.type == DrumPiece::Type::LogDrum)
            {
                // Hollow, woody: add a soft 2nd and a filtered triangle-ish 3rd.
                body = 0.75f * body + 0.25f * osc (1, f * 2.0) + 0.12f * osc (2, f * 3.0);
                body = v.f1.process (body, cachedG (v.g1, 800.0 + 2500.0 * bright), 1.2f, bp, hp);
            }
            s = std::tanh (body * p.drive) * env (t, p.decay);
            if (p.type == DrumPiece::Type::Tom)
                s += 0.15f * p.noise * noise() * env (t, 0.03);
            // Beater click.
            const float click = v.f2.process (noise(), cachedG (v.g2, 3000.0 + 5000.0 * bright), 1.0f, bp, hp);
            s += hp * p.tone * 0.6f * env (t, 0.004);
            juce::ignoreUnused (click);
            break;
        }
        case DrumPiece::Type::Snare:
        {
            const double f = v.freq * std::pow (2.0, p.pitchDrop * std::exp (-t / p.pitchDecay) / 12.0);
            const float body = (osc (0, f) + 0.5f * osc (1, f * 1.59)) * env (t, 0.06 + 0.1 * p.tone);
            v.f1.process (noise(), cachedG (v.g1, 1800.0 + 5000.0 * bright), 0.8f, bp, hp);
            const float wires = (bp * 0.6f + hp * 0.6f) * env (t, p.decay);
            s = std::tanh ((body * p.tone + wires * p.noise * 2.0f) * p.drive);
            break;
        }
        case DrumPiece::Type::HiHat:
        case DrumPiece::Type::Cymbal:
        {
            // Six detuned square oscillators at inharmonic ratios, high-passed, plus noise.
            static const double ratios[6] { 2.0, 3.0, 4.16, 5.43, 6.79, 8.21 };
            float metal = 0;
            for (int i = 0; i < 6; ++i)
            {
                v.phases[(size_t) i] += v.freq * ratios[i] * dt;
                if (v.phases[(size_t) i] > 1) v.phases[(size_t) i] -= 1;
                metal += v.phases[(size_t) i] < 0.5 ? 1.0f : -1.0f;
            }
            const float mix = metal * 0.12f * (1.0f - p.noise) + noise() * p.noise;
            v.f1.process (mix, cachedG (v.g1, 6000.0 + 5000.0 * bright), 0.7f, bp, hp);
            s = hp * env (t, p.decay) * (p.type == DrumPiece::Type::Cymbal ? 0.8f : 1.0f);
            break;
        }
        case DrumPiece::Type::Clap:
        {
            // Three quick bursts, then a diffuse tail.
            float e = 0;
            for (int k = 0; k < 3; ++k)
            {
                const double tk = t - k * 0.011;
                if (tk >= 0) e = std::max (e, (float) std::exp (-tk / 0.006));
            }
            e = std::max (e, 0.5f * env (std::max (0.0, t - 0.03), p.decay));
            v.f1.process (noise(), cachedG (v.g1, 1100.0 + 1200.0 * bright), 0.6f, bp, hp);
            s = bp * e * 2.2f;
            break;
        }
        case DrumPiece::Type::Rim:
        {
            s = (osc (0, v.freq) * 0.8f + osc (1, v.freq * 2.3)) * env (t, 0.02);
            v.f1.process (noise(), cachedG (v.g1, 3000.0), 0.5f, bp, hp);
            s += bp * env (t, 0.006) * 1.5f;
            break;
        }
        case DrumPiece::Type::Shaker:
        case DrumPiece::Type::Shekere:
        {
            // Soft attack, bandpassed noise. Shekere adds a granular bead rattle.
            float e = (float) (juce::jmin (1.0, t / 0.008) * std::exp (-std::max (0.0, t - 0.008) / p.decay));
            if (p.type == DrumPiece::Type::Shekere)
            {
                const double grainRate = 180.0;
                const float g = (float) std::pow (std::abs (std::sin (dsp::pi * t * grainRate + v.phases[3])), 8.0);
                e *= 0.4f + 0.9f * g;
            }
            v.f1.process (noise(), cachedG (v.g1, p.freq), 0.9f, bp, hp);
            s = (bp * 0.8f + hp * 0.5f * bright) * e * 1.5f;
            break;
        }
        case DrumPiece::Type::Membrane:
        {
            // Hand drum: two membrane modes with a small pitch glide (negative drop
            // gives the talking drum's rising squeeze), plus a slap of noise.
            const double f = v.freq * std::pow (2.0, p.pitchDrop * std::exp (-t / p.pitchDecay) / 12.0);
            const float body = osc (0, f) * env (t, p.decay) + 0.35f * osc (1, f * 1.59) * env (t, p.decay * 0.5);
            v.f1.process (noise(), cachedG (v.g1, 1500.0 + 4000.0 * bright), 0.7f, bp, hp);
            s = std::tanh (body * p.drive) * p.tone + bp * p.noise * env (t, 0.015) * 2.0f;
            break;
        }
        case DrumPiece::Type::Bell:
        {
            // Agogo: inharmonic partials.
            s = (osc (0, v.freq) + 0.5f * osc (1, v.freq * 2.76) * env (t, p.decay * 0.5) + 0.25f * osc (2, v.freq * 5.4) * env (t, p.decay * 0.25))
              * env (t, p.decay);
            s += noise() * 0.2f * env (t, 0.003);
            break;
        }
        case DrumPiece::Type::Udu:
        {
            // Clay pot: a low "bloop" whose pitch rises, through a resonant band-pass.
            const double f = v.freq * std::pow (2.0, -p.pitchDrop * (1.0 - std::exp (-t / p.pitchDecay)) / 12.0);
            const float src = osc (0, f) * env (t, p.decay);
            v.f1.process (src, svfG (f * 2.0, 1.0 / dt), 0.25f, bp, hp);
            s = 0.7f * src + 0.5f * bp;
            break;
        }
    }

    if (t > 6.0 * p.decay + 0.25)
        v.active = false;

    return s * vel * p.level;
}

void DrumKit::render (juce::AudioBuffer<float>& out, int start, int num)
{
    const double dt = 1.0 / sr;
    auto* L = out.getWritePointer (0);
    auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

    for (auto& v : voices)
    {
        if (! v.active) continue;
        const float pan = v.piece->pan;
        const float gl = std::sqrt (0.5f * (1.0f - pan)), gr = std::sqrt (0.5f * (1.0f + pan));
        for (int i = start; i < start + num && v.active; ++i)
        {
            const float s = renderVoice (v, dt);
            L[i] += s * gl;
            if (R != nullptr) R[i] += s * gr;
        }
    }
}

} // namespace studio
