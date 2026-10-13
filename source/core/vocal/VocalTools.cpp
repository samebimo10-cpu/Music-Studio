#include "VocalTools.h"
#include "../dsp/VoiceEffects.h"
#include "../dsp/TimeStretch.h"
#include "../model/TempoMap.h"
#include "../midi/MidiTools.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>

namespace studio::vocal
{

//==============================================================================
// Comping
//==============================================================================
void setCompSegment (Track& t, const juce::String& takeId, double start, double end)
{
    if (end <= start) return;
    std::vector<CompSegment> out;
    for (auto& s : t.comp)
    {
        if (s.endBeat <= start || s.startBeat >= end) { out.push_back (s); continue; }
        if (s.startBeat < start) out.push_back ({ s.takeId, s.startBeat, start });
        if (s.endBeat > end) out.push_back ({ s.takeId, end, s.endBeat });
    }
    out.push_back ({ takeId, start, end });
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
    t.comp = out;
}

void resetCompToLatest (Track& t)
{
    t.comp.clear();
    for (auto& take : t.takes) // later takes overwrite earlier ones where they overlap
        setCompSegment (t, take.id, take.startBeat, take.startBeat + take.lengthBeats);
}

void buildCompClips (const Project& p, Track& t, double xfade)
{
    TempoMap tm (p);
    // Remove clips that came from comping; keep imported clips.
    t.clips.erase (std::remove_if (t.clips.begin(), t.clips.end(), [] (auto& c) { return c.takeId.isNotEmpty(); }), t.clips.end());

    for (size_t i = 0; i < t.comp.size(); ++i)
    {
        auto& seg = t.comp[i];
        const Take* take = nullptr;
        for (auto& tk : t.takes) if (tk.id == seg.takeId) take = &tk;
        if (take == nullptr) continue;

        // Overlap neighbours by half a crossfade on each side.
        const bool hasPrev = i > 0 && std::abs (t.comp[i - 1].endBeat - seg.startBeat) < 1.0e-6;
        const bool hasNext = i + 1 < t.comp.size() && std::abs (t.comp[i + 1].startBeat - seg.endBeat) < 1.0e-6;
        const double s = juce::jmax (take->startBeat, seg.startBeat - (hasPrev ? xfade / 2 : 0.0));
        const double e = juce::jmin (take->startBeat + take->lengthBeats, seg.endBeat + (hasNext ? xfade / 2 : 0.0));
        if (e <= s) continue;

        Clip c;
        c.id = makeId ("clip");
        c.name = "Comp " + juce::String ((int) i + 1);
        c.audioFile = take->audioFile;
        c.startBeat = s;
        c.lengthBeats = e - s;
        c.sourceOffsetSeconds = take->sourceOffsetSeconds + (tm.beatsToSeconds (s) - tm.beatsToSeconds (take->startBeat));
        c.fadeInBeats = hasPrev ? xfade : 0.01;
        c.fadeOutBeats = hasNext ? xfade : 0.01;
        c.takeId = take->id;
        t.clips.push_back (c);
    }
}

void addTakes (const Project& p, Track& t, const std::vector<Take>& takes)
{
    for (auto& tk : takes)
    {
        t.takes.push_back (tk);
        setCompSegment (t, tk.id, tk.startBeat, tk.startBeat + tk.lengthBeats);
    }
    buildCompClips (p, t);
}

//==============================================================================
// Pitch
//==============================================================================
namespace
{
    std::vector<float> monoOf (const juce::AudioBuffer<float>& b)
    {
        std::vector<float> m ((size_t) b.getNumSamples(), 0.0f);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                m[(size_t) i] += b.getSample (ch, i) / (float) b.getNumChannels();
        return m;
    }

    constexpr int pitchHop = 256;
}

juce::AudioBuffer<float> shiftByCurve (const juce::AudioBuffer<float>& audio, double sr, const std::vector<float>& semis, int hop)
{
    juce::AudioBuffer<float> out (audio.getNumChannels(), audio.getNumSamples());
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        dsp::PitchShifter shifter;
        shifter.prepare (sr, 30.0f);
        const int lat = shifter.latencySamples();
        auto* in = audio.getReadPointer (ch);
        auto* o = out.getWritePointer (ch);
        // Run past the end by the latency, then drop the head so timing is preserved.
        for (int i = 0; i < audio.getNumSamples() + lat; ++i)
        {
            const float pos = (float) juce::jmin (i, audio.getNumSamples() - 1) / (float) hop;
            const int k = juce::jlimit (0, (int) semis.size() - 1, (int) pos);
            const int k2 = juce::jmin (k + 1, (int) semis.size() - 1);
            const float fr = pos - (float) (int) pos;
            const float st = semis.empty() ? 0.0f : semis[(size_t) k] + (semis[(size_t) k2] - semis[(size_t) k]) * fr;
            shifter.setRatio (std::pow (2.0f, st / 12.0f));
            const float y = shifter.process (i < audio.getNumSamples() ? in[i] : 0.0f);
            if (i - lat >= 0) o[i - lat] = y;
        }
    }
    return out;
}

PitchAnalysis analyseForEditor (const juce::AudioBuffer<float>& audio, double sr, const Key& key)
{
    PitchAnalysis a;
    a.sampleRate = sr;
    auto mono = monoOf (audio);
    a.frames = analysePitch (mono.data(), (int) mono.size(), sr, pitchHop);
    for (auto& n : segmentNotes (a.frames))
    {
        PitchNote pn;
        pn.startSeconds = n.startSeconds;
        pn.endSeconds = n.endSeconds;
        pn.detectedMidi = n.midiPitch;
        pn.targetMidi = (float) key.snapToScale ((int) std::round (n.midiPitch));
        a.notes.push_back (pn);
    }
    return a;
}

juce::AudioBuffer<float> applyPitchEdits (const juce::AudioBuffer<float>& audio, const PitchAnalysis& a, float strength)
{
    // Per-frame correction: inside a note, move the detected pitch toward its target,
    // keeping the singer's natural movement around the note (vibrato, scoops).
    std::vector<float> curve (a.frames.size(), 0.0f);
    for (auto& n : a.notes)
    {
        const float amt = n.edited ? 1.0f : strength;
        const float shift = (n.targetMidi - n.detectedMidi) * amt;
        for (size_t i = 0; i < a.frames.size(); ++i)
        {
            const double t = a.frames[i].timeSeconds;
            if (t < n.startSeconds - 0.02 || t > n.endSeconds + 0.02) continue;
            // 20 ms ramps at the note edges.
            const double edge = std::min (t - (n.startSeconds - 0.02), (n.endSeconds + 0.02) - t);
            curve[i] = shift * (float) juce::jlimit (0.0, 1.0, edge / 0.02);
        }
    }
    return shiftByCurve (audio, a.sampleRate, curve, pitchHop);
}

//==============================================================================
// Harmony, doubling
//==============================================================================
std::vector<int> harmonyPreset (int parts)
{
    switch (juce::jlimit (2, 4, parts))
    {
        case 2:  return { 2, -3 };          // third above, fourth below (fifth below an octave up feel)
        case 3:  return { 2, 4, -3 };       // third and fifth above, fourth below
        default: return { 2, 4, -3, -5 };   // adds a sixth below
    }
}

std::vector<juce::AudioBuffer<float>> generateHarmonies (const juce::AudioBuffer<float>& lead, double sr, const Key& key,
                                                         const std::vector<int>& steps)
{
    std::vector<juce::AudioBuffer<float>> parts;
    auto mono = monoOf (lead);
    auto frames = analysePitch (mono.data(), (int) mono.size(), sr, pitchHop);

    // Smooth the detected pitch so harmonies do not jitter between notes.
    std::vector<float> midi (frames.size(), 0.0f);
    float last = 0;
    for (size_t i = 0; i < frames.size(); ++i)
    {
        if (frames[i].frequency > 0 && frames[i].confidence > 0.6f)
            last = (float) theory::hzToMidi (frames[i].frequency);
        midi[i] = last;
    }

    juce::AudioBuffer<float> monoBuf (1, lead.getNumSamples());
    monoBuf.copyFrom (0, 0, mono.data(), (int) mono.size());

    for (size_t p = 0; p < steps.size(); ++p)
    {
        std::vector<float> curve (frames.size(), 0.0f);
        for (size_t i = 0; i < frames.size(); ++i)
        {
            if (midi[i] <= 0) continue;
            // Find the scale degree of the (snapped) lead note and move by scale steps.
            const int leadNote = key.snapToScale ((int) std::round (midi[i]));
            const auto& iv = key.intervals();
            const int n = (int) iv.size();
            const int rel = ((leadNote - key.tonic) % 12 + 12) % 12;
            int degree = 0;
            for (int d = 0; d < n; ++d) if (iv[(size_t) d] == rel) degree = d;
            // Absolute scale-degree index of the lead note, then move by scale steps.
            const int k = (leadNote - key.tonic - rel) / 12;
            const int target = key.degreeToNote (degree + n * k + steps[p], -1);
            curve[i] = (float) (target - leadNote);
        }
        auto shifted = shiftByCurve (monoBuf, sr, curve, pitchHop);

        // Each part sits slightly behind the lead and is panned apart.
        juce::AudioBuffer<float> stereo (2, lead.getNumSamples());
        stereo.clear();
        const int delay = (int) (sr * (0.008 + 0.006 * (double) p));
        const float pan = (p % 2 == 0 ? -1.0f : 1.0f) * (0.35f + 0.15f * (float) (p / 2));
        for (int i = 0; i + delay < lead.getNumSamples(); ++i)
        {
            const float s = shifted.getSample (0, i) * 0.7f;
            stereo.setSample (0, i + delay, s * juce::jmin (1.0f, 1.0f - pan));
            stereo.setSample (1, i + delay, s * juce::jmin (1.0f, 1.0f + pan));
        }
        parts.push_back (std::move (stereo));
    }
    return parts;
}

juce::AudioBuffer<float> renderDouble (const juce::AudioBuffer<float>& vocal, double sr, float amount)
{
    dsp::DoublerEffect d;
    d.prepare (sr, 1024, 2);
    d.setParam ("mix", amount);
    juce::AudioBuffer<float> out (2, vocal.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        out.copyFrom (ch, 0, vocal, juce::jmin (ch, vocal.getNumChannels() - 1), 0, vocal.getNumSamples());
    for (int pos = 0; pos < out.getNumSamples(); pos += 1024)
    {
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, juce::jmin (1024, out.getNumSamples() - pos));
        d.process (view, nullptr);
    }
    return out;
}

//==============================================================================
// Voice to MIDI
//==============================================================================
std::vector<MidiNote> voiceToMidi (const juce::AudioBuffer<float>& audio, double sr, double bpm, const Key* snapKey, double grid)
{
    auto mono = monoOf (audio);
    auto frames = analysePitch (mono.data(), (int) mono.size(), sr, pitchHop);
    std::vector<MidiNote> notes;
    const double beatsPerSecond = bpm / 60.0;
    for (auto& n : segmentNotes (frames, 0.07))
    {
        int pitch = (int) std::round (n.midiPitch);
        if (snapKey != nullptr) pitch = snapKey->snapToScale (pitch);
        notes.push_back ({ pitch, n.velocity, n.startSeconds * beatsPerSecond, (n.endSeconds - n.startSeconds) * beatsPerSecond });
    }
    if (grid > 0)
        midi::quantize (notes, grid, 0.8f, 0.0f, 0.0, true);
    return notes;
}

//==============================================================================
// Timing alignment
//==============================================================================
juce::AudioBuffer<float> alignTiming (const juce::AudioBuffer<float>& lead, const juce::AudioBuffer<float>& backing, double sr, float strength)
{
    const int hop = (int) (0.01 * sr);
    auto envelope = [hop] (const juce::AudioBuffer<float>& b)
    {
        std::vector<float> e;
        juce::AudioBuffer<float> tmp;
        const int n = b.getNumSamples() / hop;
        float prev = 0;
        for (int k = 0; k < n; ++k)
        {
            double s = 0;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int i = 0; i < hop; ++i) { const float v = b.getSample (ch, k * hop + i); s += v * v; }
            const float db = 10.0f * std::log10 ((float) (s / (hop * b.getNumChannels())) + 1.0e-10f);
            // Log energy plus its rise (onsets) make a robust alignment feature.
            const float rise = juce::jmax (0.0f, db - prev);
            prev = db;
            e.push_back (juce::jmax (-60.0f, db) / 60.0f + rise / 20.0f);
        }
        return e;
    };

    auto a = envelope (lead), b = envelope (backing);
    const int n = (int) a.size(), m = (int) b.size();
    if (n < 4 || m < 4)
        return backing;

    // DTW constrained to a band of +-300 ms (singers are rarely further apart than that).
    const int band = 30;
    const float inf = 1.0e30f;
    std::vector<float> cost ((size_t) n * (size_t) (2 * band + 1), inf);
    auto at = [&] (int i, int j) -> float& { return cost[(size_t) i * (size_t) (2 * band + 1) + (size_t) (j - i + band)]; };
    auto inBand = [&] (int i, int j) { return j - i >= -band && j - i <= band && j >= 0 && j < m; };

    for (int i = 0; i < n; ++i)
        for (int j = juce::jmax (0, i - band); j <= juce::jmin (m - 1, i + band); ++j)
        {
            const float d = std::abs (a[(size_t) i] - b[(size_t) j]);
            float best = (i == 0 && j == 0) ? 0.0f : inf;
            if (i > 0 && inBand (i - 1, j)) best = std::min (best, at (i - 1, j) + 0.1f);
            if (j > 0 && inBand (i, j - 1)) best = std::min (best, at (i, j - 1) + 0.1f);
            if (i > 0 && j > 0 && inBand (i - 1, j - 1)) best = std::min (best, at (i - 1, j - 1));
            at (i, j) = d + best;
        }

    // Backtrack from the end: path maps lead frame i -> backing frame j.
    std::vector<int> map ((size_t) n, 0);
    int i = n - 1, j = juce::jlimit (0, m - 1, n - 1);
    while (i > 0 || j > 0)
    {
        map[(size_t) i] = j;
        float c1 = (i > 0 && j > 0 && inBand (i - 1, j - 1)) ? at (i - 1, j - 1) : inf;
        float c2 = (i > 0 && inBand (i - 1, j)) ? at (i - 1, j) : inf;
        float c3 = (j > 0 && inBand (i, j - 1)) ? at (i, j - 1) : inf;
        if (c1 <= c2 && c1 <= c3) { --i; --j; }
        else if (c2 <= c3) --i;
        else --j;
        if (i < 0 || j < 0) break;
    }

    // Anchor points every 100 ms: backing time j should land at lead time i (blended by strength).
    std::vector<double> src, dst;
    src.push_back (0); dst.push_back (0);
    for (int k = 10; k < n - 1; k += 10)
    {
        const double srcT = map[(size_t) k] * hop / sr;
        const double leadT = k * hop / sr;
        const double target = srcT + (leadT - srcT) * strength;
        if (srcT > src.back() + 0.02 && target > dst.back() + 0.02) { src.push_back (srcT); dst.push_back (target); }
    }
    const double endSrc = backing.getNumSamples() / sr;
    src.push_back (endSrc);
    dst.push_back (dst.back() + (endSrc - src[src.size() - 2]));
    return dsp::timestretch::warp (backing, sr, src, dst);
}

//==============================================================================
// Stem separation (DSP)
//==============================================================================
Stems separateStems (const juce::AudioBuffer<float>& mixIn, double sr, std::function<bool (float)> progress)
{
    const int order = 11, N = 1 << order, hop = N / 4, bins = N / 2 + 1;
    const int len = mixIn.getNumSamples();
    juce::AudioBuffer<float> mix (2, len);
    for (int ch = 0; ch < 2; ++ch) mix.copyFrom (ch, 0, mixIn, juce::jmin (ch, mixIn.getNumChannels() - 1), 0, len);

    const int frames = len / hop + 1;
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * juce::MathConstants<double>::pi * i / N);

    // Complex spectra for L and R.
    std::vector<std::vector<std::complex<float>>> spec[2];
    for (int ch = 0; ch < 2; ++ch)
    {
        spec[ch].assign ((size_t) frames, std::vector<std::complex<float>> ((size_t) bins));
        std::vector<float> buf ((size_t) N * 2);
        for (int f = 0; f < frames; ++f)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i)
            {
                const int idx = f * hop + i - N / 2;
                buf[(size_t) i] = (idx >= 0 && idx < len ? mix.getSample (ch, idx) : 0.0f) * win[(size_t) i];
            }
            fft.performRealOnlyForwardTransform (buf.data(), true);
            for (int k = 0; k < bins; ++k) spec[ch][(size_t) f][(size_t) k] = { buf[(size_t) (2 * k)], buf[(size_t) (2 * k + 1)] };
        }
    }
    if (progress && ! progress (0.25f)) return {};

    // Magnitude of the mid signal for harmonic/percussive median filtering.
    std::vector<std::vector<float>> mag ((size_t) frames, std::vector<float> ((size_t) bins));
    for (int f = 0; f < frames; ++f)
        for (int k = 0; k < bins; ++k)
            mag[(size_t) f][(size_t) k] = std::abs (spec[0][(size_t) f][(size_t) k] + spec[1][(size_t) f][(size_t) k]) * 0.5f;

    const int halfT = 8, halfF = 8;
    std::vector<std::vector<float>> harm ((size_t) frames, std::vector<float> ((size_t) bins)), perc = harm;
    std::vector<float> window;
    for (int f = 0; f < frames; ++f)
    {
        for (int k = 0; k < bins; ++k)
        {
            window.clear();
            for (int t = juce::jmax (0, f - halfT); t <= juce::jmin (frames - 1, f + halfT); ++t) window.push_back (mag[(size_t) t][(size_t) k]);
            std::nth_element (window.begin(), window.begin() + (long) window.size() / 2, window.end());
            harm[(size_t) f][(size_t) k] = window[window.size() / 2];

            window.clear();
            for (int q = juce::jmax (0, k - halfF); q <= juce::jmin (bins - 1, k + halfF); ++q) window.push_back (mag[(size_t) f][(size_t) q]);
            std::nth_element (window.begin(), window.begin() + (long) window.size() / 2, window.end());
            perc[(size_t) f][(size_t) k] = window[window.size() / 2];
        }
        if (progress && f % 512 == 0 && ! progress (0.25f + 0.5f * (float) f / (float) frames)) return {};
    }

    // Soft masks per stem, per channel; overlap-add back.
    Stems out;
    for (auto* b : { &out.vocals, &out.drums, &out.bass, &out.other }) { b->setSize (2, len); b->clear(); }
    const float bassCut = 160.0f, vocalLo = 180.0f, vocalHi = 7000.0f;
    std::vector<float> buf ((size_t) N * 2);

    for (int ch = 0; ch < 2; ++ch)
    {
        for (int stem = 0; stem < 4; ++stem)
        {
            auto& target = stem == 0 ? out.vocals : stem == 1 ? out.drums : stem == 2 ? out.bass : out.other;
            for (int f = 0; f < frames; ++f)
            {
                for (int k = 0; k < bins; ++k)
                {
                    const float freq = (float) (k * sr / N);
                    const float h2 = harm[(size_t) f][(size_t) k] * harm[(size_t) f][(size_t) k];
                    const float p2 = perc[(size_t) f][(size_t) k] * perc[(size_t) f][(size_t) k];
                    const float hMask = h2 / (h2 + p2 + 1.0e-12f);
                    const float pMask = 1.0f - hMask;

                    // Centre similarity: vocals are usually panned centre.
                    const auto L = spec[0][(size_t) f][(size_t) k], R = spec[1][(size_t) f][(size_t) k];
                    const float sim = 2.0f * std::abs (L * std::conj (R)) / (std::norm (L) + std::norm (R) + 1.0e-12f);
                    const float centre = std::pow (juce::jlimit (0.0f, 1.0f, sim), 6.0f);
                    const float vocalBand = (freq >= vocalLo && freq <= vocalHi) ? 1.0f : 0.0f;
                    const float bassBand = freq < bassCut ? 1.0f : (freq < bassCut * 1.5f ? 1.0f - (freq - bassCut) / (bassCut * 0.5f) : 0.0f);

                    float m = 0;
                    switch (stem)
                    {
                        case 0: m = hMask * centre * vocalBand; break;
                        case 1: m = pMask * (1.0f - 0.5f * bassBand); break;
                        case 2: m = hMask * bassBand + pMask * 0.5f * bassBand; break;
                        case 3: m = hMask * (1.0f - bassBand) * (1.0f - centre * vocalBand); break;
                    }
                    const auto v = spec[ch][(size_t) f][(size_t) k] * m;
                    buf[(size_t) (2 * k)] = v.real();
                    buf[(size_t) (2 * k + 1)] = v.imag();
                }
                fft.performRealOnlyInverseTransform (buf.data());
                for (int i = 0; i < N; ++i)
                {
                    const int idx = f * hop + i - N / 2;
                    if (idx >= 0 && idx < len)
                        target.addSample (ch, idx, buf[(size_t) i] * win[(size_t) i] / 1.5f);
                }
            }
        }
    }
    if (progress) progress (1.0f);
    return out;
}

} // namespace studio::vocal
