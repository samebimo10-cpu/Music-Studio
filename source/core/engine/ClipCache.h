#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <juce_audio_formats/juce_audio_formats.h>

namespace studio
{

using SharedAudio = std::shared_ptr<const juce::AudioBuffer<float>>;

/** Decodes audio files once (at the engine sample rate, stretched/pitched if a clip
    asks for it) and shares them between graphs. Never used from the audio thread:
    graphs hold the shared pointers they need. */
class ClipCache
{
public:
    static ClipCache& getInstance();

    /** Loads (or returns cached) audio. Returns null if the file cannot be read. */
    SharedAudio get (const juce::File& file, double sampleRate, double stretchRatio = 1.0, double pitchSemitones = 0.0);

    /** Reads a file without caching (imports, analysis). */
    static bool readFile (const juce::File& file, juce::AudioBuffer<float>& out, double& fileSampleRate);

    /** Drops entries nobody else references. */
    void purgeUnused();
    void clear();

    juce::AudioFormatManager& formats() { return formatManager; }

private:
    ClipCache();
    juce::AudioFormatManager formatManager;
    std::map<juce::String, SharedAudio> entries;
    std::mutex lock;
};

/** Writes a float buffer to WAV/FLAC/AIFF with the given bit depth. */
bool writeAudioFile (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate, int bitDepth = 24,
                     const juce::StringPairArray& metadata = {});

} // namespace studio
