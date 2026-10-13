#pragma once

#include <array>
#include <cmath>
#include "DspCommon.h"

namespace studio::dsp
{

/** 4x oversampled true-peak estimator (ITU-R BS.1770 style, 48-tap windowed-sinc
    polyphase interpolator). Feed one channel per instance. Output lags the input
    by `delay` samples. */
class TruePeakDetector
{
public:
    static constexpr int phases = 4;
    static constexpr int tapsPerPhase = 12;
    static constexpr int delay = tapsPerPhase / 2;

    TruePeakDetector()
    {
        const int total = phases * tapsPerPhase;
        const double centre = (total - 1) / 2.0;
        for (int k = 0; k < total; ++k)
        {
            const double x = (k - centre) / (double) phases;
            const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double w = 0.5 * (1.0 - std::cos (2.0 * pi * (k + 0.5) / total)); // Hann
            coeffs[(size_t) (k % phases)][(size_t) (k / phases)] = (float) (sinc * w);
        }
        // Normalise each phase to unity DC gain.
        for (auto& ph : coeffs)
        {
            float sum = 0;
            for (auto c : ph) sum += c;
            for (auto& c : ph) c /= sum;
        }
    }

    void reset() noexcept { history.fill (0.0f); pos = 0; }

    /** Pushes a sample, returns the max absolute interpolated value around the delayed sample. */
    inline float process (float x) noexcept
    {
        history[(size_t) pos] = x;
        pos = (pos + 1) % tapsPerPhase;

        float peak = 0;
        for (int ph = 0; ph < phases; ++ph)
        {
            float acc = 0;
            for (int j = 0; j < tapsPerPhase; ++j)
                acc += coeffs[(size_t) ph][(size_t) j] * history[(size_t) ((pos + tapsPerPhase - 1 - j) % tapsPerPhase)];
            peak = std::max (peak, std::abs (acc));
        }
        // Include the original (delayed) sample itself.
        peak = std::max (peak, std::abs (history[(size_t) ((pos + tapsPerPhase - 1 - delay) % tapsPerPhase)]));
        return peak;
    }

private:
    std::array<std::array<float, tapsPerPhase>, phases> coeffs {};
    std::array<float, tapsPerPhase> history {};
    int pos = 0;
};

} // namespace studio::dsp
