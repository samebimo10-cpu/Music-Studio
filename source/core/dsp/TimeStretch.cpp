#include "TimeStretch.h"
#include "DspCommon.h"
#include <cmath>

namespace studio::dsp::timestretch
{

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& input, double sourceRate, double targetRate)
{
    if (std::abs (sourceRate - targetRate) < 1.0e-6 || input.getNumSamples() == 0)
        return input;

    const double speed = sourceRate / targetRate; // input samples consumed per output sample
    const int outLen = (int) std::ceil (input.getNumSamples() / speed);
    juce::AudioBuffer<float> out (input.getNumChannels(), outLen);
    out.clear();

    for (int ch = 0; ch < input.getNumChannels(); ++ch)
    {
        juce::WindowedSincInterpolator interp;
        // Pad the input so the interpolator can run to the end.
        std::vector<float> padded ((size_t) input.getNumSamples() + 256, 0.0f);
        std::copy (input.getReadPointer (ch), input.getReadPointer (ch) + input.getNumSamples(), padded.begin());
        interp.process (speed, padded.data(), out.getWritePointer (ch), outLen);
    }
    return out;
}

juce::AudioBuffer<float> stretch (const juce::AudioBuffer<float>& input, double ratio, double sampleRate)
{
    const int inLen = input.getNumSamples();
    const int nch = input.getNumChannels();
    if (inLen == 0 || std::abs (ratio - 1.0) < 1.0e-4)
        return input;

    ratio = juce::jlimit (0.25, 4.0, ratio);

    const int frame = juce::nextPowerOfTwo ((int) (0.04 * sampleRate)); // ~40 ms grains
    const int hopOut = frame / 2;
    const double hopIn = hopOut / ratio;
    const int tolerance = frame / 4;
    const int outLen = (int) std::ceil (inLen * ratio);

    juce::AudioBuffer<float> out (nch, outLen + frame);
    out.clear();
    std::vector<float> window ((size_t) frame), norm ((size_t) outLen + (size_t) frame, 0.0f);
    for (int i = 0; i < frame; ++i)
        window[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * pi * i / frame);

    // Mono guide for the similarity search.
    std::vector<float> mono ((size_t) inLen, 0.0f);
    for (int ch = 0; ch < nch; ++ch)
        for (int i = 0; i < inLen; ++i)
            mono[(size_t) i] += input.getSample (ch, i) / (float) nch;

    auto monoAt = [&] (int i) { return (i >= 0 && i < inLen) ? mono[(size_t) i] : 0.0f; };

    int prevInStart = 0;
    for (int outPos = 0, k = 0; outPos < outLen; outPos += hopOut, ++k)
    {
        const int nominal = (int) std::round (k * hopIn);
        int best = nominal;

        if (k > 0)
        {
            // Natural continuation of the previous grain, which we try to match.
            const int target = prevInStart + hopOut;
            double bestScore = -1.0e30;
            const int step = 2;
            for (int off = -tolerance; off <= tolerance; off += step)
            {
                const int cand = nominal + off;
                double score = 0;
                for (int i = 0; i < frame / 2; i += 4)
                    score += (double) monoAt (cand + i) * monoAt (target + i);
                if (score > bestScore) { bestScore = score; best = cand; }
            }
        }
        prevInStart = best;

        for (int ch = 0; ch < nch; ++ch)
        {
            auto* src = input.getReadPointer (ch);
            auto* dst = out.getWritePointer (ch);
            for (int i = 0; i < frame; ++i)
            {
                const int si = best + i;
                if (si >= 0 && si < inLen)
                    dst[outPos + i] += src[si] * window[(size_t) i];
            }
        }
        for (int i = 0; i < frame; ++i)
            norm[(size_t) (outPos + i)] += window[(size_t) i];
    }

    juce::AudioBuffer<float> result (nch, outLen);
    for (int ch = 0; ch < nch; ++ch)
        for (int i = 0; i < outLen; ++i)
            result.setSample (ch, i, out.getSample (ch, i) / juce::jmax (0.1f, norm[(size_t) i]));
    return result;
}

juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input, double ratio, double semitones, double sampleRate)
{
    if (std::abs (semitones) < 1.0e-4)
        return stretch (input, ratio, sampleRate);

    // Stretch by ratio * pitchRatio, then resample by pitchRatio: length = ratio, pitch moved.
    const double pitchRatio = std::pow (2.0, semitones / 12.0);
    auto stretched = stretch (input, ratio * pitchRatio, sampleRate);
    auto out = resample (stretched, sampleRate * pitchRatio, sampleRate);
    const int want = (int) std::round (input.getNumSamples() * ratio);
    if (out.getNumSamples() != want)
        out.setSize (out.getNumChannels(), want, true, true);
    return out;
}

juce::AudioBuffer<float> pitchShift (const juce::AudioBuffer<float>& input, double semitones, double sampleRate)
{
    return process (input, 1.0, semitones, sampleRate);
}

juce::AudioBuffer<float> warp (const juce::AudioBuffer<float>& input, double sampleRate,
                               const std::vector<double>& src, const std::vector<double>& dst)
{
    if (src.size() < 2 || src.size() != dst.size())
        return input;

    const int nch = input.getNumChannels();
    const int outLen = (int) std::ceil (dst.back() * sampleRate);
    juce::AudioBuffer<float> out (nch, juce::jmax (1, outLen));
    out.clear();

    const int fade = (int) (0.005 * sampleRate);
    for (size_t seg = 0; seg + 1 < src.size(); ++seg)
    {
        const int s0 = juce::jlimit (0, input.getNumSamples(), (int) (src[seg] * sampleRate));
        const int s1 = juce::jlimit (0, input.getNumSamples(), (int) (src[seg + 1] * sampleRate));
        const int d0 = (int) (dst[seg] * sampleRate);
        const int d1 = (int) (dst[seg + 1] * sampleRate);
        if (s1 <= s0 || d1 <= d0) continue;

        // Take a little extra so segments can crossfade at the joins.
        const int extra = juce::jmin (fade, input.getNumSamples() - s1);
        juce::AudioBuffer<float> piece (nch, s1 - s0 + extra);
        for (int ch = 0; ch < nch; ++ch)
            piece.copyFrom (ch, 0, input, ch, s0, s1 - s0 + extra);

        auto stretched = stretch (piece, (double) (d1 - d0) / (double) (s1 - s0), sampleRate);
        const int len = juce::jmin (stretched.getNumSamples(), out.getNumSamples() - d0);
        for (int ch = 0; ch < nch; ++ch)
            for (int i = 0; i < len; ++i)
            {
                float g = 1.0f;
                if (seg > 0 && i < fade) g = (float) i / (float) fade;
                if (i >= d1 - d0) g *= 1.0f - (float) (i - (d1 - d0)) / (float) juce::jmax (1, extra);
                out.addSample (ch, d0 + i, stretched.getSample (ch, i) * g);
            }
    }
    return out;
}

} // namespace studio::dsp::timestretch
