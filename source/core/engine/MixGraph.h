#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <vector>
#include "ClipCache.h"
#include "../dsp/Effect.h"
#include "../instruments/Instrument.h"
#include "../model/Project.h"
#include "../model/TempoMap.h"
#include "../analysis/LoudnessMeter.h"

namespace studio
{

class VocalChain;
class MasteringChain;

/** Peak meter values written by the audio thread, read by the UI. */
struct MeterValues
{
    std::atomic<float> peakL { 0 }, peakR { 0 }, rmsL { 0 }, rmsR { 0 };
    std::atomic<float> gainReductionDb { 0 };

    void update (const juce::AudioBuffer<float>& b, int numSamples) noexcept;
};

/** Options for building a graph (the live engine and offline renders differ). */
struct GraphOptions
{
    double sampleRate = 48000;
    int maxBlockSize = 512;
    bool includeMastering = true;
    juce::File projectFolder;   // relative audio paths resolve against this
    /** For stems, instrumental and a cappella renders: tracks for which this returns false are silent. */
    std::function<bool (const Track&)> trackFilter;
};

/** The processing network for one project snapshot.

    Built on the message thread from a Project, then handed to the audio thread.
    Effect and instrument instances are shared with the previous graph when track
    and slot ids match, so editing during playback does not cut reverb tails or
    reset compressors. process() never allocates, locks or touches the disk. */
class MixGraph
{
public:
    MixGraph (const Project& project, const GraphOptions& options, const MixGraph* previous = nullptr);
    ~MixGraph();

    /** Renders [timelineSample, timelineSample + numSamples) into output (stereo, cleared first).
        input may be null; it carries live input for monitoring armed tracks. */
    void process (juce::AudioBuffer<float>& output, juce::int64 timelineSample, int numSamples,
                  bool playing, const juce::AudioBuffer<float>* input) noexcept;

    /** Retriggers notes that span the current position (after a locate or graph swap). */
    void requestChase() noexcept { chasePending.store (true); }
    void allNotesOff() noexcept;

    // Live parameter changes that do not need a rebuild (message thread).
    void setTrackVolume (const juce::String& trackId, float db);
    void setTrackPan (const juce::String& trackId, float pan);
    void setTrackMute (const juce::String& trackId, bool mute);
    void setTrackSolo (const juce::String& trackId, bool solo);
    void setTrackMonitor (const juce::String& trackId, bool monitor);
    void setSendLevel (const juce::String& trackId, int sendIndex, float db);
    void setMasterVolume (float db) { masterGain.store (dsp::dbToGain (db)); }
    void setMasteringEnabled (bool e) { masteringOn.store (e); }

    dsp::Effect* findEffect (const juce::String& trackId, const juce::String& slotId) const;
    VocalChain* findVocalChain (const juce::String& trackId) const;
    MasteringChain* getMasteringChain() const { return mastering.get(); }
    Instrument* findInstrument (const juce::String& trackId) const;

    /** Live note input (on-screen keyboard, MIDI keyboard) for a MIDI track. Audio thread. */
    void liveNoteOn (const juce::String& trackId, int note, float velocity) noexcept;
    void liveNoteOff (const juce::String& trackId, int note) noexcept;
    int trackIndex (const juce::String& trackId) const;
    /** Queues a live event; note == sustainNote carries the sustain pedal (velocity > 0 = down). */
    void liveNote (int trackIdx, int note, float velocity, bool on) noexcept;
    static constexpr int sustainNote = -1;

    const MeterValues* trackMeter (const juce::String& trackId) const;
    const MeterValues& masterMeter() const noexcept { return masterMeters; }
    const LoudnessMeter& masterLoudness() const noexcept { return loudness; }
    LoudnessMeter& masterLoudness() noexcept { return loudness; }
    /** Asks the audio thread to restart loudness measurement (thread-safe). */
    void resetLoudness() noexcept { loudnessResetPending.store (true); }

    int getLatencySamples() const noexcept { return maxLatency; }
    double getSampleRate() const noexcept { return options.sampleRate; }
    const TempoMap& getTempoMap() const noexcept { return tempoMap; }
    juce::int64 getLengthSamples() const noexcept { return lengthSamples; }

private:
    struct ClipPlayback
    {
        SharedAudio audio;
        juce::int64 start = 0, length = 0, sourceOffset = 0;
        int fadeIn = 0, fadeOut = 0;
        float gain = 1;
    };

    struct NoteEvent
    {
        juce::int64 sample;
        int note;
        float velocity;   // 0 = note off
    };

    struct AutomationTarget
    {
        enum class Kind { Volume, Pan, Effect } kind = Kind::Volume;
        dsp::Effect* effect = nullptr;
        int paramIndex = -1;
        std::vector<std::pair<juce::int64, float>> points;
        float valueAt (juce::int64 s) const noexcept;
    };

    struct EffectRef
    {
        std::shared_ptr<dsp::Effect> effect;
        int sidechainTrack = -1;
    };

    struct Node
    {
        juce::String id;
        TrackType type = TrackType::Audio;
        TrackRole role = TrackRole::Unknown;
        bool included = true;
        int outputNode = -1;                     // -1 = master
        std::vector<std::pair<int, std::atomic<float>*>> sends; // bus node, gain
        std::vector<std::unique_ptr<std::atomic<float>>> sendGains;
        std::vector<bool> sendPreFader;
        std::shared_ptr<Instrument> instrument;
        std::shared_ptr<VocalChain> vocalChain;
        std::vector<EffectRef> effects;
        std::vector<juce::String> slotIdList;    // slot id for each entry in effects
        std::vector<ClipPlayback> clips;
        std::vector<NoteEvent> notes;            // sorted by sample
        struct Span { juce::int64 start, end; int pitch; float velocity; };
        std::vector<Span> spans;                 // for chasing notes that started before the play position
        juce::int64 readAhead = 0;               // latency compensation: render this far ahead
        int ownLatency = 0;
        std::vector<AutomationTarget> automation;
        size_t noteCursor = 0;
        juce::int64 lastBlockEnd = -1;
        std::atomic<float> volume { 1.0f }, pan { 0.0f };
        std::atomic<bool> mute { false }, solo { false }, monitor { false };
        bool armed = false;
        int inputChannel = 0, inputChannels = 1;
        float smoothedGainL = 1, smoothedGainR = 1;
        juce::AudioBuffer<float> buffer, preFader;
        MeterValues meters;
        bool feedsSoloed = false;
        int silentSamples = 0;                   // input silent this long: effect tails have died out

        // Live note FIFO (UI/MIDI keyboard -> audio thread)
        struct LiveNote { int note; float velocity; bool on; };
        std::array<LiveNote, 256> liveNotes {};
        juce::AbstractFifo liveFifo { 256 };
    };

    void renderNode (Node& n, juce::int64 pos, int num, bool playing, const juce::AudioBuffer<float>* input) noexcept;
    void renderMidi (Node& n, juce::int64 pos, int num, bool playing) noexcept;
    void renderAudioClips (Node& n, juce::int64 pos, int num) noexcept;
    void chase (Node& n, juce::int64 pos) noexcept;
    static void applyPanGain (Node& n, int num) noexcept;

    GraphOptions options;
    TempoMap tempoMap;
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<int> order;                      // processing order (sources, then buses by depth)
    std::map<juce::String, int> nodeIndex;
    std::shared_ptr<MasteringChain> mastering;
    std::atomic<bool> masteringOn { true };
    std::atomic<float> masterGain { 1.0f };
    juce::AudioBuffer<float> masterBuffer;
    MeterValues masterMeters;
    LoudnessMeter loudness;
    std::atomic<bool> chasePending { true };
    std::atomic<bool> loudnessResetPending { false };
    int maxLatency = 0;
    juce::int64 lengthSamples = 0;
};

} // namespace studio
