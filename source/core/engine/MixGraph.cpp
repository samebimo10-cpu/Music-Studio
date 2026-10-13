#include "MixGraph.h"
#include "../instruments/InstrumentLibrary.h"
#include "../vocal/VocalChain.h"
#include "../mastering/MasteringChain.h"
#include "PluginHost.h"
#include <algorithm>

namespace studio
{

//==============================================================================
void MeterValues::update (const juce::AudioBuffer<float>& b, int numSamples) noexcept
{
    const int n = juce::jmin (numSamples, b.getNumSamples());
    if (n <= 0) return;
    const float pl = b.getMagnitude (0, 0, n);
    const float pr = b.getNumChannels() > 1 ? b.getMagnitude (1, 0, n) : pl;
    const float rl = b.getRMSLevel (0, 0, n);
    const float rr = b.getNumChannels() > 1 ? b.getRMSLevel (1, 0, n) : rl;
    // Peak hold with decay is done in the UI; here we keep the max since the last read.
    peakL.store (std::max (peakL.load (std::memory_order_relaxed), pl), std::memory_order_relaxed);
    peakR.store (std::max (peakR.load (std::memory_order_relaxed), pr), std::memory_order_relaxed);
    rmsL.store (rl, std::memory_order_relaxed);
    rmsR.store (rr, std::memory_order_relaxed);
}

float MixGraph::AutomationTarget::valueAt (juce::int64 s) const noexcept
{
    if (points.empty()) return 0;
    if (s <= points.front().first) return points.front().second;
    if (s >= points.back().first) return points.back().second;
    auto it = std::upper_bound (points.begin(), points.end(), s, [] (juce::int64 v, const auto& p) { return v < p.first; });
    auto& b = *it;
    auto& a = *(it - 1);
    const double t = (double) (s - a.first) / (double) juce::jmax ((juce::int64) 1, b.first - a.first);
    return (float) (a.second + (b.second - a.second) * t);
}

//==============================================================================
MixGraph::MixGraph (const Project& project, const GraphOptions& opts, const MixGraph* previous)
    : options (opts), tempoMap (project)
{
    const double sr = options.sampleRate;
    const int block = options.maxBlockSize;

    auto prevNode = [previous] (const juce::String& id) -> const Node*
    {
        if (previous == nullptr) return nullptr;
        if (auto it = previous->nodeIndex.find (id); it != previous->nodeIndex.end())
            return previous->nodes[(size_t) it->second].get();
        return nullptr;
    };

    for (auto& t : project.tracks)
    {
        auto n = std::make_unique<Node>();
        n->id = t.id;
        n->type = t.type;
        n->role = t.role;
        n->included = ! options.trackFilter || options.trackFilter (t);
        n->volume.store (dsp::dbToGain (t.volumeDb));
        n->pan.store (t.pan);
        n->mute.store (t.mute);
        n->solo.store (t.solo);
        n->monitor.store (t.monitorInput);
        n->armed = t.armed;
        n->inputChannel = t.inputChannel;
        n->inputChannels = t.inputChannels;
        n->buffer.setSize (2, block);
        n->preFader.setSize (2, block);
        const Node* prev = prevNode (t.id);

        // Instrument
        if (t.type == TrackType::Midi)
        {
            const auto instId = t.instrument.id.isEmpty() ? juce::String ("grand_piano") : t.instrument.id;
            if (prev != nullptr && prev->instrument != nullptr && prev->instrument->getId() == instId)
                n->instrument = prev->instrument;
            else
            {
                std::unique_ptr<Instrument> inst;
                if (instId.startsWith ("plugin:"))
                    inst = PluginHost::getInstance().createInstrument (instId.fromFirstOccurrenceOf ("plugin:", false, false), sr, block);
                if (inst == nullptr)
                    inst = instruments::create (instId);
                inst->prepare (sr, block);
                n->instrument = std::move (inst);
            }
            for (auto& [k, v] : t.instrument.params)
                n->instrument->setParameter (k, v);
        }

        // Vocal chain (first in the insert path)
        if (t.vocalChain.enabled)
        {
            if (prev != nullptr && prev->vocalChain != nullptr)
                n->vocalChain = prev->vocalChain;
            else
            {
                n->vocalChain = std::make_shared<VocalChain>();
                n->vocalChain->prepare (sr, block, 2);
            }
            n->vocalChain->applySettings (t.vocalChain, project.keyAt (0));
        }

        // Insert effects
        for (auto& slot : t.effects)
        {
            // Reuse the previous instance with the same slot id and type.
            std::shared_ptr<dsp::Effect> fx;
            if (prev != nullptr)
                for (size_t i = 0; i < prev->effects.size(); ++i)
                    if (prev->slotIdList[i] == slot.id && prev->effects[i].effect != nullptr
                        && prev->effects[i].effect->getType() == slot.type)
                    {
                        fx = prev->effects[i].effect;
                        break;
                    }

            if (fx == nullptr)
            {
                std::unique_ptr<dsp::Effect> created;
                if (slot.type == "plugin")
                    created = PluginHost::getInstance().createEffect (slot.pluginIdentifier, slot.pluginState, sr, block);
                else
                    created = dsp::createEffect (slot.type);
                if (created == nullptr) continue;
                created->prepare (sr, block, 2);
                fx = std::move (created);
            }

            for (auto& [k, v] : slot.params) fx->setParam (k, v);
            fx->setBypassed (slot.bypassed);
            if (auto* d = dynamic_cast<dsp::DelayEffect*> (fx.get()))
                d->setTempo (project.bpm);
            n->effects.push_back ({ fx, -1 });
            n->slotIdList.push_back (slot.id);
        }

        // Audio clips
        if (t.type == TrackType::Audio)
        {
            for (auto& c : t.clips)
            {
                if (c.isMidi || c.audioFile.isEmpty()) continue;
                const juce::File f = juce::File::isAbsolutePath (c.audioFile) ? juce::File (c.audioFile)
                                                                             : options.projectFolder.getChildFile (c.audioFile);
                auto audio = ClipCache::getInstance().get (f, sr, c.stretchRatio, c.pitchSemitones);
                if (audio == nullptr) continue;
                ClipPlayback cp;
                cp.audio = audio;
                cp.start = tempoMap.beatsToSamples (c.startBeat, sr);
                cp.length = tempoMap.beatsToSamples (c.endBeat(), sr) - cp.start;
                cp.sourceOffset = (juce::int64) std::llround (c.sourceOffsetSeconds * sr);
                cp.fadeIn = (int) (tempoMap.beatsToSamples (c.startBeat + c.fadeInBeats, sr) - cp.start);
                cp.fadeOut = (int) (cp.start + cp.length - tempoMap.beatsToSamples (c.endBeat() - c.fadeOutBeats, sr));
                cp.gain = dsp::dbToGain (c.gainDb);
                n->clips.push_back (cp);
                lengthSamples = std::max (lengthSamples, cp.start + cp.length);
            }
        }

        // MIDI notes
        if (t.type == TrackType::Midi)
        {
            for (auto& c : t.clips)
            {
                if (! c.isMidi) continue;
                for (auto& note : c.notes)
                {
                    if (note.startBeat >= c.lengthBeats || note.startBeat < 0) continue;
                    const double s = c.startBeat + note.startBeat;
                    const double e = c.startBeat + std::min (note.startBeat + note.lengthBeats, c.lengthBeats);
                    const auto ss = tempoMap.beatsToSamples (s, sr);
                    const auto es = juce::jmax (ss + 1, tempoMap.beatsToSamples (e, sr));
                    n->notes.push_back ({ ss, note.pitch, juce::jmax (0.01f, note.velocity) });
                    n->notes.push_back ({ es, note.pitch, 0.0f });
                    n->spans.push_back ({ ss, es, note.pitch, note.velocity });
                    lengthSamples = std::max (lengthSamples, es);
                }
            }
            std::sort (n->notes.begin(), n->notes.end(), [] (const NoteEvent& a, const NoteEvent& b)
            {
                if (a.sample != b.sample) return a.sample < b.sample;
                return a.velocity < b.velocity; // offs first, so retriggers work
            });
        }

        nodeIndex[t.id] = (int) nodes.size();
        nodes.push_back (std::move (n));
    }

    // Routing, sends, sidechains and automation need every node to exist.
    for (size_t i = 0; i < project.tracks.size(); ++i)
    {
        auto& t = project.tracks[i];
        auto& n = *nodes[i];

        if (t.outputBusId.isNotEmpty())
            if (auto it = nodeIndex.find (t.outputBusId); it != nodeIndex.end() && it->second != (int) i
                && nodes[(size_t) it->second]->type == TrackType::Bus)
                n.outputNode = it->second;

        for (auto& s : t.sends)
        {
            auto it = nodeIndex.find (s.busId);
            if (it == nodeIndex.end() || it->second == (int) i || nodes[(size_t) it->second]->type != TrackType::Bus) continue;
            n.sendGains.push_back (std::make_unique<std::atomic<float>> (dsp::dbToGain (s.levelDb)));
            n.sends.push_back ({ it->second, n.sendGains.back().get() });
            n.sendPreFader.push_back (s.preFader);
        }

        size_t fxIndex = 0;
        for (auto& slot : t.effects)
        {
            if (fxIndex >= n.effects.size()) break;
            if (n.slotIdList[fxIndex] != slot.id) continue;
            if (slot.sidechainTrackId.isNotEmpty())
                if (auto it = nodeIndex.find (slot.sidechainTrackId); it != nodeIndex.end() && it->second != (int) i)
                    n.effects[fxIndex].sidechainTrack = it->second;
            ++fxIndex;
        }

        for (auto& lane : t.automation)
        {
            if (! lane.enabled || lane.points.empty()) continue;
            AutomationTarget target;
            if (lane.parameter == "volume")      target.kind = AutomationTarget::Kind::Volume;
            else if (lane.parameter == "pan")    target.kind = AutomationTarget::Kind::Pan;
            else if (lane.parameter.startsWith ("fx/"))
            {
                auto parts = juce::StringArray::fromTokens (lane.parameter, "/", "");
                if (parts.size() != 3) continue;
                target.kind = AutomationTarget::Kind::Effect;
                target.effect = findEffect (t.id, parts[1]);
                if (target.effect == nullptr) continue;
                target.paramIndex = target.effect->indexOf (parts[2]);
                if (target.paramIndex < 0) continue;
            }
            else continue;
            for (auto& p : lane.points)
                target.points.push_back ({ tempoMap.beatsToSamples (p.beat, sr), p.value });
            n.automation.push_back (std::move (target));
        }
    }

    // Break bus feedback loops (a bus that eventually routes to itself goes to master).
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        int hops = 0;
        for (int cur = nodes[i]->outputNode; cur >= 0 && hops <= (int) nodes.size(); cur = nodes[(size_t) cur]->outputNode)
            if (cur == (int) i || ++hops > (int) nodes.size()) { nodes[i]->outputNode = -1; break; }
    }

    // Processing order: sidechain sources first, other sources, then buses deepest-first.
    auto depth = [this] (int idx)
    {
        int d = 0;
        for (int cur = nodes[(size_t) idx]->outputNode; cur >= 0 && d < 64; cur = nodes[(size_t) cur]->outputNode) ++d;
        return d;
    };
    std::vector<bool> isSidechainSource (nodes.size(), false);
    for (auto& n : nodes)
        for (auto& e : n->effects)
            if (e.sidechainTrack >= 0) isSidechainSource[(size_t) e.sidechainTrack] = true;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i]->type != TrackType::Bus && isSidechainSource[i]) order.push_back ((int) i);
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i]->type != TrackType::Bus && ! isSidechainSource[i]) order.push_back ((int) i);
    std::vector<int> buses;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i]->type == TrackType::Bus) buses.push_back ((int) i);
    std::stable_sort (buses.begin(), buses.end(), [&] (int a, int b) { return depth (a) > depth (b); });
    order.insert (order.end(), buses.begin(), buses.end());

    // Latency compensation: each node renders ahead by its own plus its downstream latency.
    for (auto& n : nodes)
    {
        n->ownLatency = n->vocalChain != nullptr ? n->vocalChain->getLatencySamples() : 0;
        for (auto& e : n->effects) n->ownLatency += e.effect->getLatencySamples();
    }
    for (auto& n : nodes)
    {
        juce::int64 ahead = n->ownLatency;
        for (int cur = n->outputNode, guard = 0; cur >= 0 && guard < 64; cur = nodes[(size_t) cur]->outputNode, ++guard)
            ahead += nodes[(size_t) cur]->ownLatency;
        n->readAhead = ahead;
        maxLatency = std::max (maxLatency, (int) ahead);
    }

    // Master
    if (options.includeMastering)
    {
        if (previous != nullptr && previous->mastering != nullptr)
            mastering = previous->mastering;
        else
        {
            mastering = std::make_shared<MasteringChain>();
            mastering->prepare (sr, block, 2);
        }
        mastering->applySettings (project.mastering);
        masteringOn.store (project.mastering.enabled);
    }
    masterBuffer.setSize (2, block);
    loudness.prepare (sr, 2);
}

MixGraph::~MixGraph() = default;

//==============================================================================
int MixGraph::trackIndex (const juce::String& id) const
{
    auto it = nodeIndex.find (id);
    return it == nodeIndex.end() ? -1 : it->second;
}

void MixGraph::setTrackVolume (const juce::String& id, float db) { if (auto i = trackIndex (id); i >= 0) nodes[(size_t) i]->volume.store (dsp::dbToGain (db)); }
void MixGraph::setTrackPan (const juce::String& id, float pan)   { if (auto i = trackIndex (id); i >= 0) nodes[(size_t) i]->pan.store (pan); }
void MixGraph::setTrackMute (const juce::String& id, bool m)     { if (auto i = trackIndex (id); i >= 0) nodes[(size_t) i]->mute.store (m); }
void MixGraph::setTrackSolo (const juce::String& id, bool s)     { if (auto i = trackIndex (id); i >= 0) nodes[(size_t) i]->solo.store (s); }
void MixGraph::setTrackMonitor (const juce::String& id, bool m)  { if (auto i = trackIndex (id); i >= 0) nodes[(size_t) i]->monitor.store (m); }

void MixGraph::setSendLevel (const juce::String& id, int sendIndex, float db)
{
    if (auto i = trackIndex (id); i >= 0)
    {
        auto& n = *nodes[(size_t) i];
        if (sendIndex >= 0 && sendIndex < (int) n.sends.size())
            n.sends[(size_t) sendIndex].second->store (dsp::dbToGain (db));
    }
}

dsp::Effect* MixGraph::findEffect (const juce::String& trackId, const juce::String& slotId) const
{
    if (auto i = trackIndex (trackId); i >= 0)
    {
        auto& n = *nodes[(size_t) i];
        for (size_t k = 0; k < n.effects.size(); ++k)
            if (n.slotIdList[k] == slotId) return n.effects[k].effect.get();
    }
    return nullptr;
}

VocalChain* MixGraph::findVocalChain (const juce::String& trackId) const
{
    if (auto i = trackIndex (trackId); i >= 0) return nodes[(size_t) i]->vocalChain.get();
    return nullptr;
}

Instrument* MixGraph::findInstrument (const juce::String& trackId) const
{
    if (auto i = trackIndex (trackId); i >= 0) return nodes[(size_t) i]->instrument.get();
    return nullptr;
}

const MeterValues* MixGraph::trackMeter (const juce::String& trackId) const
{
    if (auto i = trackIndex (trackId); i >= 0) return &nodes[(size_t) i]->meters;
    return nullptr;
}

void MixGraph::liveNote (int idx, int note, float velocity, bool on) noexcept
{
    if (idx < 0 || idx >= (int) nodes.size()) return;
    auto& n = *nodes[(size_t) idx];
    int s1, z1, s2, z2;
    n.liveFifo.prepareToWrite (1, s1, z1, s2, z2);
    if (z1 > 0) n.liveNotes[(size_t) s1] = { note, velocity, on };
    else if (z2 > 0) n.liveNotes[(size_t) s2] = { note, velocity, on };
    n.liveFifo.finishedWrite (z1 + z2);
}

void MixGraph::liveNoteOn (const juce::String& trackId, int note, float velocity) noexcept { liveNote (trackIndex (trackId), note, velocity, true); }
void MixGraph::liveNoteOff (const juce::String& trackId, int note) noexcept { liveNote (trackIndex (trackId), note, 0.0f, false); }

void MixGraph::allNotesOff() noexcept
{
    for (auto& n : nodes)
        if (n->instrument != nullptr) n->instrument->allNotesOff (false);
}

//==============================================================================
void MixGraph::chase (Node& n, juce::int64 pos) noexcept
{
    if (n.instrument == nullptr) return;
    for (auto& s : n.spans)
        if (s.start < pos && s.end > pos + 64)
            n.instrument->noteOn (s.pitch, s.velocity);
}

void MixGraph::renderMidi (Node& n, juce::int64 pos, int num, bool playing) noexcept
{
    auto* inst = n.instrument.get();
    if (inst == nullptr) return;

    // Live notes from the keyboard go in at the start of the block.
    int s1, z1, s2, z2;
    n.liveFifo.prepareToRead (n.liveFifo.getNumReady(), s1, z1, s2, z2);
    auto applyLive = [inst] (const Node::LiveNote& e)
    {
        if (e.note == sustainNote) inst->setSustain (e.velocity > 0.5f);
        else if (e.on)             inst->noteOn (e.note, e.velocity);
        else                       inst->noteOff (e.note);
    };
    for (int k = 0; k < z1; ++k) applyLive (n.liveNotes[(size_t) (s1 + k)]);
    for (int k = 0; k < z2; ++k) applyLive (n.liveNotes[(size_t) (s2 + k)]);
    n.liveFifo.finishedRead (z1 + z2);

    int rendered = 0;
    if (playing)
    {
        if (pos != n.lastBlockEnd)
        {
            n.noteCursor = (size_t) (std::lower_bound (n.notes.begin(), n.notes.end(), pos,
                                                       [] (const NoteEvent& e, juce::int64 v) { return e.sample < v; }) - n.notes.begin());
        }
        const juce::int64 end = pos + num;
        while (n.noteCursor < n.notes.size() && n.notes[n.noteCursor].sample < end)
        {
            auto& e = n.notes[n.noteCursor];
            const int offset = (int) juce::jlimit ((juce::int64) 0, (juce::int64) num, e.sample - pos);
            if (offset > rendered)
            {
                inst->render (n.buffer, rendered, offset - rendered);
                rendered = offset;
            }
            if (e.velocity > 0) inst->noteOn (e.note, e.velocity);
            else                inst->noteOff (e.note);
            ++n.noteCursor;
        }
        n.lastBlockEnd = end;
    }
    else
        n.lastBlockEnd = -1;

    if (rendered < num)
        inst->render (n.buffer, rendered, num - rendered);
}

void MixGraph::renderAudioClips (Node& n, juce::int64 pos, int num) noexcept
{
    const juce::int64 end = pos + num;
    for (auto& c : n.clips)
    {
        const juce::int64 cEnd = c.start + c.length;
        if (cEnd <= pos || c.start >= end) continue;

        const auto& src = *c.audio;
        const int from = (int) std::max ((juce::int64) 0, c.start - pos);
        const int to = (int) std::min ((juce::int64) num, cEnd - pos);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* in = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
            float* out = n.buffer.getWritePointer (ch);
            for (int i = from; i < to; ++i)
            {
                const juce::int64 rel = pos + i - c.start;
                const juce::int64 si = c.sourceOffset + rel;
                if (si < 0 || si >= src.getNumSamples()) continue;
                float g = c.gain;
                if (c.fadeIn > 0 && rel < c.fadeIn) g *= (float) rel / (float) c.fadeIn;
                if (c.fadeOut > 0 && rel > c.length - c.fadeOut) g *= (float) (c.length - rel) / (float) c.fadeOut;
                out[i] += in[si] * g;
            }
        }
    }
}

void MixGraph::renderNode (Node& n, juce::int64 pos, int num, bool playing, const juce::AudioBuffer<float>* input) noexcept
{
    const juce::int64 p = pos + n.readAhead;
    if (n.type == TrackType::Midi)
        renderMidi (n, p, num, playing);
    else if (n.type == TrackType::Audio)
    {
        if (playing)
            renderAudioClips (n, p, num);

        // Input monitoring for armed tracks (through the track's effects).
        if (input != nullptr && n.monitor.load (std::memory_order_relaxed) && input->getNumChannels() > 0)
        {
            const int c0 = juce::jlimit (0, input->getNumChannels() - 1, n.inputChannel);
            const int c1 = n.inputChannels > 1 ? juce::jmin (c0 + 1, input->getNumChannels() - 1) : c0;
            n.buffer.addFrom (0, 0, *input, c0, 0, num);
            n.buffer.addFrom (1, 0, *input, c1, 0, num);
        }
    }
}

void MixGraph::process (juce::AudioBuffer<float>& output, juce::int64 pos, int num, bool playing,
                        const juce::AudioBuffer<float>* input) noexcept
{
    num = juce::jmin (num, options.maxBlockSize, output.getNumSamples());
    masterBuffer.clear (0, num);
    for (auto& n : nodes) n->buffer.clear (0, num);

    const bool doChase = chasePending.exchange (false) && playing;

    // Solo: a node is audible if nothing is soloed, it is soloed, it feeds a soloed bus,
    // or (for buses) a soloed track feeds it.
    bool anySolo = false;
    for (auto& n : nodes) { n->feedsSoloed = false; anySolo = anySolo || n->solo.load (std::memory_order_relaxed); }
    if (anySolo)
    {
        for (auto& n : nodes)
            if (n->solo.load (std::memory_order_relaxed))
            {
                for (int cur = n->outputNode, g = 0; cur >= 0 && g < 64; cur = nodes[(size_t) cur]->outputNode, ++g)
                    nodes[(size_t) cur]->feedsSoloed = true;
                for (auto& s : n->sends) nodes[(size_t) s.first]->feedsSoloed = true;
            }
    }

    for (int idx : order)
    {
        auto& n = *nodes[(size_t) idx];
        if (doChase) chase (n, pos + n.readAhead);
        renderNode (n, pos, num, playing, input);

        juce::AudioBuffer<float> view (n.buffer.getArrayOfWritePointers(), 2, num);

        // Effect automation (block rate).
        const juce::int64 ap = pos + n.readAhead;
        float autoVolumeDb = 0;
        bool hasVolumeAuto = false, hasPanAuto = false;
        float autoPan = 0;
        for (auto& a : n.automation)
        {
            switch (a.kind)
            {
                case AutomationTarget::Kind::Volume: autoVolumeDb = a.valueAt (ap); hasVolumeAuto = true; break;
                case AutomationTarget::Kind::Pan:    autoPan = a.valueAt (ap); hasPanAuto = true; break;
                case AutomationTarget::Kind::Effect: a.effect->setParamByIndex (a.paramIndex, a.valueAt (ap)); break;
            }
        }

        // Skip the effect chain once a track has been silent for longer than any tail
        // (10 s covers long reverbs and the spectral effects' latency). Saves a lot of CPU
        // on empty vocal tracks with full vocal chains.
        const bool inputSilent = n.buffer.getMagnitude (0, num) < 1.0e-7f && n.buffer.getMagnitude (1, num) < 1.0e-7f;
        n.silentSamples = inputSilent ? juce::jmin (n.silentSamples + num, std::numeric_limits<int>::max() / 2) : 0;
        const bool runEffects = n.silentSamples < (int) (10.0 * options.sampleRate) + n.ownLatency;
        if (runEffects)
        {
            if (n.vocalChain != nullptr)
                n.vocalChain->process (view, nullptr);
            for (auto& e : n.effects)
            {
                if (e.effect->bypassed()) continue;
                const juce::AudioBuffer<float>* sc = nullptr;
                if (e.sidechainTrack >= 0) sc = &nodes[(size_t) e.sidechainTrack]->preFader;
                e.effect->process (view, sc);
            }
        }

        for (int ch = 0; ch < 2; ++ch)
            n.preFader.copyFrom (ch, 0, view, ch, 0, num);

        // Fader and pan, ramped across the block to avoid zipper noise.
        float vol = n.volume.load (std::memory_order_relaxed);
        if (hasVolumeAuto) vol *= dsp::dbToGain (autoVolumeDb);
        const float pan = juce::jlimit (-1.0f, 1.0f, hasPanAuto ? autoPan : n.pan.load (std::memory_order_relaxed));
        const float gl = vol * juce::jmin (1.0f, 1.0f - pan);
        const float gr = vol * juce::jmin (1.0f, 1.0f + pan);
        view.applyGainRamp (0, 0, num, n.smoothedGainL, gl);
        view.applyGainRamp (1, 0, num, n.smoothedGainR, gr);
        n.smoothedGainL = gl;
        n.smoothedGainR = gr;

        n.meters.update (view, num);
        if (! n.effects.empty())
            n.meters.gainReductionDb.store (n.effects.back().effect->getGainReductionDb(), std::memory_order_relaxed);

        const bool audible = n.included && ! n.mute.load (std::memory_order_relaxed)
                          && (! anySolo || n.solo.load (std::memory_order_relaxed) || n.feedsSoloed
                              || (n.outputNode >= 0 && nodes[(size_t) n.outputNode]->solo.load (std::memory_order_relaxed)));
        if (! audible)
            continue;

        for (size_t s = 0; s < n.sends.size(); ++s)
        {
            auto& target = nodes[(size_t) n.sends[s].first]->buffer;
            const float g = n.sends[s].second->load (std::memory_order_relaxed);
            const auto& srcBuf = n.sendPreFader[s] ? n.preFader : n.buffer;
            for (int ch = 0; ch < 2; ++ch)
                target.addFrom (ch, 0, srcBuf, ch, 0, num, g);
        }

        auto& dest = n.outputNode >= 0 ? nodes[(size_t) n.outputNode]->buffer : masterBuffer;
        for (int ch = 0; ch < 2; ++ch)
            dest.addFrom (ch, 0, n.buffer, ch, 0, num);
    }

    juce::AudioBuffer<float> masterView (masterBuffer.getArrayOfWritePointers(), 2, num);
    if (mastering != nullptr && options.includeMastering && masteringOn.load (std::memory_order_relaxed))
        mastering->process (masterView, nullptr);
    masterView.applyGain (masterGain.load (std::memory_order_relaxed));

    masterMeters.update (masterView, num);
    if (mastering != nullptr)
        masterMeters.gainReductionDb.store (mastering->getGainReductionDb(), std::memory_order_relaxed);
    if (loudnessResetPending.exchange (false)) loudness.reset();
    loudness.process (masterView);

    for (int ch = 0; ch < juce::jmin (2, output.getNumChannels()); ++ch)
        output.copyFrom (ch, 0, masterView, ch, 0, num);
}

} // namespace studio
