#pragma once

#include <atomic>
#include <memory>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MixGraph.h"
#include "Metronome.h"
#include "../dsp/BuiltinEffects.h"
#include "../mixing/MixTools.h"

namespace studio
{

/** A finished recording pass on one track. Loop recording yields one take per pass. */
struct RecordedTakes
{
    juce::String trackId;
    juce::File file;
    std::vector<Take> takes;
};

/** The real-time engine: owns the audio device, the transport and recording.

    The audio callback runs on a dedicated real-time thread. It never allocates,
    locks or waits on disk: the mix graph is swapped in through an atomic pointer,
    retired graphs are deleted on the message thread, and recorded audio goes to
    disk through JUCE's ThreadedWriter FIFO on a background thread. */
class AudioEngine : private juce::AudioIODeviceCallback,
                    private juce::MidiInputCallback,
                    private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /** Opens the default (or saved) device. Pass the saved XML state from Settings. */
    juce::String initialiseDevice (const juce::XmlElement* savedState = nullptr, int numInputs = 2, int numOutputs = 2);
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    double getSampleRate() const { return sampleRate.load(); }
    int getBlockSize() const { return blockSize.load(); }

    /** Estimated round-trip latency of the current device (ms). */
    double getRoundTripLatencyMs() const;

    // ---- project ---------------------------------------------------------------
    /** Rebuilds the graph from the project (message thread). Cheap edits should use the setters below. */
    void setProject (const Project& project, const juce::File& projectFolder);
    MixGraph* getGraph() const { return activeForUi.load(); }

    // ---- transport ---------------------------------------------------------------
    void play();
    void stop();
    bool isPlaying() const { return playing.load(); }
    void setPositionSamples (juce::int64 pos);
    void setPositionBeats (double beat);
    juce::int64 getPositionSamples() const { return position.load(); }
    double getPositionBeats() const;
    double getPositionSeconds() const { return (double) position.load() / sampleRate.load(); }

    void setLoop (bool enabled, double startBeat, double endBeat);
    bool isLooping() const { return loopOn.load(); }

    // ---- recording ---------------------------------------------------------------
    struct RecordSettings
    {
        int countInBars = 0;       // clicks only, then playback and recording start
        int preRollBars = 1;       // playback starts this many bars before the record position
        bool punch = false;        // record only between punch-in and punch-out
        double punchInBeat = 0, punchOutBeat = 0;
        bool loopRecord = false;   // record every pass of the loop as a take
    };

    /** Starts recording on every armed track at the current position. Returns an error message or "". */
    juce::String startRecording (const RecordSettings& settings, const Project& project, const juce::File& takesFolder);
    void stopRecording();
    bool isRecording() const { return recording.load(); }
    bool isCountingIn() const { return countInRemaining.load() > 0; }
    std::function<void (std::vector<RecordedTakes>)> onRecordingFinished;

    // ---- metronome, monitoring ---------------------------------------------------
    void setMetronome (bool on) { metronomeOn.store (on); }
    bool isMetronomeOn() const { return metronomeOn.load(); }
    void setMetronomeVolume (float v) { metronome.setVolume (v); }
    void setMonitorReverb (float amount) { monitorReverbAmount.store (amount); }

    /** Listening checks on the monitor output only (0 studio, 1 phone, 2 car, 3 earbuds, 4 mono). */
    void setListeningMode (int mode) { listening.setParam ("mode", (float) mode); }
    int getListeningMode() const { return (int) listening.getParam ("mode"); }

    /** Live input level per input channel (peak since last read, 0..1). */
    float getInputPeak (int channel);
    int getNumInputChannels() const { return numInputs.load(); }

    /** Captures raw input for N seconds (room check, input wizard) and calls back on the message thread. */
    void captureInput (double seconds, int firstChannel, int numChannels, std::function<void (juce::AudioBuffer<float>)> done);

    // ---- live playing ------------------------------------------------------------
    void setLiveTrack (const juce::String& trackId);
    void liveNoteOn (int note, float velocity);
    void liveNoteOff (int note);

    /** Dropout counter: callbacks that overran their time budget. */
    int getXruns() const { return xruns.load(); }
    double getCpuLoad() const { return cpuLoad.load(); }

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numIns, float* const* outputs, int numOuts,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;

    void processSegment (juce::AudioBuffer<float>& out, int offset, int num, juce::int64 pos, juce::AudioBuffer<float>& input);
    void writeRecording (const juce::AudioBuffer<float>& input, int offset, int num, juce::int64 pos);
    void finishRecording();

    juce::AudioDeviceManager deviceManager;
    std::atomic<double> sampleRate { 48000 };
    std::atomic<int> blockSize { 512 }, numInputs { 0 };

    // Graph handover: message thread -> audio thread through `pending`; the audio
    // thread retires the old one into a FIFO that the message thread drains.
    std::unique_ptr<MixGraph> owned;               // message-thread ownership of the newest graph
    std::atomic<MixGraph*> pending { nullptr };
    MixGraph* active = nullptr;                    // audio thread only
    std::atomic<MixGraph*> activeForUi { nullptr };
    std::array<MixGraph*, 16> retired {};
    juce::AbstractFifo retiredFifo { 16 };
    std::vector<std::unique_ptr<MixGraph>> graveyard; // graphs waiting to be confirmed unused
    Project projectSnapshot;
    juce::File projectFolder;

    // Transport
    std::atomic<bool> playing { false }, loopOn { false }, metronomeOn { false };
    std::atomic<juce::int64> position { 0 }, loopStart { 0 }, loopEnd { 0 };
    std::atomic<bool> locateRequested { false }, notesOffRequested { false };
    Metronome metronome;

    // Recording
    struct RecordTarget
    {
        juce::String trackId;
        int firstChannel = 0, channels = 1;
        juce::File file;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        juce::int64 fileStartTimeline = -1;        // timeline sample of file sample 0
        std::atomic<juce::int64> samplesWritten { 0 };
    };
    std::vector<std::unique_ptr<RecordTarget>> targets;
    juce::TimeSliceThread writerThread { "Recording writer" };
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> countInRemaining { 0 };
    juce::int64 countInPos = 0;
    RecordSettings recordSettings;
    juce::int64 recordFrom = 0, recordTo = std::numeric_limits<juce::int64>::max();
    std::array<juce::int64, 256> loopWrapFileIndex {};  // file sample index at each loop wrap
    std::atomic<int> loopWraps { 0 };
    double samplesPerBeatAtRecord = 24000;

    // Monitoring
    std::atomic<float> monitorReverbAmount { 0.0f };
    dsp::ReverbEffect cueReverb;
    dsp::ListeningCheckEffect listening;
    juce::AudioBuffer<float> inputBuffer, mixBuffer, cueBuffer;
    std::array<std::atomic<float>, 32> inputPeaks {};
    struct MonitorRoute { int first = 0, channels = 1; float pan = 0; };
    std::array<MonitorRoute, 16> monitorRoutes {};
    std::atomic<int> numMonitorRoutes { 0 };

    // Input capture (room check)
    std::atomic<bool> capturing { false };
    juce::AudioBuffer<float> captureBuffer;
    std::atomic<int> capturePos { 0 };
    int captureFirst = 0, captureChannels = 1;
    std::function<void (juce::AudioBuffer<float>)> captureDone;

    // Live MIDI
    juce::MidiMessageCollector midiCollector;
    juce::MidiBuffer midiBlock;
    std::atomic<int> liveTrackIndex { -1 };
    juce::String liveTrackId;

    std::atomic<int> xruns { 0 };
    std::atomic<double> cpuLoad { 0 };
};

} // namespace studio
