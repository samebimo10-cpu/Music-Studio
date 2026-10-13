#include "AudioAnalysis.h"
#include "../dsp/DspCommon.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <numeric>

namespace studio::analysis
{

namespace
{
    /** Mono mix of a region. */
    std::vector<float> monoOf (const juce::AudioBuffer<float>& b, int start, int len)
    {
        std::vector<float> m ((size_t) juce::jmax (0, len), 0.0f);
        const int nch = b.getNumChannels();
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* d = b.getReadPointer (ch);
            for (int i = 0; i < len; ++i)
                m[(size_t) i] += d[start + i] / (float) nch;
        }
        return m;
    }

    /** Power spectra of Hann-windowed frames, averaged or per-frame via callback. */
    template <typename Fn>
    void forEachFrame (const std::vector<float>& x, int order, int hop, Fn&& fn)
    {
        const int n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) n), buf ((size_t) n * 2);
        for (int i = 0; i < n; ++i)
            win[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * dsp::pi * i / n);

        std::vector<float> power ((size_t) n / 2 + 1);
        for (size_t start = 0; start + (size_t) n <= x.size(); start += (size_t) hop)
        {
            for (int i = 0; i < n; ++i) buf[(size_t) i] = x[start + (size_t) i] * win[(size_t) i];
            std::fill (buf.begin() + n, buf.end(), 0.0f);
            fft.performRealOnlyForwardTransform (buf.data(), true);
            for (int k = 0; k <= n / 2; ++k)
                power[(size_t) k] = buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)];
            fn (power, (int) start);
        }
    }

    float dbOfPower (double p) { return p <= 1.0e-20 ? -120.0f : (float) (10.0 * std::log10 (p)); }

    // Krumhansl-Kessler key profiles.
    constexpr float majorProfile[12] { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    constexpr float minorProfile[12] { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    float correlate (const std::array<float, 12>& c, const float* profile, int shift)
    {
        float mc = 0, mp = 0;
        for (int i = 0; i < 12; ++i) { mc += c[(size_t) i]; mp += profile[i]; }
        mc /= 12; mp /= 12;
        float num = 0, dc = 0, dp = 0;
        for (int i = 0; i < 12; ++i)
        {
            const float a = c[(size_t) ((i + shift) % 12)] - mc;
            const float b = profile[i] - mp;
            num += a * b; dc += a * a; dp += b * b;
        }
        return dc > 0 && dp > 0 ? num / std::sqrt (dc * dp) : 0.0f;
    }
}

//==============================================================================
const std::array<float, SpectrumProfile::numBands>& SpectrumProfile::bandCentres()
{
    static const std::array<float, numBands> c { 31.5f, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    return c;
}

std::array<float, SpectrumProfile::numBands> SpectrumProfile::tilt() const
{
    std::array<float, numBands> t {};
    float mean = 0;
    for (auto v : bandDb) mean += v;
    mean /= numBands;
    for (int i = 0; i < numBands; ++i) t[(size_t) i] = bandDb[(size_t) i] - mean;
    return t;
}

SpectrumProfile analyseSpectrum (const juce::AudioBuffer<float>& buffer, double sr, int start, int numSamples)
{
    SpectrumProfile prof;
    if (numSamples < 0) numSamples = buffer.getNumSamples() - start;
    numSamples = juce::jmin (numSamples, buffer.getNumSamples() - start);
    if (numSamples <= 0 || buffer.getNumChannels() == 0)
        return prof;

    auto mono = monoOf (buffer, start, numSamples);

    // Level, peak, active fraction.
    double sumSq = 0;
    float peak = 0;
    int activeBlocks = 0, totalBlocks = 0;
    const int block = (int) (0.1 * sr);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = std::max (peak, buffer.getMagnitude (ch, start, numSamples));
    for (int b = 0; b + block <= numSamples; b += block)
    {
        double bs = 0;
        for (int i = 0; i < block; ++i) bs += (double) mono[(size_t) (b + i)] * mono[(size_t) (b + i)];
        sumSq += bs;
        ++totalBlocks;
        if (dbOfPower (bs / block) > -50.0f) ++activeBlocks;
    }
    prof.rmsDb = dbOfPower (sumSq / juce::jmax (1, totalBlocks * block));
    prof.peakDb = dsp::gainToDb (peak);
    prof.crestDb = prof.peakDb - prof.rmsDb;
    prof.activeFraction = totalBlocks > 0 ? (float) activeBlocks / (float) totalBlocks : 0.0f;

    // Octave bands from averaged power spectra (only active frames count).
    const int order = 12;
    const int n = 1 << order;
    std::array<double, SpectrumProfile::numBands> bandPower {};
    double centroidNum = 0, centroidDen = 0;
    int frames = 0;
    forEachFrame (mono, order, n / 2, [&] (const std::vector<float>& power, int)
    {
        double total = 0;
        for (auto p : power) total += p;
        if (dbOfPower (total / (n * n / 4.0)) < -60.0f) return;
        ++frames;
        for (int k = 1; k < (int) power.size(); ++k)
        {
            const double f = k * sr / n;
            centroidNum += f * power[(size_t) k];
            centroidDen += power[(size_t) k];
            for (int b = 0; b < SpectrumProfile::numBands; ++b)
            {
                const double c = SpectrumProfile::bandCentres()[(size_t) b];
                if (f >= c / std::sqrt (2.0) && f < c * std::sqrt (2.0))
                {
                    bandPower[(size_t) b] += power[(size_t) k];
                    break;
                }
            }
        }
    });

    // Normalise so a full-scale sine reads about 0 dB in its band.
    const double norm = (double) frames * (n * n / 16.0);
    for (int b = 0; b < SpectrumProfile::numBands; ++b)
        prof.bandDb[(size_t) b] = frames > 0 ? dbOfPower (bandPower[(size_t) b] / norm) : -120.0f;
    prof.centroidHz = centroidDen > 0 ? (float) (centroidNum / centroidDen) : 0.0f;

    // Stereo correlation and width.
    if (buffer.getNumChannels() >= 2)
    {
        auto* l = buffer.getReadPointer (0, start);
        auto* r = buffer.getReadPointer (1, start);
        double lr = 0, ll = 0, rr = 0, mid = 0, side = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            lr += (double) l[i] * r[i]; ll += (double) l[i] * l[i]; rr += (double) r[i] * r[i];
            const double m = 0.5 * (l[i] + r[i]), s = 0.5 * (l[i] - r[i]);
            mid += m * m; side += s * s;
        }
        prof.stereoCorrelation = (ll > 0 && rr > 0) ? (float) (lr / std::sqrt (ll * rr)) : 1.0f;
        prof.stereoWidth = mid > 0 ? (float) (side / mid) : 0.0f;
    }
    else
    {
        prof.stereoCorrelation = 1.0f;
        prof.stereoWidth = 0.0f;
    }
    return prof;
}

//==============================================================================
std::array<float, 12> chroma (const juce::AudioBuffer<float>& buffer, double sr, int start, int length)
{
    std::array<float, 12> c {};
    length = juce::jmin (length, buffer.getNumSamples() - start);
    if (length <= 0) return c;

    auto mono = monoOf (buffer, start, length);
    const int order = 12; // 4096-point frames, 1/4 overlap
    const int n = 1 << order;
    if ((int) mono.size() < n) mono.resize ((size_t) n, 0.0f);

    // Collect magnitudes between 100 Hz and 4.2 kHz. Below 100 Hz the kick drum's
    // pitched thump would bias the result; bass notes still count through their harmonics.
    const int kLo = juce::jmax (1, (int) std::ceil (100.0 * n / sr));
    const int kHi = juce::jmin (n / 2, (int) (4200.0 * n / sr));
    const int bins = kHi - kLo + 1;
    std::vector<std::vector<float>> frames;
    forEachFrame (mono, order, n / 4, [&] (const std::vector<float>& power, int)
    {
        std::vector<float> m ((size_t) bins);
        for (int k = 0; k < bins; ++k) m[(size_t) k] = std::sqrt (power[(size_t) (k + kLo)]);
        frames.push_back (std::move (m));
    });

    // Harmonic/percussive split (median across time vs across frequency): only
    // sustained, pitched energy counts, so drums and percussion do not skew the key.
    const int T = (int) frames.size();
    std::vector<float> win;
    for (int f = 0; f < T; ++f)
    {
        for (int k = 0; k < bins; ++k)
        {
            win.clear();
            for (int t = juce::jmax (0, f - 4); t <= juce::jmin (T - 1, f + 4); ++t) win.push_back (frames[(size_t) t][(size_t) k]);
            std::nth_element (win.begin(), win.begin() + (long) win.size() / 2, win.end());
            const float h = win[win.size() / 2];
            win.clear();
            for (int q = juce::jmax (0, k - 4); q <= juce::jmin (bins - 1, k + 4); ++q) win.push_back (frames[(size_t) f][(size_t) q]);
            std::nth_element (win.begin(), win.begin() + (long) win.size() / 2, win.end());
            const float p = win[win.size() / 2];
            if (h <= p) continue;

            const double freq = (k + kLo) * sr / n;
            const double midi = theory::hzToMidi (freq);
            const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
            const double dev = std::abs (midi - std::round (midi));
            c[(size_t) pc] += (float) (h * (1.0 - dev));
        }
    }

    const float maxV = *std::max_element (c.begin(), c.end());
    if (maxV > 0)
        for (auto& v : c) v /= maxV;
    return c;
}

KeyEstimate detectKey (const juce::AudioBuffer<float>& buffer, double sr)
{
    KeyEstimate est;
    auto c = chroma (buffer, sr, 0, buffer.getNumSamples());

    float best = -2, second = -2;
    for (int tonic = 0; tonic < 12; ++tonic)
    {
        for (int minor = 0; minor < 2; ++minor)
        {
            const float r = correlate (c, minor ? minorProfile : majorProfile, tonic);
            if (r > best)
            {
                second = best;
                best = r;
                est.key.tonic = tonic;
                est.key.scale = minor ? ScaleType::NaturalMinor : ScaleType::Major;
            }
            else if (r > second) second = r;
        }
    }
    est.confidence = juce::jlimit (0.0f, 1.0f, (best - second) * 4.0f + juce::jmax (0.0f, best) * 0.5f);
    return est;
}

TempoEstimate detectTempo (const juce::AudioBuffer<float>& buffer, double sr)
{
    TempoEstimate est;
    auto mono = monoOf (buffer, 0, buffer.getNumSamples());
    const int order = 10, hop = 512;

    // Onset strength: half-wave rectified spectral flux on a log-magnitude spectrum.
    std::vector<float> flux, prev;
    forEachFrame (mono, order, hop, [&] (const std::vector<float>& power, int)
    {
        if (prev.empty()) prev.assign (power.size(), 0.0f);
        float f = 0;
        for (size_t k = 1; k < power.size(); ++k)
        {
            const float m = std::log1p (1000.0f * std::sqrt (power[k]));
            f += std::max (0.0f, m - prev[k]);
            prev[k] = m;
        }
        flux.push_back (f);
    });
    if (flux.size() < 64)
        return est;

    // Remove the local mean so sustained loudness does not bias the autocorrelation.
    std::vector<float> o (flux.size());
    for (size_t i = 0; i < flux.size(); ++i)
    {
        float mean = 0; int cnt = 0;
        for (int k = -8; k <= 8; ++k)
            if ((int) i + k >= 0 && (int) i + k < (int) flux.size()) { mean += flux[(size_t) ((int) i + k)]; ++cnt; }
        o[i] = std::max (0.0f, flux[i] - mean / (float) cnt);
    }

    const double framesPerSec = sr / hop;
    double bestScore = 0, total = 0;
    int count = 0;
    for (double bpm = 60.0; bpm <= 180.0; bpm += 0.5)
    {
        const double lag = framesPerSec * 60.0 / bpm;
        double score = 0;
        // Sum autocorrelation at the beat lag and its multiples (comb), fractional lags interpolated.
        for (int mult = 1; mult <= 4; ++mult)
        {
            const double L = lag * mult;
            const int li = (int) L;
            const double fr = L - li;
            double ac = 0;
            for (size_t i = (size_t) li + 1; i < o.size(); ++i)
                ac += o[i] * (o[i - (size_t) li] * (1 - fr) + o[i - (size_t) li - 1] * fr);
            score += ac / mult;
        }
        // Mild preference for common tempos (log-normal around 110 BPM).
        const double w = std::exp (-0.5 * std::pow (std::log2 (bpm / 110.0) / 0.9, 2.0));
        score *= w;
        total += score; ++count;
        if (score > bestScore) { bestScore = score; est.bpm = bpm; }
    }
    est.confidence = total > 0 ? (float) juce::jlimit (0.0, 1.0, (bestScore / (total / count) - 1.0) / 2.0) : 0.0f;
    return est;
}

std::vector<ChordAtTime> detectChords (const juce::AudioBuffer<float>& buffer, double sr, double segmentSeconds)
{
    std::vector<ChordAtTime> out;
    const int seg = (int) (segmentSeconds * sr);
    for (int start = 0; start + seg / 2 < buffer.getNumSamples(); start += seg)
    {
        auto c = chroma (buffer, sr, start, seg);
        float best = 0;
        Chord bestChord;
        for (int root = 0; root < 12; ++root)
        {
            for (auto q : { ChordQuality::Major, ChordQuality::Minor, ChordQuality::Dominant7, ChordQuality::Minor7 })
            {
                Chord ch { root, q, -1 };
                float score = 0;
                auto iv = ch.intervals();
                std::array<bool, 12> tone {};
                for (auto i : iv) tone[(size_t) ((root + i) % 12)] = true;
                for (int pc = 0; pc < 12; ++pc)
                    score += tone[(size_t) pc] ? c[(size_t) pc] : -0.35f * c[(size_t) pc];
                score /= (float) iv.size();
                if (iv.size() > 3) score -= 0.03f;
                if (score > best) { best = score; bestChord = ch; }
            }
        }
        if (best > 0.25f)
        {
            if (! out.empty() && out.back().chord == bestChord)
                continue; // extend the previous chord
            out.push_back ({ start / sr, bestChord, juce::jlimit (0.0f, 1.0f, best) });
        }
    }
    return out;
}

//==============================================================================
RoomCheckResult analyseRoom (const juce::AudioBuffer<float>& silence, double sr)
{
    RoomCheckResult r;
    auto mono = monoOf (silence, 0, silence.getNumSamples());
    if (mono.empty())
    {
        r.rating = "No signal";
        r.suggestions.add ("No audio was recorded. Check that the right input is selected.");
        return r;
    }

    double sumSq = 0;
    for (auto v : mono) sumSq += (double) v * v;
    r.noiseFloorDb = dbOfPower (sumSq / (double) mono.size());
    r.clipped = countClippedSamples (silence) > 0;

    // Long FFT for hum lines and broad bands.
    const int order = 14, n = 1 << order;
    std::vector<double> avg ((size_t) n / 2 + 1, 0.0);
    int frames = 0;
    auto padded = mono;
    if ((int) padded.size() < n) padded.resize ((size_t) n, 0.0f);
    forEachFrame (padded, order, n / 2, [&] (const std::vector<float>& power, int)
    {
        for (size_t k = 0; k < power.size(); ++k) avg[k] += power[k];
        ++frames;
    });
    const double norm = juce::jmax (1, frames) * (n * n / 4.0);

    auto bandDb = [&] (double lo, double hi)
    {
        double s = 0;
        for (int k = (int) (lo * n / sr); k <= (int) (hi * n / sr) && k < (int) avg.size(); ++k) s += avg[(size_t) k];
        return dbOfPower (s / norm);
    };

    auto lineDb = [&] (double f)
    {
        double s = 0;
        for (int h = 1; h <= 6; ++h)
        {
            const int k = (int) std::round (f * h * n / sr);
            for (int d = -2; d <= 2; ++d)
                if (k + d > 0 && k + d < (int) avg.size()) s += avg[(size_t) (k + d)];
        }
        return dbOfPower (s / norm);
    };

    const float hum50 = lineDb (50.0), hum60 = lineDb (60.0);
    const float broadband = bandDb (30.0, 400.0);
    r.humFrequency = 0;
    r.humDb = std::max (hum50, hum60);
    if (r.humDb > -75.0f && r.humDb > broadband - 3.0f)
        r.humFrequency = hum50 >= hum60 ? 50 : 60;
    r.hissDb = bandDb (4000.0, std::min (16000.0, sr * 0.45));
    r.rumbleDb = bandDb (20.0, 80.0);

    // Noise profile for the denoiser (same 1024-point frames as StftEffect).
    {
        const int pOrder = 10, pn = 1 << pOrder;
        std::vector<double> prof ((size_t) pn / 2 + 1, 0.0);
        int pf = 0;
        forEachFrame (mono, pOrder, pn / 4, [&] (const std::vector<float>& power, int)
        {
            for (size_t k = 0; k < power.size(); ++k) prof[k] += power[k];
            ++pf;
        });
        r.noiseProfile.resize (prof.size());
        for (size_t k = 0; k < prof.size(); ++k)
            r.noiseProfile[k] = (float) (prof[k] / juce::jmax (1, pf));
    }

    if (r.noiseFloorDb < -70.0f)      r.rating = "Great";
    else if (r.noiseFloorDb < -60.0f) r.rating = "Good";
    else if (r.noiseFloorDb < -48.0f) r.rating = "Noisy";
    else                              r.rating = "Too noisy";

    if (r.clipped)
        r.suggestions.add ("The input clipped during the silence check. Something loud is near the mic, or the gain is far too high.");
    if (r.humFrequency > 0)
        r.suggestions.add (juce::String::formatted ("Mains/generator hum at %d Hz. Try another power socket, keep cables away from power leads, "
                                                    "or move away from the generator. Hum removal can take out what remains.", r.humFrequency));
    if (r.hissDb > -70.0f)
        r.suggestions.add ("High-frequency hiss. Lower the interface gain and sing closer to the mic, or check for a noisy USB port or cable.");
    if (r.rumbleDb > -60.0f)
        r.suggestions.add ("Low rumble (traffic, AC, footsteps). Use a low-cut filter and keep the mic stand off the floor or desk.");
    if (r.noiseFloorDb >= -60.0f)
        r.suggestions.add ("Turn off fans and the AC while recording, close windows, and hang blankets or duvets around the mic to calm room echo.");
    if (r.suggestions.isEmpty())
        r.suggestions.add ("The room is quiet. You're ready to record.");
    return r;
}

InputLevelAdvice adviseInputLevel (float peakDb, float rmsDb)
{
    InputLevelAdvice a;
    a.peakDb = peakDb;
    a.rmsDb = rmsDb;
    a.clipping = peakDb > -0.5f;
    a.tooQuiet = peakDb < -24.0f;
    if (a.clipping)       a.message = "Too loud: the signal is clipping. Turn the gain down until the loudest line peaks around -10 dB.";
    else if (peakDb > -6) a.message = "A bit hot. Turn the gain down slightly to leave room for loud notes.";
    else if (a.tooQuiet)  a.message = "Too quiet. Turn the gain up or move closer to the mic (about a hand's width away).";
    else if (peakDb < -18) a.message = "Slightly low, but usable. A little more gain would help.";
    else                  a.message = "Good level.";
    return a;
}

//==============================================================================
float leadingSilenceSeconds (const juce::AudioBuffer<float>& b, double sr, float thresholdDb)
{
    const float thr = dsp::dbToGain (thresholdDb);
    for (int i = 0; i < b.getNumSamples(); ++i)
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            if (std::abs (b.getSample (ch, i)) > thr)
                return (float) (i / sr);
    return (float) (b.getNumSamples() / sr);
}

float trailingSilenceSeconds (const juce::AudioBuffer<float>& b, double sr, float thresholdDb)
{
    const float thr = dsp::dbToGain (thresholdDb);
    for (int i = b.getNumSamples() - 1; i >= 0; --i)
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            if (std::abs (b.getSample (ch, i)) > thr)
                return (float) ((b.getNumSamples() - 1 - i) / sr);
    return (float) (b.getNumSamples() / sr);
}

int countClippedSamples (const juce::AudioBuffer<float>& b, float threshold)
{
    int count = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        auto* d = b.getReadPointer (ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs (d[i]) >= threshold) ++count;
    }
    return count;
}

} // namespace studio::analysis
