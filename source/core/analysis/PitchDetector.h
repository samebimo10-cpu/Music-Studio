#pragma once

#include <memory>
#include <vector>
#include <juce_dsp/juce_dsp.h>

namespace studio
{

/** YIN fundamental-frequency estimator with an FFT-based difference function,
    so it is cheap enough to run every few milliseconds inside an effect.
    Construct and prepare off the audio thread; detect() does not allocate. */
class PitchDetector
{
public:
    struct Result
    {
        float frequency = 0;     // Hz, 0 when unvoiced
        float confidence = 0;    // 0..1 (1 - YIN aperiodicity)
        bool voiced() const noexcept { return frequency > 0; }
    };

    /** windowSize must be a power of two. */
    void prepare (double sampleRate, int windowSize = 2048, float minHz = 60.0f, float maxHz = 1200.0f);

    /** Analyses windowSize samples. */
    Result detect (const float* samples) noexcept;

    int getWindowSize() const noexcept { return window; }
    void setThreshold (float t) noexcept { threshold = t; }

private:
    double sr = 48000;
    int window = 2048;
    int half = 1024;
    int minTau = 40, maxTau = 800;
    float threshold = 0.15f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<std::complex<float>> a, b, tmp;
    std::vector<float> diff, energyPrefix;
};

/** One analysed pitch frame. */
struct PitchFrame
{
    double timeSeconds = 0;
    float frequency = 0;
    float confidence = 0;
    float rmsDb = -120;
};

/** Offline pitch track of a mono signal (hop in samples). */
std::vector<PitchFrame> analysePitch (const float* samples, int numSamples, double sampleRate, int hop = 256);

/** A stable note found in a pitch track (voice-to-MIDI and the pitch note editor). */
struct DetectedNote
{
    double startSeconds = 0;
    double endSeconds = 0;
    float midiPitch = 60;     // median detected pitch, fractional
    float velocity = 0.8f;
};

std::vector<DetectedNote> segmentNotes (const std::vector<PitchFrame>& frames, double minNoteSeconds = 0.08);

} // namespace studio
