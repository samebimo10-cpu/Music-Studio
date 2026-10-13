#pragma once

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

namespace testutil
{
    inline juce::AudioBuffer<float> sine (double freq, double seconds, double sr, float amplitude, int channels = 2, double phase = 0)
    {
        juce::AudioBuffer<float> b (channels, (int) (seconds * sr));
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr + phase));
        return b;
    }

    inline juce::AudioBuffer<float> noise (double seconds, double sr, float amplitude, int channels = 2, int seed = 1)
    {
        juce::Random r (seed);
        juce::AudioBuffer<float> b (channels, (int) (seconds * sr));
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, amplitude * (r.nextFloat() * 2.0f - 1.0f));
        return b;
    }

    inline float rmsDb (const juce::AudioBuffer<float>& b, int start = 0, int len = -1)
    {
        if (len < 0) len = b.getNumSamples() - start;
        float r = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch) r = std::max (r, b.getRMSLevel (ch, start, len));
        return juce::Decibels::gainToDecibels (r, -120.0f);
    }

    /** Runs an effect over a buffer in blocks. */
    template <typename FX>
    void runEffect (FX& fx, juce::AudioBuffer<float>& b, int block = 512, const juce::AudioBuffer<float>* sc = nullptr)
    {
        for (int pos = 0; pos < b.getNumSamples(); pos += block)
        {
            const int n = juce::jmin (block, b.getNumSamples() - pos);
            juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), b.getNumChannels(), pos, n);
            if (sc != nullptr)
            {
                juce::AudioBuffer<float> scView (const_cast<juce::AudioBuffer<float>*> (sc)->getArrayOfWritePointers(), sc->getNumChannels(), pos, n);
                fx.process (view, &scView);
            }
            else
                fx.process (view, nullptr);
        }
    }
}
