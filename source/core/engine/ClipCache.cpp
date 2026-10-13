#include "ClipCache.h"
#include "../dsp/TimeStretch.h"

namespace studio
{

ClipCache& ClipCache::getInstance()
{
    static ClipCache c;
    return c;
}

ClipCache::ClipCache()
{
    formatManager.registerBasicFormats();
}

bool ClipCache::readFile (const juce::File& file, juce::AudioBuffer<float>& out, double& fileSampleRate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;
    if (reader->lengthInSamples > (juce::int64) std::numeric_limits<int>::max())
        return false;

    out.setSize ((int) juce::jmin (2u, reader->numChannels), (int) reader->lengthInSamples);
    reader->read (&out, 0, (int) reader->lengthInSamples, 0, true, true);
    fileSampleRate = reader->sampleRate;
    return true;
}

SharedAudio ClipCache::get (const juce::File& file, double sampleRate, double stretchRatio, double pitchSemitones)
{
    const auto key = file.getFullPathName() + "|" + juce::String (sampleRate) + "|" + juce::String (stretchRatio, 4)
                   + "|" + juce::String (pitchSemitones, 3) + "|" + juce::String (file.getLastModificationTime().toMilliseconds());
    {
        std::lock_guard<std::mutex> l (lock);
        if (auto it = entries.find (key); it != entries.end())
            return it->second;
    }

    juce::AudioBuffer<float> raw;
    double fileRate = 0;
    if (! readFile (file, raw, fileRate))
        return nullptr;

    auto audio = std::make_shared<juce::AudioBuffer<float>> (dsp::timestretch::resample (raw, fileRate, sampleRate));
    if (std::abs (stretchRatio - 1.0) > 1.0e-4 || std::abs (pitchSemitones) > 1.0e-4)
        *audio = dsp::timestretch::process (*audio, stretchRatio, pitchSemitones, sampleRate);

    std::lock_guard<std::mutex> l (lock);
    entries[key] = audio;
    return audio;
}

void ClipCache::purgeUnused()
{
    std::lock_guard<std::mutex> l (lock);
    for (auto it = entries.begin(); it != entries.end();)
        it = it->second.use_count() <= 1 ? entries.erase (it) : std::next (it);
}

void ClipCache::clear()
{
    std::lock_guard<std::mutex> l (lock);
    entries.clear();
}

bool writeAudioFile (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate, int bitDepth,
                     const juce::StringPairArray& metadata)
{
    std::unique_ptr<juce::AudioFormat> format;
    const auto ext = file.getFileExtension().toLowerCase();
    if (ext == ".flac")                       format = std::make_unique<juce::FlacAudioFormat>();
    else if (ext == ".aif" || ext == ".aiff") format = std::make_unique<juce::AiffAudioFormat>();
    else                                      format = std::make_unique<juce::WavAudioFormat>();

    if (ext == ".flac" && bitDepth > 24) bitDepth = 24;
    if (ext == ".flac" && bitDepth < 16) bitDepth = 16;

    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
    if (os == nullptr)
        return false;

    std::unordered_map<juce::String, juce::String> meta;
    for (auto& k : metadata.getAllKeys())
        meta[k] = metadata[k];

    auto options = juce::AudioFormatWriterOptions()
                       .withSampleRate (sampleRate)
                       .withNumChannels (buffer.getNumChannels())
                       .withBitsPerSample (bitDepth)
                       .withMetadataValues (meta)
                       .withSampleFormat (bitDepth == 32 && ext == ".wav" ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                          : juce::AudioFormatWriterOptions::SampleFormat::automatic);
    auto writer = format->createWriterFor (os, options);
    if (writer == nullptr)
        return false;
    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

} // namespace studio
