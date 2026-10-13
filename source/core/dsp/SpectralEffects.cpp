#include "SpectralEffects.h"

namespace studio::dsp
{

//==============================================================================
void StftEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    window.resize ((size_t) frameSize);
    // Periodic Hann, applied at analysis and synthesis. At 75 % overlap the sum of
    // squared windows is 1.5, which the synthesis step divides out.
    for (int i = 0; i < frameSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * pi * i / frameSize);

    for (auto& s : state)
    {
        s.input.assign ((size_t) frameSize, 0.0f);
        s.output.assign ((size_t) frameSize * 2, 0.0f);
        s.frame.assign ((size_t) frameSize * 2 + 2, 0.0f);
    }
    prepareSpectral();
    reset();
}

void StftEffect::reset()
{
    for (auto& s : state)
    {
        std::fill (s.input.begin(), s.input.end(), 0.0f);
        std::fill (s.output.begin(), s.output.end(), 0.0f);
        s.inputPos = 0;
        s.hopCounter = 0;
        s.outputPos = 0;
    }
}

void StftEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    const int nch = juce::jmin (2, buffer.getNumChannels());
    const float synthNorm = 1.0f / 1.5f;

    for (int ch = 0; ch < nch; ++ch)
    {
        auto& s = state[(size_t) ch];
        auto* d = buffer.getWritePointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            s.input[(size_t) s.inputPos] = d[i];
            s.inputPos = (s.inputPos + 1) % frameSize;

            // Output: read the overlap-added sample, clear it for reuse.
            d[i] = s.output[(size_t) s.outputPos];
            s.output[(size_t) s.outputPos] = 0.0f;
            s.outputPos = (s.outputPos + 1) % (frameSize * 2);

            if (++s.hopCounter < hopSize)
                continue;
            s.hopCounter = 0;

            // Unwrap the circular input into the frame, windowed.
            for (int k = 0; k < frameSize; ++k)
                s.frame[(size_t) k] = s.input[(size_t) ((s.inputPos + k) % frameSize)] * window[(size_t) k];
            std::fill (s.frame.begin() + frameSize, s.frame.end(), 0.0f);

            fft.performRealOnlyForwardTransform (s.frame.data(), true);
            processFrame (ch, s.frame.data());
            fft.performRealOnlyInverseTransform (s.frame.data());

            // Overlap-add from the next read position: the frame's oldest sample comes
            // out exactly frameSize samples after it went in (one frame of latency).
            for (int k = 0; k < frameSize; ++k)
            {
                const int pos = (s.outputPos + k) % (frameSize * 2);
                s.output[(size_t) pos] += s.frame[(size_t) k] * window[(size_t) k] * synthNorm;
            }
        }
    }
}

//==============================================================================
NoiseReductionEffect::NoiseReductionEffect()
{
    addParam ({ "amount", "Reduction", 0, 40, 18, "dB", {}, false, "How far the noise is turned down" });
    addParam ({ "sensitivity", "Sensitivity", 0.5f, 4, 1.5f, "", {}, false, "Higher removes more, but can thin the voice" });
    addParam ({ "learn", "Learn noise", 0, 1, 0, "", { "Off", "Learning" }, false, "Play a few seconds of room noise only while this is on" });
    addParam ({ "hum", "Hum", 0, 2, 0, "", { "Off", "50 Hz", "60 Hz" }, false, "Removes mains/generator hum and its harmonics" });
    addParam ({ "adaptive", "Adaptive", 0, 1, 1, "", { "Off", "On" }, false, "Keeps tracking the noise when it changes" });
}

void NoiseReductionEffect::prepareSpectral()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        noise[(size_t) ch].assign (numBins, 1.0e-9f);
        gainSmooth[(size_t) ch].assign (numBins, 1.0f);
        minTrack[(size_t) ch].assign (numBins, 1.0e9f);
    }
    if (fixedProfile.size() != (size_t) numBins)
        fixedProfile.assign (numBins, 1.0e-9f);
    lastHum = -1;
}

void NoiseReductionEffect::reset()
{
    StftEffect::reset();
    framesSeen = 0;
    for (auto& g : gainSmooth) std::fill (g.begin(), g.end(), 1.0f);
    for (auto& ch : humNotches) for (auto& f : ch) f.reset();
}

void NoiseReductionEffect::setNoiseProfile (const std::vector<float>& powerPerBin)
{
    if (powerPerBin.size() != (size_t) numBins)
        return;
    fixedProfile = powerPerBin;
    for (auto& n : noise) n = powerPerBin;
    for (auto& m : minTrack) m = powerPerBin;
    hasFixedProfile = true;
}

std::vector<float> NoiseReductionEffect::getNoiseProfile() const { return noise[0]; }

void NoiseReductionEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sc)
{
    const int humMode = juce::jlimit (0, 2, (int) std::round (p (hum)));
    if (humMode != lastHum)
    {
        lastHum = humMode;
        const double base = humMode == 1 ? 50.0 : 60.0;
        for (auto& ch : humNotches)
            for (int h = 0; h < 8; ++h)
            {
                ch[(size_t) h].setCoefficients (Biquad::Type::Notch, sampleRate, base * (h + 1), 30.0);
                ch[(size_t) h].reset();
            }
    }

    if (humMode > 0)
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float x = d[i];
                for (auto& f : humNotches[(size_t) ch]) x = f.process (x);
                d[i] = x;
            }
        }

    StftEffect::process (buffer, sc);
}

void NoiseReductionEffect::processFrame (int ch, float* spec) noexcept
{
    auto& n = noise[(size_t) ch];
    auto& g = gainSmooth[(size_t) ch];
    auto& mt = minTrack[(size_t) ch];
    const bool learning = p (learn) > 0.5f;
    const bool adapt = p (adaptive) > 0.5f;

    // Leaving learn mode freezes what was heard as the reference profile.
    if (! learning && wasLearning[(size_t) ch])
    {
        fixedProfile = n;
        mt = n;
        hasFixedProfile.store (true, std::memory_order_relaxed);
    }
    wasLearning[(size_t) ch] = learning;
    const bool haveProfile = hasFixedProfile.load (std::memory_order_relaxed);
    const float floorGain = dbToGain (-p (amount));
    const float over = p (sensitivity);
    ++framesSeen;

    // Learning, or minimum statistics on the first ~0.5 s with no profile yet.
    for (int k = 0; k < numBins; ++k)
    {
        const float re = spec[2 * k], im = spec[2 * k + 1];
        const float power = re * re + im * im;

        if (learning)
            n[(size_t) k] = framesSeen < 4 ? power : 0.95f * n[(size_t) k] + 0.05f * power;
        else if (adapt)
        {
            // Minimum tracking with slow rise, so speech does not lift the estimate.
            mt[(size_t) k] = power < mt[(size_t) k] ? 0.7f * mt[(size_t) k] + 0.3f * power : mt[(size_t) k] * 1.002f + 1.0e-12f;
            float target = mt[(size_t) k] * 1.5f;
            // With a learned/room-check profile, only follow the noise down, or up by 3 dB at most:
            // sustained sung notes must never be mistaken for noise.
            if (haveProfile) target = std::min (target, fixedProfile[(size_t) k] * 2.0f);
            n[(size_t) k] = 0.98f * n[(size_t) k] + 0.02f * target;
        }
    }

    if (learning)
        return; // pass audio through untouched while learning

    // Spectral gain with temporal smoothing (fast attack, slower release to avoid warble).
    for (int k = 0; k < numBins; ++k)
    {
        const float re = spec[2 * k], im = spec[2 * k + 1];
        const float power = re * re + im * im + 1.0e-12f;
        float target = 1.0f - over * n[(size_t) k] / power;
        target = juce::jlimit (floorGain, 1.0f, target);
        auto& gs = g[(size_t) k];
        gs = target > gs ? 0.5f * gs + 0.5f * target : 0.8f * gs + 0.2f * target;
    }

    // Light smoothing across frequency.
    float prev = g[0];
    for (int k = 1; k < numBins - 1; ++k)
    {
        const float cur = g[(size_t) k];
        const float sm = 0.25f * prev + 0.5f * cur + 0.25f * g[(size_t) k + 1];
        prev = cur;
        spec[2 * k] *= sm;
        spec[2 * k + 1] *= sm;
    }
    spec[0] *= g[0]; spec[1] *= g[0];
    spec[2 * (numBins - 1)] *= g[(size_t) numBins - 1];
    spec[2 * (numBins - 1) + 1] *= g[(size_t) numBins - 1];
}

//==============================================================================
DeReverbEffect::DeReverbEffect()
{
    addParam ({ "amount", "Amount", 0, 1, 0.5f, "%", {}, false, "How much room echo to remove" });
    addParam ({ "roomDecay", "Room decay", 0.2f, 2.5f, 0.6f, "s", {}, false, "Roughly how long the room rings" });
    addParam ({ "floor", "Floor", -30, 0, -12, "dB", {}, false, "Limits the reduction so the voice stays natural" });
}

void DeReverbEffect::prepareSpectral()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        history[(size_t) ch].assign ((size_t) historyFrames * numBins, 0.0f);
        gainSmooth[(size_t) ch].assign (numBins, 1.0f);
    }
}

void DeReverbEffect::reset()
{
    StftEffect::reset();
    for (auto& h : history) std::fill (h.begin(), h.end(), 0.0f);
    for (auto& g : gainSmooth) std::fill (g.begin(), g.end(), 1.0f);
    historyPos.fill (0);
}

void DeReverbEffect::processFrame (int ch, float* spec) noexcept
{
    // Late reverb power at frame t is modelled as the power Td earlier, decayed
    // exponentially by the room's RT60 (Lebart's statistical model).
    const double hopSec = hopSize / sampleRate;
    const int delayFrames = juce::jlimit (1, historyFrames - 1, (int) std::round (0.05 / hopSec));
    const double rt = p (roomDecay);
    const float decay = (float) std::exp (-2.0 * (3.0 * std::log (10.0) / rt) * delayFrames * hopSec);
    const float amt = p (amount);
    const float floorGain = dbToGain (p (floorDb));

    auto& h = history[(size_t) ch];
    auto& g = gainSmooth[(size_t) ch];
    int& pos = historyPos[(size_t) ch];
    const int pastRow = (pos - delayFrames + historyFrames) % historyFrames;

    for (int k = 0; k < numBins; ++k)
    {
        const float re = spec[2 * k], im = spec[2 * k + 1];
        const float power = re * re + im * im + 1.0e-12f;
        const float late = decay * h[(size_t) (pastRow * numBins + k)];
        h[(size_t) (pos * numBins + k)] = power;

        float target = juce::jlimit (floorGain, 1.0f, 1.0f - amt * late / power);
        auto& gs = g[(size_t) k];
        gs = 0.6f * gs + 0.4f * target;
        spec[2 * k] *= gs;
        spec[2 * k + 1] *= gs;
    }
    pos = (pos + 1) % historyFrames;
}

} // namespace studio::dsp
