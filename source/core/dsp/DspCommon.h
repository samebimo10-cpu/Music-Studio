#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

namespace studio::dsp
{

constexpr double pi = 3.14159265358979323846;

inline float dbToGain (float db) noexcept     { return db <= -120.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept      { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }

/** One-pole smoothing coefficient for a time constant in ms. */
inline float timeCoeff (float ms, double sampleRate) noexcept
{
    return ms <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (0.001 * ms * sampleRate));
}

//==============================================================================
/** RBJ cookbook biquad in transposed direct form II. Coefficient updates never allocate. */
class Biquad
{
public:
    enum class Type { LowPass, HighPass, BandPass, Peak, LowShelf, HighShelf, Notch, AllPass };

    void setCoefficients (Type type, double sampleRate, double freq, double q, double gainDb = 0) noexcept
    {
        freq = juce::jlimit (10.0, sampleRate * 0.49, freq);
        q = juce::jmax (0.05, q);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * freq / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

        switch (type)
        {
            case Type::LowPass:   b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::HighPass:  b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::BandPass:  b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::Notch:     b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::AllPass:   b0 = 1 - alpha; b1 = -2 * cw; b2 = 1 + alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::Peak:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
                break;
            case Type::LowShelf:
            {
                const double sq = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + sq);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - sq);
                a0 = (A + 1) + (A - 1) * cw + sq;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - sq;
                break;
            }
            case Type::HighShelf:
            {
                const double sq = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + sq);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - sq);
                a0 = (A + 1) - (A - 1) * cw + sq;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - sq;
                break;
            }
        }

        c[0] = (float) (b0 / a0); c[1] = (float) (b1 / a0); c[2] = (float) (b2 / a0);
        c[3] = (float) (a1 / a0); c[4] = (float) (a2 / a0);
    }

    /** Sets raw normalised coefficients (b0, b1, b2, a1, a2). */
    void setRaw (double b0, double b1, double b2, double a1, double a2) noexcept
    {
        c[0] = (float) b0; c[1] = (float) b1; c[2] = (float) b2; c[3] = (float) a1; c[4] = (float) a2;
    }

    /** Takes another filter's coefficients but keeps this filter's state (no clicks). */
    void copyCoefficientsFrom (const Biquad& other) noexcept { c = other.c; }

    inline float process (float x) noexcept
    {
        const float y = c[0] * x + z1;
        z1 = c[1] * x - c[3] * y + z2;
        z2 = c[2] * x - c[4] * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0; }

    /** Magnitude response in dB at a frequency (for EQ curve drawing). */
    double magnitudeDb (double freq, double sampleRate) const noexcept
    {
        const double w = 2.0 * pi * freq / sampleRate;
        const std::complex<double> z1c = std::polar (1.0, -w), z2c = std::polar (1.0, -2.0 * w);
        auto num = (double) c[0] + (double) c[1] * z1c + (double) c[2] * z2c;
        auto den = 1.0 + (double) c[3] * z1c + (double) c[4] * z2c;
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (num / den)));
    }

private:
    std::array<float, 5> c { 1, 0, 0, 0, 0 };
    float z1 = 0, z2 = 0;
};

//==============================================================================
/** Fixed-capacity fractional delay line. */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        buffer.assign ((size_t) juce::nextPowerOfTwo (maxDelaySamples + 4), 0.0f);
        mask = (int) buffer.size() - 1;
        writePos = 0;
    }

    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    inline void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Reads `delay` samples behind the most recent write (1 = last sample written). */
    inline float read (float delay) const noexcept
    {
        delay = juce::jlimit (1.0f, (float) mask - 2.0f, delay);
        const float pos = (float) writePos - delay;
        const int i0 = (int) std::floor (pos);
        const float frac = pos - (float) i0;
        const float a = buffer[(size_t) (i0 & mask)];
        const float b = buffer[(size_t) ((i0 + 1) & mask)];
        return a + (b - a) * frac;
    }

    inline float readInt (int delay) const noexcept { return buffer[(size_t) ((writePos - delay) & mask)]; }

    int capacity() const noexcept { return mask + 1; }

private:
    std::vector<float> buffer { 0.0f, 0.0f };
    int mask = 1;
    int writePos = 0;
};

//==============================================================================
/** Peak/RMS envelope follower with separate attack and release. */
class EnvelopeFollower
{
public:
    void setTimes (float attackMs, float releaseMs, double sampleRate) noexcept
    {
        att = timeCoeff (attackMs, sampleRate);
        rel = timeCoeff (releaseMs, sampleRate);
    }

    inline float process (float x) noexcept
    {
        x = std::abs (x);
        env = x > env ? att * env + (1 - att) * x : rel * env + (1 - rel) * x;
        return env;
    }

    float value() const noexcept { return env; }
    void reset() noexcept { env = 0; }

private:
    float att = 0.9f, rel = 0.999f, env = 0;
};

/** Linkwitz-Riley 4th-order crossover (two cascaded Butterworth biquads per side). */
class LinkwitzRiley
{
public:
    void setup (double sampleRate, double freq) noexcept
    {
        for (auto& b : lp) b.setCoefficients (Biquad::Type::LowPass, sampleRate, freq, 0.7071);
        for (auto& b : hp) b.setCoefficients (Biquad::Type::HighPass, sampleRate, freq, 0.7071);
    }

    inline void process (float x, float& low, float& high) noexcept
    {
        low = lp[1].process (lp[0].process (x));
        high = hp[1].process (hp[0].process (x));
    }

    void reset() noexcept { for (auto& b : lp) b.reset(); for (auto& b : hp) b.reset(); }

private:
    std::array<Biquad, 2> lp, hp;
};

} // namespace studio::dsp
