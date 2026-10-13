#include "LoudnessMeter.h"
#include <algorithm>
#include <cmath>

namespace studio
{

namespace
{
    // BS.1770 K-weighting coefficients derived for any sample rate (the published
    // 48 kHz values come from these analogue prototypes).
    void designKWeighting (double fs, dsp::Biquad& pre, dsp::Biquad& rlb)
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (dsp::pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            pre.setRaw ((Vh + Vb * K / Q + K * K) / a0,
                        2.0 * (K * K - Vh) / a0,
                        (Vh - Vb * K / Q + K * K) / a0,
                        2.0 * (K * K - 1.0) / a0,
                        (1.0 - K / Q + K * K) / a0);
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (dsp::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            rlb.setRaw (1.0, -2.0, 1.0,
                        2.0 * (K * K - 1.0) / a0,
                        (1.0 - K / Q + K * K) / a0);
        }
    }

    float energyToLufs (double e) noexcept
    {
        return e <= 1.0e-12 ? -120.0f : (float) (-0.691 + 10.0 * std::log10 (e));
    }

    double lufsToEnergy (float l) noexcept { return std::pow (10.0, (l + 0.691) / 10.0); }

    constexpr size_t maxBlocks = 10 * 60 * 60 * 6; // six hours of 100 ms blocks
}

void LoudnessMeter::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    nch = juce::jlimit (1, 8, numChannels);
    for (auto& f : kFilters)
        designKWeighting (sr, f[0], f[1]);
    blockSize = (int) std::round (sr * 0.1);
    momentaryBlocks.assign (maxBlocks, -120.0f);
    shortTermBlocks.assign (maxBlocks, -120.0f);
    reset();
}

void LoudnessMeter::reset()
{
    for (auto& f : kFilters) { f[0].reset(); f[1].reset(); }
    for (auto& t : tp) t.reset();
    blockCounter = 0;
    blockEnergy = 0;
    recentBlocks.fill (0);
    recentPos = recentCount = 0;
    momentaryCount = shortTermCount = 0;
    momentary = -120.0f;
    shortTerm = -120.0f;
    truePeak = 0.0f;
    samplePeak = 0.0f;
}

void LoudnessMeter::process (const juce::AudioBuffer<float>& buffer)
{
    process (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void LoudnessMeter::process (const float* const* channels, int numChannels, int numSamples)
{
    const int chans = juce::jmin (numChannels, nch);
    float tpMax = truePeak.load(), spMax = samplePeak.load();

    for (int i = 0; i < numSamples; ++i)
    {
        double sum = 0;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float x = channels[ch][i];
            spMax = std::max (spMax, std::abs (x));
            tpMax = std::max (tpMax, tp[(size_t) ch].process (x));
            const float y = kFilters[(size_t) ch][1].process (kFilters[(size_t) ch][0].process (x));
            sum += (double) y * y; // channel weights are 1.0 for L, R, C
        }
        blockEnergy += sum;

        if (++blockCounter >= blockSize)
            finishBlock();
    }

    truePeak = tpMax;
    samplePeak = spMax;
}

void LoudnessMeter::finishBlock() noexcept
{
    recentBlocks[(size_t) recentPos] = blockEnergy / blockCounter;
    recentPos = (recentPos + 1) % (int) recentBlocks.size();
    recentCount = std::min (recentCount + 1, (int) recentBlocks.size());
    blockEnergy = 0;
    blockCounter = 0;

    auto meanOfLast = [this] (int n)
    {
        double s = 0;
        const int count = std::min (n, recentCount);
        for (int k = 1; k <= count; ++k)
            s += recentBlocks[(size_t) ((recentPos - k + (int) recentBlocks.size()) % (int) recentBlocks.size())];
        return count > 0 ? s / n : 0.0;
    };

    if (recentCount >= 4)
    {
        const float m = energyToLufs (meanOfLast (4));
        momentary = m;
        if (momentaryCount < maxBlocks) momentaryBlocks[momentaryCount++] = m;
    }
    if (recentCount >= 30)
    {
        const float s = energyToLufs (meanOfLast (30));
        shortTerm = s;
        if (shortTermCount < maxBlocks) shortTermBlocks[shortTermCount++] = s;
    }
    else if (recentCount >= 4)
        shortTerm = energyToLufs (meanOfLast (recentCount)); // provisional until 3 s are in
}

float LoudnessMeter::getIntegratedLufs() const
{
    // Absolute gate at -70 LUFS, then a relative gate 10 LU under the mean of what passed.
    double sum = 0;
    size_t n = 0;
    for (size_t i = 0; i < momentaryCount; ++i)
        if (momentaryBlocks[i] > -70.0f) { sum += lufsToEnergy (momentaryBlocks[i]); ++n; }
    if (n == 0)
        return -120.0f;

    const float relGate = energyToLufs (sum / (double) n) - 10.0f;
    sum = 0; n = 0;
    for (size_t i = 0; i < momentaryCount; ++i)
        if (momentaryBlocks[i] > -70.0f && momentaryBlocks[i] > relGate) { sum += lufsToEnergy (momentaryBlocks[i]); ++n; }
    return n == 0 ? -120.0f : energyToLufs (sum / (double) n);
}

float LoudnessMeter::getLoudnessRange() const
{
    std::vector<float> st;
    double sum = 0;
    for (size_t i = 0; i < shortTermCount; ++i)
        if (shortTermBlocks[i] > -70.0f) { st.push_back (shortTermBlocks[i]); sum += lufsToEnergy (shortTermBlocks[i]); }
    if (st.size() < 2)
        return 0.0f;

    const float relGate = energyToLufs (sum / (double) st.size()) - 20.0f;
    st.erase (std::remove_if (st.begin(), st.end(), [relGate] (float v) { return v < relGate; }), st.end());
    if (st.size() < 2)
        return 0.0f;

    std::sort (st.begin(), st.end());
    auto pct = [&] (double q) { return st[(size_t) std::clamp ((int) std::round (q * (double) (st.size() - 1)), 0, (int) st.size() - 1)]; };
    return pct (0.95) - pct (0.10);
}

LoudnessMeter::Summary LoudnessMeter::measure (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    LoudnessMeter m;
    m.prepare (sampleRate, juce::jmax (1, buffer.getNumChannels()));
    Summary s;

    const int chunk = 4096;
    for (int pos = 0; pos < buffer.getNumSamples(); pos += chunk)
    {
        const int n = juce::jmin (chunk, buffer.getNumSamples() - pos);
        const float* ptrs[8];
        for (int ch = 0; ch < juce::jmin (8, buffer.getNumChannels()); ++ch)
            ptrs[ch] = buffer.getReadPointer (ch, pos);
        m.process (ptrs, buffer.getNumChannels(), n);
        s.maxMomentaryLufs = std::max (s.maxMomentaryLufs, m.getMomentaryLufs());
        s.maxShortTermLufs = std::max (s.maxShortTermLufs, m.getShortTermLufs());
    }

    // Flush the true-peak interpolators with silence so the last samples count.
    for (int ch = 0; ch < juce::jmin (8, buffer.getNumChannels()); ++ch)
        for (int i = 0; i < dsp::TruePeakDetector::tapsPerPhase; ++i)
            m.truePeak = std::max (m.truePeak.load(), m.tp[(size_t) ch].process (0.0f));

    s.integratedLufs = m.getIntegratedLufs();
    s.loudnessRange = m.getLoudnessRange();
    s.truePeakDb = m.getTruePeakDb();
    s.samplePeakDb = m.getSamplePeakDb();
    return s;
}

//==============================================================================
const std::vector<PlatformTarget>& platformTargets()
{
    static const std::vector<PlatformTarget> targets {
        { "Spotify",        -14.0f, true  },
        { "Apple Music",    -16.0f, true  },
        { "YouTube Music",  -14.0f, false },
        { "Amazon Music",   -14.0f, true  },
        { "Tidal",          -14.0f, false },
        { "Deezer",         -15.0f, false },
        { "Audiomack",      -14.0f, false },
        { "Boomplay",       -14.0f, false },
    };
    return targets;
}

float platformAdjustmentDb (const PlatformTarget& t, float lufs, float truePeakDb)
{
    float gain = t.targetLufs - lufs;
    if (gain > 0)
    {
        if (! t.turnsUpQuietSongs)
            return 0.0f;
        // Platforms that turn up stop before the peaks would clip (-1 dBTP headroom).
        gain = std::min (gain, -1.0f - truePeakDb);
        gain = std::max (gain, 0.0f);
    }
    return gain;
}

} // namespace studio
