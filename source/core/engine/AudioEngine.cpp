#include "AudioEngine.h"

namespace studio
{

AudioEngine::AudioEngine()
{
    writerThread.startThread (juce::Thread::Priority::high);
    cueReverb.setParam ("type", 2);
    cueReverb.setParam ("decay", 2.2f);
    cueReverb.setParam ("mix", 1.0f);
    startTimerHz (20);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    if (recording.load()) stopRecording();
    deviceManager.removeAudioCallback (this);
    for (auto& d : juce::MidiInput::getAvailableDevices())
        deviceManager.removeMidiInputDeviceCallback (d.identifier, this);
    deviceManager.closeAudioDevice();
    writerThread.stopThread (2000);
}

juce::String AudioEngine::initialiseDevice (const juce::XmlElement* savedState, int ins, int outs)
{
    auto err = deviceManager.initialise (ins, outs, savedState, true);
    deviceManager.addAudioCallback (this);

    // Common MIDI keyboards and pads work without setup: listen to every input.
    for (auto& d : juce::MidiInput::getAvailableDevices())
    {
        deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
        deviceManager.addMidiInputDeviceCallback (d.identifier, this);
    }
    return err;
}

double AudioEngine::getRoundTripLatencyMs() const
{
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const double sr = dev->getCurrentSampleRate();
        const int samples = dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples() + 2 * dev->getCurrentBufferSizeSamples();
        return sr > 0 ? 1000.0 * samples / sr : 0.0;
    }
    return 0.0;
}

//==============================================================================
void AudioEngine::setProject (const Project& project, const juce::File& folder)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread() || juce::MessageManager::getInstanceWithoutCreating() == nullptr);
    projectSnapshot = project;
    projectFolder = folder;

    GraphOptions opts;
    opts.sampleRate = sampleRate.load();
    opts.maxBlockSize = juce::jmax (blockSize.load(), 512);
    opts.projectFolder = folder;

    auto* previous = graveyard.empty() ? nullptr : graveyard.back().get();
    auto graph = std::make_unique<MixGraph> (project, opts, previous);
    auto* raw = graph.get();
    graveyard.push_back (std::move (graph));

    const bool deviceRunning = deviceManager.getCurrentAudioDevice() != nullptr && deviceManager.getCurrentAudioDevice()->isPlaying();
    if (deviceRunning)
    {
        // If an earlier pending graph was never picked up, it simply gets replaced.
        pending.store (raw);
    }
    else
    {
        active = raw;
        activeForUi.store (raw);
        // No audio thread running: everything older than the newest graph can go.
        graveyard.erase (graveyard.begin(), graveyard.end() - 1);
    }

    // Monitoring routes for armed tracks with monitoring on.
    int count = 0;
    for (auto& t : project.tracks)
        if (t.type == TrackType::Audio && t.armed && t.monitorInput && count < (int) monitorRoutes.size())
            monitorRoutes[(size_t) count++] = { t.inputChannel, t.inputChannels, t.pan };
    numMonitorRoutes.store (count);

    if (liveTrackId.isNotEmpty())
        liveTrackIndex.store (raw->trackIndex (liveTrackId));
}

void AudioEngine::timerCallback()
{
    // Drain retired graphs and free them (they are no longer referenced by the audio thread).
    int s1, z1, s2, z2;
    retiredFifo.prepareToRead (retiredFifo.getNumReady(), s1, z1, s2, z2);
    auto drop = [this] (MixGraph* g)
    {
        for (auto it = graveyard.begin(); it != graveyard.end(); ++it)
            if (it->get() == g) { graveyard.erase (it); break; }
    };
    for (int i = 0; i < z1; ++i) drop (retired[(size_t) (s1 + i)]);
    for (int i = 0; i < z2; ++i) drop (retired[(size_t) (s2 + i)]);
    retiredFifo.finishedRead (z1 + z2);

    // Graphs that were superseded before the audio thread ever saw them.
    auto* act = activeForUi.load();
    auto* pend = pending.load();
    while (graveyard.size() > 2)
    {
        auto* g = graveyard.front().get();
        if (g == act || g == pend) break;
        graveyard.erase (graveyard.begin());
    }

    if (capturing.load() && capturePos.load() >= captureBuffer.getNumSamples())
    {
        capturing = false;
        if (captureDone) captureDone (captureBuffer);
        captureDone = nullptr;
    }

    ClipCache::getInstance().purgeUnused();
}

//==============================================================================
void AudioEngine::play()
{
    if (auto* g = activeForUi.load()) { g->requestChase(); }
    playing.store (true);
}

void AudioEngine::stop()
{
    if (recording.load()) stopRecording();
    playing.store (false);
    countInRemaining.store (0);
    notesOffRequested.store (true);
}

void AudioEngine::setPositionSamples (juce::int64 pos)
{
    position.store (juce::jmax ((juce::int64) 0, pos));
    locateRequested.store (true);
}

void AudioEngine::setPositionBeats (double beat)
{
    TempoMap tm (projectSnapshot);
    setPositionSamples (tm.beatsToSamples (beat, sampleRate.load()));
}

double AudioEngine::getPositionBeats() const
{
    if (auto* g = activeForUi.load())
        return g->getTempoMap().samplesToBeats (position.load(), sampleRate.load());
    return 0.0;
}

void AudioEngine::setLoop (bool enabled, double startBeat, double endBeat)
{
    TempoMap tm (projectSnapshot);
    loopStart.store (tm.beatsToSamples (startBeat, sampleRate.load()));
    loopEnd.store (tm.beatsToSamples (endBeat, sampleRate.load()));
    loopOn.store (enabled && endBeat > startBeat);
}

float AudioEngine::getInputPeak (int channel)
{
    if (channel < 0 || channel >= (int) inputPeaks.size()) return 0.0f;
    return inputPeaks[(size_t) channel].exchange (0.0f);
}

void AudioEngine::setLiveTrack (const juce::String& trackId)
{
    liveTrackId = trackId;
    if (auto* g = activeForUi.load()) liveTrackIndex.store (g->trackIndex (trackId));
}

void AudioEngine::liveNoteOn (int note, float velocity)
{
    midiCollector.addMessageToQueue (juce::MidiMessage::noteOn (1, note, velocity).withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001));
}

void AudioEngine::liveNoteOff (int note)
{
    midiCollector.addMessageToQueue (juce::MidiMessage::noteOff (1, note).withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001));
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    midiCollector.addMessageToQueue (m);
}

void AudioEngine::captureInput (double seconds, int firstChannel, int channels, std::function<void (juce::AudioBuffer<float>)> done)
{
    captureBuffer.setSize (juce::jmax (1, channels), juce::jmax (1, (int) (seconds * sampleRate.load())));
    captureBuffer.clear();
    captureFirst = firstChannel;
    captureChannels = channels;
    captureDone = std::move (done);
    capturePos.store (0);
    capturing.store (true);
}

//==============================================================================
juce::String AudioEngine::startRecording (const RecordSettings& settings, const Project& project, const juce::File& takesFolder)
{
    if (recording.load()) return "Already recording.";

    takesFolder.createDirectory();
    targets.clear();
    const double sr = sampleRate.load();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");

    for (auto& t : project.tracks)
    {
        if (t.type != TrackType::Audio || ! t.armed) continue;
        auto target = std::make_unique<RecordTarget>();
        target->trackId = t.id;
        target->firstChannel = t.inputChannel;
        target->channels = juce::jlimit (1, 2, t.inputChannels);
        target->file = takesFolder.getChildFile (juce::File::createLegalFileName (t.name) + "_" + stamp + ".wav").getNonexistentSibling();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (target->file.createOutputStream().release());
        if (os == nullptr) return "Cannot write to " + target->file.getFullPathName();
        auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (target->channels).withBitsPerSample (32)
                        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto writer = wav.createWriterFor (os, opts);
        if (writer == nullptr) return "Cannot create a WAV writer.";
        // 10 s FIFO between the audio thread and the disk thread.
        target->writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, (int) (sr * 10));
        targets.push_back (std::move (target));
    }
    if (targets.empty())
        return "Arm a track to record (the red button on the track).";

    recordSettings = settings;
    TempoMap tm (project);
    const auto here = position.load();
    const double hereBeat = tm.samplesToBeats (here, sr);
    const double beatsPerBar = project.timeSigNumerator;
    samplesPerBeatAtRecord = 60.0 / tm.bpmAt (hereBeat) * sr;

    recordFrom = settings.punch ? tm.beatsToSamples (settings.punchInBeat, sr) : here;
    recordTo = settings.punch ? tm.beatsToSamples (settings.punchOutBeat, sr) : std::numeric_limits<juce::int64>::max();
    loopWraps.store (0);

    if (settings.preRollBars > 0 && ! settings.loopRecord)
    {
        const double startBeat = juce::jmax (0.0, tm.samplesToBeats (recordFrom, sr) - settings.preRollBars * beatsPerBar);
        position.store (tm.beatsToSamples (startBeat, sr));
        locateRequested.store (true);
    }
    countInPos = 0;
    countInRemaining.store (settings.countInBars > 0 ? (juce::int64) (settings.countInBars * beatsPerBar * samplesPerBeatAtRecord) : 0);

    recording.store (true);
    if (auto* g = activeForUi.load()) g->requestChase();
    playing.store (true);
    return {};
}

void AudioEngine::stopRecording()
{
    if (! recording.exchange (false))
        return;
    finishRecording();
}

void AudioEngine::finishRecording()
{
    const double sr = sampleRate.load();
    TempoMap tm (projectSnapshot);
    std::vector<RecordedTakes> results;

    for (auto& t : targets)
    {
        t->writer.reset(); // flushes and closes the file
        const auto written = t->samplesWritten.load();
        if (written <= 0 || t->fileStartTimeline < 0)
        {
            t->file.deleteFile();
            continue;
        }

        RecordedTakes r;
        r.trackId = t->trackId;
        r.file = t->file;
        const auto now = juce::Time::getCurrentTime().toISO8601 (true);

        auto makeTake = [&] (juce::int64 fileOffset, juce::int64 timelineStart, juce::int64 lengthSamples)
        {
            Take take;
            take.id = makeId ("take");
            take.audioFile = t->file.getFullPathName();
            take.sourceOffsetSeconds = (double) fileOffset / sr;
            take.startBeat = tm.samplesToBeats (timelineStart, sr);
            take.lengthBeats = tm.samplesToBeats (timelineStart + lengthSamples, sr) - take.startBeat;
            take.recordedAt = now;
            r.takes.push_back (take);
        };

        const int wraps = juce::jmin (loopWraps.load(), (int) loopWrapFileIndex.size());
        if (recordSettings.loopRecord && wraps > 0)
        {
            // First (possibly partial) pass, then one take per full loop pass.
            const auto ls = loopStart.load(), le = loopEnd.load();
            juce::int64 prev = 0;
            juce::int64 tlStart = t->fileStartTimeline;
            for (int w = 0; w <= wraps; ++w)
            {
                const juce::int64 endIdx = w < wraps ? loopWrapFileIndex[(size_t) w] : written;
                if (endIdx - prev > sr * 0.25)
                    makeTake (prev, tlStart, endIdx - prev);
                prev = endIdx;
                tlStart = ls;
                juce::ignoreUnused (le);
            }
        }
        else
            makeTake (0, t->fileStartTimeline, written);

        results.push_back (std::move (r));
    }
    targets.clear();

    if (onRecordingFinished)
        onRecordingFinished (std::move (results));
}

void AudioEngine::writeRecording (const juce::AudioBuffer<float>& input, int offset, int num, juce::int64 pos)
{
    // Only the part of this segment inside [recordFrom, recordTo).
    const juce::int64 a = juce::jmax (pos, recordFrom);
    const juce::int64 b = juce::jmin (pos + num, recordTo);
    if (b <= a) return;
    const int from = offset + (int) (a - pos);
    const int len = (int) (b - a);

    for (auto& t : targets)
    {
        if (t->writer == nullptr) continue;
        if (t->fileStartTimeline < 0) t->fileStartTimeline = a;
        const float* chans[2];
        for (int c = 0; c < t->channels; ++c)
        {
            const int ch = juce::jlimit (0, input.getNumChannels() - 1, t->firstChannel + c);
            chans[c] = input.getReadPointer (ch, from);
        }
        if (t->writer->write (chans, len))
            t->samplesWritten.fetch_add (len);
    }
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double newRate = device->getCurrentSampleRate();
    const int newBlock = device->getCurrentBufferSizeSamples();
    const bool changed = std::abs (newRate - sampleRate.load()) > 0.5 || newBlock > blockSize.load();
    sampleRate.store (newRate);
    blockSize.store (newBlock);

    const int ins = device->getActiveInputChannels().countNumberOfSetBits();
    numInputs.store (ins);
    inputBuffer.setSize (juce::jmax (2, device->getInputChannelNames().size()), juce::jmax (newBlock, 512));
    mixBuffer.setSize (2, juce::jmax (newBlock, 512));
    cueBuffer.setSize (2, juce::jmax (newBlock, 512));
    cueReverb.prepare (newRate, juce::jmax (newBlock, 512), 2);
    listening.prepare (newRate, juce::jmax (newBlock, 512), 2);
    metronome.prepare (newRate);
    midiCollector.reset (newRate);
    midiBlock.ensureSize (2048);

    // A new rate or larger block needs a new graph; rebuild on the message thread.
    if (changed)
        juce::MessageManager::callAsync ([this] { setProject (projectSnapshot, projectFolder); });
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::processSegment (juce::AudioBuffer<float>& out, int offset, int num, juce::int64 pos, juce::AudioBuffer<float>& input)
{
    juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, offset, num);
    if (active != nullptr)
        active->process (view, pos, num, true, nullptr);
    else
        view.clear();

    if (metronomeOn.load() && active != nullptr)
        metronome.render (out, offset, num, pos, active->getTempoMap(), projectSnapshot.timeSigNumerator);

    if (recording.load())
    {
        juce::AudioBuffer<float> inView (input.getArrayOfWritePointers(), input.getNumChannels(), offset, num);
        writeRecording (inView, 0, num, pos);
    }
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numIns, float* const* outputs, int numOuts,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const auto startTicks = juce::Time::getHighResolutionTicks();

    // Pick up a new graph.
    if (auto* p = pending.exchange (nullptr))
    {
        if (active != nullptr)
        {
            int s1, z1, s2, z2;
            retiredFifo.prepareToWrite (1, s1, z1, s2, z2);
            if (z1 > 0) retired[(size_t) s1] = active;
            else if (z2 > 0) retired[(size_t) s2] = active;
            retiredFifo.finishedWrite (z1 + z2);
        }
        active = p;
        activeForUi.store (p);
        active->allNotesOff();
        active->requestChase();
    }

    numSamples = juce::jmin (numSamples, inputBuffer.getNumSamples());
    const int ins = juce::jmin (numIns, inputBuffer.getNumChannels());
    for (int ch = 0; ch < inputBuffer.getNumChannels(); ++ch)
    {
        if (ch < ins && inputs[ch] != nullptr)
        {
            inputBuffer.copyFrom (ch, 0, inputs[ch], numSamples);
            if (ch < (int) inputPeaks.size())
            {
                const float pk = inputBuffer.getMagnitude (ch, 0, numSamples);
                if (pk > inputPeaks[(size_t) ch].load (std::memory_order_relaxed))
                    inputPeaks[(size_t) ch].store (pk, std::memory_order_relaxed);
            }
        }
        else
            inputBuffer.clear (ch, 0, numSamples);
    }

    // Input capture (room check).
    if (capturing.load())
    {
        const int cp = capturePos.load();
        const int n = juce::jmin (numSamples, captureBuffer.getNumSamples() - cp);
        for (int c = 0; c < captureBuffer.getNumChannels() && n > 0; ++c)
            captureBuffer.copyFrom (c, cp, inputBuffer, juce::jlimit (0, inputBuffer.getNumChannels() - 1, captureFirst + c), 0, n);
        capturePos.store (cp + juce::jmax (0, n));
    }

    // Live MIDI to the selected track.
    midiBlock.clear();
    midiCollector.removeNextBlockOfMessages (midiBlock, numSamples);
    if (active != nullptr && liveTrackIndex.load() >= 0)
        for (const auto meta : midiBlock)
        {
            auto m = meta.getMessage();
            if (m.isNoteOn()) active->liveNote (liveTrackIndex.load(), m.getNoteNumber(), m.getFloatVelocity(), true);
            else if (m.isNoteOff()) active->liveNote (liveTrackIndex.load(), m.getNoteNumber(), 0.0f, false);
            else if (m.isSustainPedalOn() || m.isSustainPedalOff())
                active->liveNote (liveTrackIndex.load(), MixGraph::sustainNote, m.isSustainPedalOn() ? 1.0f : 0.0f, true);
        }

    juce::AudioBuffer<float> out (mixBuffer.getArrayOfWritePointers(), 2, numSamples);
    out.clear();

    if (notesOffRequested.exchange (false) && active != nullptr)
        active->allNotesOff();

    if (locateRequested.exchange (false) && active != nullptr)
    {
        active->allNotesOff();
        active->requestChase();
    }

    if (playing.load())
    {
        // Count-in: clicks only, the song and the recording wait.
        auto ci = countInRemaining.load();
        int done = 0;
        if (ci > 0)
        {
            const int n = (int) juce::jmin ((juce::int64) numSamples, ci);
            metronome.renderCountIn (out, 0, n, countInPos, samplesPerBeatAtRecord, projectSnapshot.timeSigNumerator);
            countInPos += n;
            countInRemaining.store (ci - n);
            done = n;
            if (ci - n == 0 && active != nullptr) active->requestChase();
        }

        auto pos = position.load();
        while (done < numSamples)
        {
            int n = numSamples - done;
            const bool looping = loopOn.load() && loopEnd.load() > loopStart.load();
            if (looping && pos < loopEnd.load())
                n = (int) juce::jmin ((juce::int64) n, loopEnd.load() - pos);

            processSegment (out, done, n, pos, inputBuffer);
            pos += n;
            done += n;

            if (looping && pos >= loopEnd.load())
            {
                // Record where the file was when the loop wrapped (one take per pass).
                if (recording.load() && ! targets.empty())
                {
                    const int w = loopWraps.load();
                    if (w < (int) loopWrapFileIndex.size())
                    {
                        loopWrapFileIndex[(size_t) w] = targets.front()->samplesWritten.load();
                        loopWraps.store (w + 1);
                    }
                }
                pos = loopStart.load();
                if (active != nullptr) { active->allNotesOff(); active->requestChase(); }
            }
        }
        position.store (pos);
    }
    else if (active != nullptr)
    {
        // Stopped: still run the graph for live playing and effect tails.
        active->process (out, position.load(), numSamples, false, nullptr);
    }

    listening.process (out, nullptr);

    // Low-latency direct monitoring of armed inputs, with optional cue reverb.
    const int routes = numMonitorRoutes.load();
    if (routes > 0)
    {
        cueBuffer.clear (0, numSamples);
        for (int r = 0; r < routes; ++r)
        {
            auto& route = monitorRoutes[(size_t) r];
            const int c0 = juce::jlimit (0, inputBuffer.getNumChannels() - 1, route.first);
            const int c1 = route.channels > 1 ? juce::jmin (c0 + 1, inputBuffer.getNumChannels() - 1) : c0;
            const float gl = juce::jmin (1.0f, 1.0f - route.pan), gr = juce::jmin (1.0f, 1.0f + route.pan);
            out.addFrom (0, 0, inputBuffer, c0, 0, numSamples, gl);
            out.addFrom (1, 0, inputBuffer, c1, 0, numSamples, gr);
            cueBuffer.addFrom (0, 0, inputBuffer, c0, 0, numSamples, gl);
            cueBuffer.addFrom (1, 0, inputBuffer, c1, 0, numSamples, gr);
        }
        const float amt = monitorReverbAmount.load();
        if (amt > 0.001f)
        {
            juce::AudioBuffer<float> cue (cueBuffer.getArrayOfWritePointers(), 2, numSamples);
            cueReverb.process (cue, nullptr);
            // The reverb output still holds half the dry signal at mix = 1; the dry path is already in.
            for (int ch = 0; ch < 2; ++ch)
                out.addFrom (ch, 0, cue, ch, 0, numSamples, amt);
        }
    }

    for (int ch = 0; ch < numOuts; ++ch)
        if (outputs[ch] != nullptr)
        {
            if (ch < 2) juce::FloatVectorOperations::copy (outputs[ch], out.getReadPointer (ch), numSamples);
            else juce::FloatVectorOperations::clear (outputs[ch], numSamples);
        }

    // CPU load and overruns (callback took longer than its real-time budget).
    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const double budget = numSamples / sampleRate.load();
    const double load = elapsed / budget;
    cpuLoad.store (0.9 * cpuLoad.load() + 0.1 * load);
    if (load > 1.0) xruns.fetch_add (1);
}

} // namespace studio
