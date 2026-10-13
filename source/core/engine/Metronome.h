#pragma once

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../model/TempoMap.h"

namespace studio
{

/** Click generator: an accented downbeat and softer beats, sample-accurate
    against the tempo map. Real-time safe. */
class Metronome
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { remaining = 0; phase = 0; }
    void setVolume (float v) { volume = v; }

    /** Adds clicks for the timeline range [pos, pos + num) into out (all channels or one). */
    void render (juce::AudioBuffer<float>& out, int startSample, int num, juce::int64 pos,
                 const TempoMap& tempo, int beatsPerBar, int onlyChannel = -1) noexcept
    {
        const double beat0 = tempo.samplesToBeats (pos, sr);
        double nextBeat = std::ceil (beat0 - 1.0e-9);
        juce::int64 nextClick = tempo.beatsToSamples (nextBeat, sr);

        for (int i = 0; i < num; ++i)
        {
            const juce::int64 s = pos + i;
            if (s == nextClick)
            {
                const bool downbeat = ((juce::int64) std::llround (nextBeat) % juce::jmax (1, beatsPerBar)) == 0;
                trigger (downbeat);
                nextBeat += 1.0;
                nextClick = tempo.beatsToSamples (nextBeat, sr);
            }
            if (remaining > 0)
            {
                const float env = (float) remaining / (float) length;
                const float v = (float) std::sin (phase) * env * env * volume * (accent ? 1.0f : 0.6f);
                phase += 2.0 * juce::MathConstants<double>::pi * (accent ? 1760.0 : 1320.0) / sr;
                --remaining;
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    if (onlyChannel < 0 || ch == onlyChannel)
                        out.addSample (ch, startSample + i, v);
            }
        }
    }

    /** A free-running count-in (independent of the song position). */
    void renderCountIn (juce::AudioBuffer<float>& out, int startSample, int num, juce::int64 countInPos,
                        double samplesPerBeat, int beatsPerBar) noexcept
    {
        for (int i = 0; i < num; ++i)
        {
            const juce::int64 s = countInPos + i;
            const auto spb = (juce::int64) std::llround (samplesPerBeat);
            if (spb > 0 && s % spb == 0)
                trigger ((s / spb) % juce::jmax (1, beatsPerBar) == 0);
            if (remaining > 0)
            {
                const float env = (float) remaining / (float) length;
                const float v = (float) std::sin (phase) * env * env * volume * (accent ? 1.0f : 0.6f);
                phase += 2.0 * juce::MathConstants<double>::pi * (accent ? 1760.0 : 1320.0) / sr;
                --remaining;
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, startSample + i, v);
            }
        }
    }

private:
    void trigger (bool downbeat) noexcept
    {
        accent = downbeat;
        length = (int) (0.035 * sr);
        remaining = length;
        phase = 0;
    }

    double sr = 48000;
    int remaining = 0, length = 1;
    double phase = 0;
    bool accent = false;
    float volume = 0.5f;
};

} // namespace studio
