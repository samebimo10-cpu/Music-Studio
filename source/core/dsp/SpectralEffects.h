#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Effect.h"

namespace studio::dsp
{

/** Short-time Fourier transform engine: Hann windows, 75 % overlap, overlap-add.
    Latency is one frame. Derived classes edit the spectrum in processFrame(). */
class StftEffect : public Effect
{
public:
    static constexpr int fftOrder = 10;
    static constexpr int frameSize = 1 << fftOrder;
    static constexpr int hopSize = frameSize / 4;
    static constexpr int numBins = frameSize / 2 + 1;

    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    int getLatencySamples() const override { return frameSize; }

protected:
    /** Called once per hop per channel with the complex spectrum (interleaved re/im, frameSize+2 floats). */
    virtual void processFrame (int channel, float* spectrum) noexcept = 0;
    virtual void prepareSpectral() {}

    double hopsPerSecond() const noexcept { return sampleRate / hopSize; }

private:
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window;
    struct ChannelState
    {
        std::vector<float> input, output, frame;
        int inputPos = 0, hopCounter = 0, outputPos = 0;
    };
    std::array<ChannelState, 2> state;
};

//==============================================================================
/** Spectral noise removal for hum, fans, generators and hiss.

    The noise profile is either captured (learn mode, or a room-check recording)
    or tracked continuously with minimum statistics. Gains are smoothed over
    time and frequency to avoid "musical noise". Hum removal adds notches at
    50 or 60 Hz and harmonics. This is the DSP fallback for the on-device ML
    denoiser slot (see docs/ARCHITECTURE.md). */
class NoiseReductionEffect : public StftEffect
{
public:
    NoiseReductionEffect();
    juce::String getType() const override { return "denoise"; }
    juce::String getDisplayName() const override { return "Noise removal"; }
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;

    /** Sets a fixed noise power profile (numBins values), e.g. from the room check. Message thread. */
    void setNoiseProfile (const std::vector<float>& powerPerBin);
    std::vector<float> getNoiseProfile() const;

protected:
    void processFrame (int channel, float* spectrum) noexcept override;
    void prepareSpectral() override;

private:
    enum { amount, sensitivity, learn, hum, adaptive };
    std::array<std::vector<float>, 2> noise, gainSmooth, minTrack;
    std::vector<float> fixedProfile;
    std::atomic<bool> hasFixedProfile { false };
    std::array<std::array<Biquad, 8>, 2> humNotches;
    int lastHum = -1;
    int framesSeen = 0;
    std::array<bool, 2> wasLearning {};
};

//==============================================================================
/** Reduces the late reverb of an untreated room (spectral late-reverb suppression). */
class DeReverbEffect : public StftEffect
{
public:
    DeReverbEffect();
    juce::String getType() const override { return "dereverb"; }
    juce::String getDisplayName() const override { return "De-reverb"; }
    void reset() override;

protected:
    void processFrame (int channel, float* spectrum) noexcept override;
    void prepareSpectral() override;

private:
    enum { amount, roomDecay, floorDb };
    static constexpr int historyFrames = 16;
    std::array<std::vector<float>, 2> history;   // historyFrames x numBins magnitudes
    std::array<std::vector<float>, 2> gainSmooth;
    std::array<int, 2> historyPos {};
};

} // namespace studio::dsp
