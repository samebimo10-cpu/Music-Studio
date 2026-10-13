#include "PitchDetector.h"
#include "../theory/MusicTheory.h"
#include <algorithm>
#include <cmath>

namespace studio
{

void PitchDetector::prepare (double sampleRate, int windowSize, float minHz, float maxHz)
{
    sr = sampleRate;
    window = juce::nextPowerOfTwo (windowSize);
    half = window / 2;
    minTau = juce::jmax (2, (int) (sr / maxHz));
    maxTau = juce::jmin (half - 2, (int) (sr / minHz));

    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (window * 2)));
    a.assign ((size_t) window * 2, {});
    b.assign ((size_t) window * 2, {});
    tmp.assign ((size_t) window * 2, {});
    diff.assign ((size_t) half, 0.0f);
    energyPrefix.assign ((size_t) window + 1, 0.0f);
}

PitchDetector::Result PitchDetector::detect (const float* x) noexcept
{
    Result r;
    if (fft == nullptr)
        return r;

    const int n2 = window * 2;

    // Cross-correlation of the first half with the full window via FFT.
    for (int i = 0; i < n2; ++i)
    {
        a[(size_t) i] = { i < half ? x[i] : 0.0f, 0.0f };
        b[(size_t) i] = { i < window ? x[i] : 0.0f, 0.0f };
    }
    fft->perform (a.data(), tmp.data(), false);
    std::swap (a, tmp);
    fft->perform (b.data(), tmp.data(), false);
    std::swap (b, tmp);
    for (int i = 0; i < n2; ++i)
        a[(size_t) i] = std::conj (a[(size_t) i]) * b[(size_t) i];
    fft->perform (a.data(), tmp.data(), true); // tmp[tau] = sum x[j] x[j+tau] (scaled by 1/n2 in JUCE inverse)

    energyPrefix[0] = 0;
    for (int i = 0; i < window; ++i)
        energyPrefix[(size_t) i + 1] = energyPrefix[(size_t) i] + x[i] * x[i];

    const float e1 = energyPrefix[(size_t) half];
    if (e1 < 1.0e-7f * (float) half)
        return r; // silence

    // d(tau) = E[0..half) + E[tau..tau+half) - 2 r(tau), then the cumulative mean normalisation.
    diff[0] = 1.0f;
    float running = 0;
    int bestTau = -1;
    for (int tau = 1; tau < half; ++tau)
    {
        const float e2 = energyPrefix[(size_t) (tau + half)] - energyPrefix[(size_t) tau];
        const float corr = tmp[(size_t) tau].real();
        const float d = juce::jmax (0.0f, e1 + e2 - 2.0f * corr);
        running += d;
        diff[(size_t) tau] = running > 0 ? d * (float) tau / running : 1.0f;
    }

    for (int tau = minTau; tau <= maxTau; ++tau)
    {
        if (diff[(size_t) tau] < threshold)
        {
            while (tau + 1 <= maxTau && diff[(size_t) tau + 1] < diff[(size_t) tau])
                ++tau;
            bestTau = tau;
            break;
        }
    }

    if (bestTau < 0)
    {
        // No dip under the threshold: take the global minimum, but report low confidence.
        float best = 1.0f;
        for (int tau = minTau; tau <= maxTau; ++tau)
            if (diff[(size_t) tau] < best) { best = diff[(size_t) tau]; bestTau = tau; }
        if (bestTau < 0 || best > 0.35f)
            return r;
    }

    // Parabolic interpolation around the dip.
    float tauF = (float) bestTau;
    if (bestTau > 1 && bestTau < half - 1)
    {
        const float s0 = diff[(size_t) bestTau - 1], s1 = diff[(size_t) bestTau], s2 = diff[(size_t) bestTau + 1];
        const float denom = 2.0f * (2.0f * s1 - s2 - s0);
        if (std::abs (denom) > 1.0e-9f)
            tauF += (s2 - s0) / denom;
    }

    r.frequency = (float) (sr / tauF);
    r.confidence = juce::jlimit (0.0f, 1.0f, 1.0f - diff[(size_t) bestTau]);
    return r;
}

//==============================================================================
std::vector<PitchFrame> analysePitch (const float* samples, int numSamples, double sampleRate, int hop)
{
    std::vector<PitchFrame> frames;
    PitchDetector det;
    det.prepare (sampleRate, sampleRate > 60000 ? 4096 : 2048);
    const int w = det.getWindowSize();

    std::vector<float> win ((size_t) w, 0.0f);
    for (int start = -w / 2; start + w / 2 < numSamples; start += hop)
    {
        float sumSq = 0;
        for (int i = 0; i < w; ++i)
        {
            const int idx = start + i;
            const float v = (idx >= 0 && idx < numSamples) ? samples[idx] : 0.0f;
            win[(size_t) i] = v;
            sumSq += v * v;
        }

        PitchFrame f;
        f.timeSeconds = (start + w / 2) / sampleRate;
        f.rmsDb = 10.0f * std::log10 (sumSq / (float) w + 1.0e-12f);
        if (f.rmsDb > -55.0f)
        {
            auto res = det.detect (win.data());
            f.frequency = res.frequency;
            f.confidence = res.confidence;
        }
        frames.push_back (f);
    }
    return frames;
}

std::vector<DetectedNote> segmentNotes (const std::vector<PitchFrame>& frames, double minNoteSeconds)
{
    std::vector<DetectedNote> notes;
    if (frames.size() < 2)
        return notes;

    const double hop = frames[1].timeSeconds - frames[0].timeSeconds;
    std::vector<float> current;
    double start = 0;
    float peakDb = -120;

    auto flush = [&] (double end)
    {
        if (current.size() * hop >= minNoteSeconds)
        {
            auto sorted = current;
            std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
            DetectedNote n;
            n.startSeconds = start;
            n.endSeconds = end;
            n.midiPitch = sorted[sorted.size() / 2];
            n.velocity = juce::jlimit (0.2f, 1.0f, (peakDb + 50.0f) / 45.0f);
            notes.push_back (n);
        }
        current.clear();
        peakDb = -120;
    };

    for (auto& f : frames)
    {
        const bool voiced = f.frequency > 0 && f.confidence > 0.6f;
        const float midi = voiced ? (float) theory::hzToMidi (f.frequency) : 0.0f;

        if (! voiced)
        {
            if (! current.empty()) flush (f.timeSeconds);
            continue;
        }

        if (! current.empty())
        {
            // A jump of more than ~70 cents from the running median starts a new note.
            auto sorted = current;
            std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
            if (std::abs (midi - sorted[sorted.size() / 2]) > 0.7f)
                flush (f.timeSeconds);
        }

        if (current.empty()) start = f.timeSeconds;
        current.push_back (midi);
        peakDb = std::max (peakDb, f.rmsDb);
    }
    if (! current.empty())
        flush (frames.back().timeSeconds + hop);

    return notes;
}

} // namespace studio
