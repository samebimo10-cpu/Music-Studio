#pragma once

#include <vector>
#include "Project.h"

namespace studio
{

/** Converts between beats and seconds across tempo changes.
    Built once from the project; safe to read from any thread after construction. */
class TempoMap
{
public:
    TempoMap() = default;
    explicit TempoMap (const Project& p);
    TempoMap (double bpm, std::vector<TempoEvent> changes = {});

    double beatsToSeconds (double beat) const noexcept;
    double secondsToBeats (double seconds) const noexcept;
    double bpmAt (double beat) const noexcept;

    juce::int64 beatsToSamples (double beat, double sampleRate) const noexcept
    {
        return (juce::int64) std::llround (beatsToSeconds (beat) * sampleRate);
    }

    double samplesToBeats (juce::int64 samples, double sampleRate) const noexcept
    {
        return secondsToBeats ((double) samples / sampleRate);
    }

private:
    struct Segment
    {
        double startBeat;
        double startSeconds;
        double bpm;
    };
    std::vector<Segment> segments { { 0.0, 0.0, 120.0 } };
};

} // namespace studio
