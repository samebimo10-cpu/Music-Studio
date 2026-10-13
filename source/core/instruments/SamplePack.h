#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include "Instrument.h"

namespace studio
{

/** One sample mapped to a key and velocity range. */
struct SampleZone
{
    juce::File file;
    int rootNote = 60, loNote = 0, hiNote = 127, loVel = 0, hiVel = 127;
    int roundRobin = 0;          // zones sharing ranges cycle through rr groups
    double fileSampleRate = 48000;
    juce::int64 lengthFrames = 0;
    int numChannels = 2;
    juce::AudioBuffer<float> preload; // first frames in RAM; the rest streams from disk
};

struct SampledInstrumentDef
{
    juce::String id, name, family, role;
    float attack = 0.002f, release = 0.3f;
    std::vector<std::shared_ptr<SampleZone>> zones;
};

struct SamplePackInfo
{
    juce::String id, name, version, description;
    juce::File folder;
    juce::int64 sizeBytes = 0;
    juce::StringArray instrumentIds;
    bool installed = false;
};

/** Background disk streamer shared by every sampler voice. A 30 GB pack only
    keeps each sample's first frames in RAM; voices then read from per-voice ring
    buffers that this thread keeps filled. */
class DiskStreamer : private juce::Thread
{
public:
    static constexpr int ringFrames = 1 << 15;   // ~0.7 s at 48 kHz per voice
    static constexpr int maxSlots = 128;

    struct Slot
    {
        enum State : int { Idle, Requested, Streaming, Stopping };
        std::atomic<int> state { Idle };
        std::atomic<SampleZone*> zone { nullptr };
        std::atomic<juce::int64> writeFrame { 0 };  // next frame index (in file frames) to be written
        std::atomic<juce::int64> readFrame { 0 };   // consumer position (file frames)
        juce::AudioBuffer<float> ring { 2, ringFrames };
        std::unique_ptr<juce::AudioFormatReader> reader; // streamer thread only
        SampleZone* readerZone = nullptr;
    };

    static DiskStreamer& getInstance();

    /** Audio thread: claims a slot and asks for streaming from the end of the preload. */
    int request (SampleZone* zone) noexcept;
    void release (int slot) noexcept;
    Slot& slot (int index) noexcept { return slots[(size_t) index]; }

    std::atomic<int> underruns { 0 };

private:
    DiskStreamer();
    ~DiskStreamer() override;
    void run() override;

    std::array<Slot, maxSlots> slots;
    juce::AudioFormatManager formats;
    juce::WaitableEvent wake;
};

/** Plays a SampledInstrumentDef with velocity layers, round-robins and streaming. */
class SamplerInstrument : public Instrument
{
public:
    explicit SamplerInstrument (std::shared_ptr<SampledInstrumentDef> def);
    ~SamplerInstrument() override;

    juce::String getId() const override { return "sampler:" + definition->id; }
    void prepare (double sampleRate, int maxBlockSize) override;
    void noteOn (int note, float velocity) override;
    void noteOff (int note) override;
    void allNotesOff (bool immediately) override;
    void setSustain (bool down) override;
    void render (juce::AudioBuffer<float>& out, int start, int num) override;
    int getActiveVoices() const override;

private:
    struct Voice
    {
        SampleZone* zone = nullptr;
        int note = -1;
        double pos = 0, inc = 1;
        float gain = 0, env = 0, envTarget = 0, envStep = 0;
        bool active = false, held = false, sustained = false;
        int streamSlot = -1;
        juce::uint32 age = 0;
    };

    float readFrame (Voice& v, int channel, juce::int64 frame) noexcept;

    std::shared_ptr<SampledInstrumentDef> definition;
    std::array<Voice, 48> voices;
    double sr = 48000;
    std::array<int, 128> rrCounter {};
    juce::uint32 ages = 0;
    bool sustainDown = false;
};

/** Finds, installs, downloads and loads sample packs. The packs folder can live
    on an external drive (Settings -> Storage). */
class SamplePackManager
{
public:
    static SamplePackManager& getInstance();

    void setPacksFolder (const juce::File& folder);
    juce::File getPacksFolder() const { return packsFolder; }
    static juce::File defaultPacksFolder();

    std::vector<SamplePackInfo> scanInstalled() const;

    /** Installs from a .zip or a folder (e.g. a USB drive), copying into the packs folder. */
    juce::Result installFromFile (const juce::File& zipOrFolder);
    juce::Result remove (const juce::String& packId);

    /** Loads (and caches) an instrument: "packId/instrumentId". */
    std::shared_ptr<SampledInstrumentDef> loadInstrument (const juce::String& spec);

    /** Frames kept in RAM per sample. Lower values use less memory but need faster disks. */
    void setPreloadFrames (int frames) { preloadFrames = juce::jlimit (4096, 1 << 18, frames); }
    int getPreloadFrames() const { return preloadFrames; }

    /** Bytes currently held in preload buffers (for the memory display). */
    juce::int64 preloadBytes() const;

    //==========================================================================
    /** Resumable download of a pack archive (HTTP Range), then install. Runs on a
        background thread; progress and completion come back on the message thread. */
    struct Download
    {
        juce::String packId;
        juce::URL url;
        juce::File partialFile;
        std::atomic<double> progress { 0 };
        std::atomic<bool> cancelled { false };
        std::function<void (double)> onProgress;
        std::function<void (juce::Result)> onFinished;
    };
    std::shared_ptr<Download> startDownload (const juce::String& packId, const juce::URL& archiveUrl,
                                             std::function<void (double)> onProgress,
                                             std::function<void (juce::Result)> onFinished);

private:
    SamplePackManager();
    std::shared_ptr<SampledInstrumentDef> parseInstrument (const juce::File& packFolder, const juce::var& inst);

    juce::File packsFolder;
    int preloadFrames = 16384;
    std::map<juce::String, std::shared_ptr<SampledInstrumentDef>> cache;
    mutable std::mutex cacheLock;
    juce::ThreadPool downloadPool { 1 };
};

/** Creates a sampler for "packId/instrumentId", or nullptr if not installed. */
std::unique_ptr<Instrument> createSamplerInstrument (const juce::String& spec);

} // namespace studio
