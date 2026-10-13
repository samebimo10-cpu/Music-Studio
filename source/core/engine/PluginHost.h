#pragma once

#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Effect.h"
#include "../instruments/Instrument.h"

namespace studio
{

/** Third-party plugin hosting (VST3 on Windows/macOS/Linux, AU on macOS).

    Plugins are scanned on a background thread and remembered between sessions.
    Instances are created on the message thread when a graph is built. */
class PluginHost
{
public:
    static PluginHost& getInstance();

    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    juce::KnownPluginList& getKnownPlugins() { return knownPlugins; }

    /** Scans the default plugin folders (and any extra paths) in the background. */
    void scanAsync (std::function<void (int numFound)> onDone, const juce::StringArray& extraPaths = {});
    bool isScanning() const { return scanning.load(); }

    std::vector<juce::PluginDescription> effects() const;
    std::vector<juce::PluginDescription> instruments() const;
    const juce::PluginDescription* findByIdentifier (const juce::String& fileOrIdentifier) const;

    /** Creates an effect wrapper around a plugin. stateBase64 restores the plugin's state. */
    std::unique_ptr<dsp::Effect> createEffect (const juce::String& identifier, const juce::String& stateBase64,
                                               double sampleRate, int blockSize);
    std::unique_ptr<Instrument> createInstrument (const juce::String& identifier, double sampleRate, int blockSize);

    /** Saved plugin list location. */
    static juce::File listFile();

private:
    PluginHost();
    std::unique_ptr<juce::AudioPluginInstance> instantiate (const juce::String& identifier, double sr, int block, juce::String& error);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    std::atomic<bool> scanning { false };
    juce::ThreadPool scanPool { 1 };
};

/** dsp::Effect adapter for a hosted plugin. Exposes up to 32 of its parameters. */
class PluginEffect : public dsp::Effect
{
public:
    explicit PluginEffect (std::unique_ptr<juce::AudioPluginInstance> instance);
    juce::String getType() const override { return "plugin"; }
    juce::String getDisplayName() const override { return plugin->getName(); }
    void prepare (double sr, int block, int ch) override;
    void reset() override { plugin->reset(); }
    void process (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    int getLatencySamples() const override { return plugin->getLatencySamples(); }

    juce::AudioPluginInstance& getPlugin() { return *plugin; }
    juce::String getStateBase64() const;

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::MidiBuffer emptyMidi;
    juce::AudioBuffer<float> work;
    std::vector<juce::AudioProcessorParameter*> exposed;
    std::array<float, maxParams> lastSent {};
};

} // namespace studio
