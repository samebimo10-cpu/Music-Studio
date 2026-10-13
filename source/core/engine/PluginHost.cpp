#include "PluginHost.h"

namespace studio
{

namespace
{
    /** Instrument adapter: queues note events into a MIDI buffer that the plugin
        receives on the next render call (events land at the render's start offset). */
    class PluginInstrument : public Instrument
    {
    public:
        PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> p, juce::String identifier)
            : plugin (std::move (p)), id (std::move (identifier)) {}

        juce::String getId() const override { return "plugin:" + id; }

        void prepare (double sampleRate, int maxBlockSize) override
        {
            plugin->setPlayConfigDetails (0, 2, sampleRate, maxBlockSize);
            plugin->prepareToPlay (sampleRate, maxBlockSize);
            work.setSize (juce::jmax (2, plugin->getTotalNumOutputChannels()), maxBlockSize);
            midi.ensureSize (4096);
        }

        void noteOn (int note, float velocity) override  { midi.addEvent (juce::MidiMessage::noteOn (1, note, juce::jlimit (0.0f, 1.0f, velocity)), 0); }
        void noteOff (int note) override                 { midi.addEvent (juce::MidiMessage::noteOff (1, note), 0); }
        void setSustain (bool down) override             { midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, down ? 127 : 0), 0); }
        void setPitchBend (float semis) override         { midi.addEvent (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, 8192 + (int) (semis / 2.0f * 8191))), 0); }
        void allNotesOff (bool) override                 { midi.addEvent (juce::MidiMessage::allNotesOff (1), 0); }

        void render (juce::AudioBuffer<float>& out, int start, int num) override
        {
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), num);
            view.clear();
            plugin->processBlock (view, midi);
            midi.clear();
            for (int ch = 0; ch < juce::jmin (2, out.getNumChannels()); ++ch)
                out.addFrom (ch, start, view, juce::jmin (ch, view.getNumChannels() - 1), 0, num);
        }

    private:
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::String id;
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> work;
    };
}

//==============================================================================
PluginHost& PluginHost::getInstance()
{
    static PluginHost host;
    return host;
}

PluginHost::PluginHost()
{
    formatManager.addDefaultFormats();
    if (auto xml = juce::XmlDocument::parse (listFile()))
        knownPlugins.recreateFromXml (*xml);
}

juce::File PluginHost::listFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Studio").getChildFile ("plugins.xml");
}

void PluginHost::scanAsync (std::function<void (int)> onDone, const juce::StringArray& extraPaths)
{
    if (scanning.exchange (true))
        return;

    scanPool.addJob ([this, onDone, extraPaths]
    {
        int found = 0;
        for (auto* format : formatManager.getFormats())
        {
            auto paths = format->getDefaultLocationsToSearch();
            for (auto& p : extraPaths) paths.add (juce::File (p));
            juce::PluginDirectoryScanner scanner (knownPlugins, *format, paths, true,
                                                  juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("studio_scan_dead"),
                                                  true);
            juce::String name;
            while (scanner.scanNextFile (true, name)) {}
        }
        found = knownPlugins.getNumTypes();
        if (auto xml = knownPlugins.createXml())
        {
            listFile().getParentDirectory().createDirectory();
            xml->writeTo (listFile());
        }
        scanning = false;
        juce::MessageManager::callAsync ([onDone, found] { if (onDone) onDone (found); });
    });
}

std::vector<juce::PluginDescription> PluginHost::effects() const
{
    std::vector<juce::PluginDescription> out;
    for (auto& d : knownPlugins.getTypes())
        if (! d.isInstrument) out.push_back (d);
    return out;
}

std::vector<juce::PluginDescription> PluginHost::instruments() const
{
    std::vector<juce::PluginDescription> out;
    for (auto& d : knownPlugins.getTypes())
        if (d.isInstrument) out.push_back (d);
    return out;
}

const juce::PluginDescription* PluginHost::findByIdentifier (const juce::String& identifier) const
{
    static thread_local juce::PluginDescription found;
    for (auto& d : knownPlugins.getTypes())
        if (d.createIdentifierString() == identifier || d.fileOrIdentifier == identifier)
        {
            found = d;
            return &found;
        }
    return nullptr;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::instantiate (const juce::String& identifier, double sr, int block, juce::String& error)
{
    auto* desc = findByIdentifier (identifier);
    if (desc == nullptr)
    {
        error = "Plugin not found: " + identifier;
        return nullptr;
    }
    return formatManager.createPluginInstance (*desc, sr, block, error);
}

std::unique_ptr<dsp::Effect> PluginHost::createEffect (const juce::String& identifier, const juce::String& stateBase64, double sr, int block)
{
    juce::String error;
    auto inst = instantiate (identifier, sr, block, error);
    if (inst == nullptr)
        return nullptr;

    if (stateBase64.isNotEmpty())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (stateBase64))
            inst->setStateInformation (mb.getData(), (int) mb.getSize());
    }
    return std::make_unique<PluginEffect> (std::move (inst));
}

std::unique_ptr<Instrument> PluginHost::createInstrument (const juce::String& identifier, double sr, int block)
{
    juce::String error;
    auto inst = instantiate (identifier, sr, block, error);
    if (inst == nullptr)
        return nullptr;
    return std::make_unique<PluginInstrument> (std::move (inst), identifier);
}

//==============================================================================
PluginEffect::PluginEffect (std::unique_ptr<juce::AudioPluginInstance> instance) : plugin (std::move (instance))
{
    for (auto* p : plugin->getParameters())
    {
        if ((int) exposed.size() >= maxParams) break;
        exposed.push_back (p);
        addParam ({ juce::String ((int) exposed.size() - 1), p->getName (32), 0, 1, p->getDefaultValue(), p->getLabel() });
    }
    for (size_t i = 0; i < exposed.size(); ++i)
    {
        setParamByIndex ((int) i, exposed[i]->getValue());
        lastSent[i] = exposed[i]->getValue();
    }
}

void PluginEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    plugin->setPlayConfigDetails (2, 2, sr, block);
    plugin->prepareToPlay (sr, block);
    work.setSize (juce::jmax (2, juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels())), block);
}

void PluginEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    // Push changed parameters (from automation or our UI) into the plugin.
    for (size_t i = 0; i < exposed.size(); ++i)
    {
        const float v = p ((int) i);
        if (v != lastSent[i]) { exposed[i]->setValue (v); lastSent[i] = v; }
    }

    const int n = buffer.getNumSamples();
    juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), n);
    view.clear();
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        view.copyFrom (ch, 0, buffer, ch, 0, n);
    plugin->processBlock (view, emptyMidi);
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        buffer.copyFrom (ch, 0, view, juce::jmin (ch, view.getNumChannels() - 1), 0, n);
}

juce::String PluginEffect::getStateBase64() const
{
    juce::MemoryBlock mb;
    plugin->getStateInformation (mb);
    return mb.toBase64Encoding();
}

} // namespace studio
