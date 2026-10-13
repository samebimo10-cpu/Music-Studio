#pragma once

#include <functional>
#include "../analysis/AudioAnalysis.h"
#include "../analysis/LoudnessMeter.h"
#include "../dsp/Effect.h"
#include "../model/Project.h"

namespace studio::mixing
{

/** Per-track analysis used by auto-mix and the mix assistant. */
struct TrackAnalysis
{
    juce::String trackId;
    TrackRole role = TrackRole::Unknown;
    analysis::SpectrumProfile spectrum;
    float activeRmsDb = -120;   // level while the part is playing
};

/** Guesses a track's role from its name, instrument and content when not set. */
TrackRole guessRole (const Track& t);

/** Renders and analyses every track over the busiest part of the song (up to `maxSeconds`). */
std::vector<TrackAnalysis> analyseTracks (const Project& p, const juce::File& projectFolder, double sampleRate = 44100, double maxSeconds = 30);

/** Auto-mix: sets levels, panning and EQ from each track's role and analysis. */
struct AutoMixReport { juce::StringArray changes; };
AutoMixReport autoMix (Project& p, const std::vector<TrackAnalysis>& analyses);

/** A problem the mix assistant found, in plain words, with a one-click fix. */
struct MixIssue
{
    juce::String id;
    juce::String title;      // "Vocal buried in the chorus"
    juce::String detail;     // what was measured and what the fix does
    std::function<void (Project&)> fix;
};

std::vector<MixIssue> assistMix (const Project& p, const juce::AudioBuffer<float>& mixBuffer, double sampleRate,
                                 const std::vector<TrackAnalysis>& tracks);

/** Reference track comparison: tone and loudness differences in plain words. */
struct ReferenceComparison
{
    std::array<float, analysis::SpectrumProfile::numBands> bandDifferenceDb {}; // mix minus reference (tilt-normalised)
    float loudnessDifferenceLu = 0;   // mix minus reference
    float widthDifference = 0;
    float matchGainDb = 0;            // gain for the reference so A/B is at equal loudness
    juce::StringArray notes;
};
ReferenceComparison compareToReference (const juce::AudioBuffer<float>& mix, const juce::AudioBuffer<float>& reference, double sampleRate);

/** Adds or replaces a built-in effect on a track with the given parameters; returns the slot id. */
juce::String setEffect (Track& t, const juce::String& type, const std::map<juce::String, float>& params, const juce::String& tag = {});

/** Finds or creates a bus by name (e.g. "Reverb", "Drums"). */
Track& ensureBus (Project& p, const juce::String& name, TrackRole role = TrackRole::Unknown);

} // namespace studio::mixing

namespace studio::dsp
{

/** Listening checks: phone speaker, car, earbuds and mono. Monitoring only, never exported. */
class ListeningCheckEffect : public Effect
{
public:
    enum Mode { Off, Phone, Car, Earbuds, Mono };
    ListeningCheckEffect();
    juce::String getType() const override { return "listening"; }
    juce::String getDisplayName() const override { return "Listening check"; }
    void prepare (double sr, int block, int ch) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    static juce::StringArray modeNames();

private:
    std::array<std::array<Biquad, 4>, 2> filters;
    int lastMode = -1;
    float env = 0;
};

} // namespace studio::dsp
