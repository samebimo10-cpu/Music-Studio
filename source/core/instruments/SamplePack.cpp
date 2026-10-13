#include "SamplePack.h"
#include <mutex>

namespace studio
{

//==============================================================================
// DiskStreamer
//==============================================================================
DiskStreamer& DiskStreamer::getInstance()
{
    static DiskStreamer instance;
    return instance;
}

DiskStreamer::DiskStreamer() : juce::Thread ("Sample streamer")
{
    formats.registerBasicFormats();
    startThread (juce::Thread::Priority::high);
}

DiskStreamer::~DiskStreamer()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (2000);
}

int DiskStreamer::request (SampleZone* zone) noexcept
{
    for (int i = 0; i < maxSlots; ++i)
    {
        int expected = Slot::Idle;
        if (slots[(size_t) i].state.compare_exchange_strong (expected, Slot::Requested))
        {
            auto& s = slots[(size_t) i];
            s.zone.store (zone);
            s.readFrame.store (zone->preload.getNumSamples());
            s.writeFrame.store (zone->preload.getNumSamples());
            wake.signal();
            return i;
        }
    }
    return -1; // every slot busy: the voice plays only the preloaded part
}

void DiskStreamer::release (int index) noexcept
{
    if (index >= 0 && index < maxSlots)
    {
        slots[(size_t) index].state.store (Slot::Stopping);
        wake.signal();
    }
}

void DiskStreamer::run()
{
    constexpr int chunk = 4096;
    juce::AudioBuffer<float> temp (2, chunk);

    while (! threadShouldExit())
    {
        bool didWork = false;
        for (auto& s : slots)
        {
            const int state = s.state.load();
            if (state == Slot::Idle)
                continue;

            if (state == Slot::Stopping)
            {
                s.zone.store (nullptr);
                s.state.store (Slot::Idle);
                continue;
            }

            auto* zone = s.zone.load();
            if (zone == nullptr) continue;

            if (state == Slot::Requested)
            {
                if (s.readerZone != zone)
                {
                    s.reader.reset (formats.createReaderFor (zone->file));
                    s.readerZone = zone;
                }
                s.state.store (Slot::Streaming);
            }
            if (s.reader == nullptr) continue;

            // Fill while there is room in the ring.
            for (int guard = 0; guard < 4; ++guard)
            {
                const auto w = s.writeFrame.load();
                const auto r = s.readFrame.load();
                const auto space = (juce::int64) ringFrames - (w - r);
                const auto remaining = zone->lengthFrames - w;
                if (space < chunk || remaining <= 0)
                    break;

                const int n = (int) juce::jmin ((juce::int64) chunk, remaining);
                temp.clear();
                s.reader->read (&temp, 0, n, w, true, true);
                for (int i = 0; i < n; ++i)
                {
                    const int idx = (int) ((w + i) % ringFrames);
                    s.ring.setSample (0, idx, temp.getSample (0, i));
                    s.ring.setSample (1, idx, temp.getSample (zone->numChannels > 1 ? 1 : 0, i));
                }
                s.writeFrame.store (w + n);
                didWork = true;
            }
        }
        if (! didWork)
            wake.wait (3);
    }
}

//==============================================================================
// SamplerInstrument
//==============================================================================
SamplerInstrument::SamplerInstrument (std::shared_ptr<SampledInstrumentDef> def) : definition (std::move (def)) {}

SamplerInstrument::~SamplerInstrument()
{
    for (auto& v : voices)
        if (v.streamSlot >= 0) DiskStreamer::getInstance().release (v.streamSlot);
}

void SamplerInstrument::prepare (double sampleRate, int)
{
    sr = sampleRate;
    DiskStreamer::getInstance(); // make sure the streamer thread exists before audio starts
}

int SamplerInstrument::getActiveVoices() const
{
    int n = 0;
    for (auto& v : voices) if (v.active) ++n;
    return n;
}

void SamplerInstrument::noteOn (int note, float velocity)
{
    const int vel = juce::jlimit (0, 127, (int) std::round (velocity * 127.0f));

    // Matching zones; pick the round-robin group in turn.
    SampleZone* candidates[16];
    int count = 0;
    for (auto& z : definition->zones)
        if (note >= z->loNote && note <= z->hiNote && vel >= z->loVel && vel <= z->hiVel && count < 16)
            candidates[count++] = z.get();
    if (count == 0)
        return;

    auto& rr = rrCounter[(size_t) juce::jlimit (0, 127, note)];
    SampleZone* zone = candidates[rr % count];
    ++rr;

    Voice* v = nullptr;
    for (auto& cand : voices) if (! cand.active) { v = &cand; break; }
    if (v == nullptr)
    {
        v = &voices[0];
        for (auto& cand : voices) if (cand.age < v->age) v = &cand;
        if (v->streamSlot >= 0) DiskStreamer::getInstance().release (v->streamSlot);
    }

    v->zone = zone;
    v->note = note;
    v->pos = 0;
    v->inc = std::pow (2.0, (note - zone->rootNote) / 12.0) * zone->fileSampleRate / sr;
    v->gain = 0.3f + 0.7f * velocity;
    v->env = 0;
    v->envTarget = 1;
    v->envStep = (float) (1.0 / juce::jmax (1.0, definition->attack * sr));
    v->active = v->held = true;
    v->sustained = false;
    v->age = ++ages;
    v->streamSlot = zone->lengthFrames > zone->preload.getNumSamples() ? DiskStreamer::getInstance().request (zone) : -1;
}

void SamplerInstrument::noteOff (int note)
{
    for (auto& v : voices)
    {
        if (! v.active || v.note != note || ! v.held) continue;
        v.held = false;
        if (sustainDown) { v.sustained = true; continue; }
        v.envTarget = 0;
        v.envStep = (float) (1.0 / juce::jmax (1.0, definition->release * sr));
    }
}

void SamplerInstrument::setSustain (bool down)
{
    sustainDown = down;
    if (! down)
        for (auto& v : voices)
            if (v.sustained)
            {
                v.sustained = false;
                v.envTarget = 0;
                v.envStep = (float) (1.0 / juce::jmax (1.0, definition->release * sr));
            }
}

void SamplerInstrument::allNotesOff (bool immediately)
{
    for (auto& v : voices)
    {
        if (! v.active) continue;
        if (immediately)
        {
            v.active = false;
            if (v.streamSlot >= 0) { DiskStreamer::getInstance().release (v.streamSlot); v.streamSlot = -1; }
        }
        else
        {
            v.envTarget = 0;
            v.envStep = (float) (1.0 / juce::jmax (1.0, definition->release * sr));
        }
    }
}

float SamplerInstrument::readFrame (Voice& v, int ch, juce::int64 frame) noexcept
{
    auto* z = v.zone;
    const int c = juce::jmin (ch, z->preload.getNumChannels() - 1);
    if (frame < z->preload.getNumSamples())
        return z->preload.getSample (c, (int) frame);
    if (v.streamSlot < 0 || frame >= z->lengthFrames)
        return 0.0f;

    auto& s = DiskStreamer::getInstance().slot (v.streamSlot);
    if (frame >= s.writeFrame.load (std::memory_order_acquire))
    {
        ++DiskStreamer::getInstance().underruns;
        return 0.0f;
    }
    return s.ring.getSample (juce::jmin (ch, 1), (int) (frame % DiskStreamer::ringFrames));
}

void SamplerInstrument::render (juce::AudioBuffer<float>& out, int start, int num)
{
    auto* L = out.getWritePointer (0);
    auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

    for (auto& v : voices)
    {
        if (! v.active) continue;
        for (int i = start; i < start + num; ++i)
        {
            const auto f0 = (juce::int64) v.pos;
            const float fr = (float) (v.pos - (double) f0);
            if (f0 + 1 >= v.zone->lengthFrames) { v.active = false; break; }

            const float l = readFrame (v, 0, f0) + (readFrame (v, 0, f0 + 1) - readFrame (v, 0, f0)) * fr;
            const float r = readFrame (v, 1, f0) + (readFrame (v, 1, f0 + 1) - readFrame (v, 1, f0)) * fr;

            if (v.env < v.envTarget) v.env = juce::jmin (v.envTarget, v.env + v.envStep);
            else if (v.env > v.envTarget) v.env = juce::jmax (v.envTarget, v.env - v.envStep);

            const float g = v.gain * v.env;
            L[i] += l * g;
            if (R != nullptr) R[i] += r * g;
            v.pos += v.inc;

            if (v.envTarget == 0 && v.env <= 0) { v.active = false; break; }
        }

        if (v.streamSlot >= 0)
        {
            auto& s = DiskStreamer::getInstance().slot (v.streamSlot);
            s.readFrame.store (juce::jmax ((juce::int64) v.pos - 2, (juce::int64) v.zone->preload.getNumSamples()));
            if (! v.active) { DiskStreamer::getInstance().release (v.streamSlot); v.streamSlot = -1; }
        }
    }
}

//==============================================================================
// SamplePackManager
//==============================================================================
SamplePackManager& SamplePackManager::getInstance()
{
    static SamplePackManager m;
    return m;
}

SamplePackManager::SamplePackManager() : packsFolder (defaultPacksFolder()) {}

juce::File SamplePackManager::defaultPacksFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Studio").getChildFile ("Sound Packs");
}

void SamplePackManager::setPacksFolder (const juce::File& folder)
{
    std::lock_guard<std::mutex> l (cacheLock);
    packsFolder = folder;
    cache.clear();
}

std::vector<SamplePackInfo> SamplePackManager::scanInstalled() const
{
    std::vector<SamplePackInfo> out;
    for (auto& dir : packsFolder.findChildFiles (juce::File::findDirectories, false))
    {
        auto manifest = dir.getChildFile ("pack.json");
        if (! manifest.existsAsFile()) continue;
        auto v = juce::JSON::parse (manifest);
        SamplePackInfo info;
        info.id = v.getProperty ("id", dir.getFileName()).toString();
        info.name = v.getProperty ("name", info.id).toString();
        info.version = v.getProperty ("version", "1.0").toString();
        info.description = v.getProperty ("description", "").toString();
        info.folder = dir;
        info.installed = true;
        if (auto* arr = v.getProperty ("instruments", {}).getArray())
            for (auto& i : *arr) info.instrumentIds.add (i.getProperty ("id", "").toString());
        for (auto& f : dir.findChildFiles (juce::File::findFiles, true))
            info.sizeBytes += f.getSize();
        out.push_back (info);
    }
    return out;
}

juce::Result SamplePackManager::installFromFile (const juce::File& source)
{
    packsFolder.createDirectory();
    if (source.isDirectory())
    {
        if (! source.getChildFile ("pack.json").existsAsFile())
            return juce::Result::fail ("That folder is not a sound pack (no pack.json).");
        auto target = packsFolder.getChildFile (source.getFileName());
        if (! source.copyDirectoryTo (target))
            return juce::Result::fail ("Could not copy the pack. Is there enough free space?");
        return juce::Result::ok();
    }

    juce::ZipFile zip (source);
    if (zip.getNumEntries() == 0)
        return juce::Result::fail ("The file is not a valid pack archive.");
    auto target = packsFolder.getChildFile (source.getFileNameWithoutExtension());
    auto r = zip.uncompressTo (target, true);
    if (r.failed()) return r;

    // Archives may contain a single top-level folder.
    if (! target.getChildFile ("pack.json").existsAsFile())
    {
        auto subs = target.findChildFiles (juce::File::findDirectories, false);
        if (subs.size() == 1 && subs[0].getChildFile ("pack.json").existsAsFile())
        {
            auto tmp = packsFolder.getChildFile (subs[0].getFileName() + "_tmp");
            subs[0].moveFileTo (tmp);
            target.deleteRecursively();
            tmp.moveFileTo (target);
        }
        else
            return juce::Result::fail ("The archive has no pack.json.");
    }
    return juce::Result::ok();
}

juce::Result SamplePackManager::remove (const juce::String& packId)
{
    for (auto& p : scanInstalled())
        if (p.id == packId)
        {
            {
                std::lock_guard<std::mutex> l (cacheLock);
                for (auto it = cache.begin(); it != cache.end();)
                    it = it->first.startsWith (packId + "/") ? cache.erase (it) : std::next (it);
            }
            return p.folder.deleteRecursively() ? juce::Result::ok() : juce::Result::fail ("Could not delete the pack folder.");
        }
    return juce::Result::fail ("Pack not found.");
}

juce::int64 SamplePackManager::preloadBytes() const
{
    std::lock_guard<std::mutex> l (cacheLock);
    juce::int64 total = 0;
    for (auto& [k, def] : cache)
        for (auto& z : def->zones)
            total += (juce::int64) z->preload.getNumChannels() * z->preload.getNumSamples() * (juce::int64) sizeof (float);
    return total;
}

std::shared_ptr<SampledInstrumentDef> SamplePackManager::parseInstrument (const juce::File& folder, const juce::var& inst)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    auto def = std::make_shared<SampledInstrumentDef>();
    def->id = inst.getProperty ("id", "").toString();
    def->name = inst.getProperty ("name", def->id).toString();
    def->family = inst.getProperty ("family", "").toString();
    def->role = inst.getProperty ("role", "").toString();
    def->attack = (float) (double) inst.getProperty ("attack", 0.002);
    def->release = (float) (double) inst.getProperty ("release", 0.3);

    if (auto* zones = inst.getProperty ("zones", {}).getArray())
    {
        for (auto& zv : *zones)
        {
            auto z = std::make_shared<SampleZone>();
            z->file = folder.getChildFile (zv.getProperty ("file", "").toString());
            z->rootNote = zv.getProperty ("root", 60);
            z->loNote = zv.getProperty ("lo", z->rootNote);
            z->hiNote = zv.getProperty ("hi", z->rootNote);
            z->loVel = zv.getProperty ("velLo", 0);
            z->hiVel = zv.getProperty ("velHi", 127);
            z->roundRobin = zv.getProperty ("rr", 0);

            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (z->file));
            if (reader == nullptr) continue;
            z->fileSampleRate = reader->sampleRate;
            z->lengthFrames = reader->lengthInSamples;
            z->numChannels = (int) reader->numChannels;
            const int pre = (int) juce::jmin ((juce::int64) preloadFrames, z->lengthFrames);
            z->preload.setSize (juce::jmin (2, z->numChannels), pre);
            reader->read (&z->preload, 0, pre, 0, true, true);
            def->zones.push_back (z);
        }
    }
    return def;
}

std::shared_ptr<SampledInstrumentDef> SamplePackManager::loadInstrument (const juce::String& spec)
{
    {
        std::lock_guard<std::mutex> l (cacheLock);
        if (auto it = cache.find (spec); it != cache.end())
            return it->second;
    }

    const auto packId = spec.upToFirstOccurrenceOf ("/", false, false);
    const auto instId = spec.fromFirstOccurrenceOf ("/", false, false);
    for (auto& p : scanInstalled())
    {
        if (p.id != packId) continue;
        auto v = juce::JSON::parse (p.folder.getChildFile ("pack.json"));
        if (auto* arr = v.getProperty ("instruments", {}).getArray())
            for (auto& inst : *arr)
                if (inst.getProperty ("id", "").toString() == instId)
                {
                    auto def = parseInstrument (p.folder, inst);
                    std::lock_guard<std::mutex> l (cacheLock);
                    cache[spec] = def;
                    return def;
                }
    }
    return nullptr;
}

std::shared_ptr<SamplePackManager::Download> SamplePackManager::startDownload (const juce::String& packId, const juce::URL& url,
                                                                                std::function<void (double)> onProgress,
                                                                                std::function<void (juce::Result)> onFinished)
{
    auto dl = std::make_shared<Download>();
    dl->packId = packId;
    dl->url = url;
    dl->partialFile = packsFolder.getChildFile (".downloads").getChildFile (packId + ".zip.part");
    dl->onProgress = std::move (onProgress);
    dl->onFinished = std::move (onFinished);

    downloadPool.addJob ([this, dl]
    {
        dl->partialFile.getParentDirectory().createDirectory();
        auto finish = [dl] (juce::Result r)
        {
            juce::MessageManager::callAsync ([dl, r] { if (dl->onFinished) dl->onFinished (r); });
        };

        // Resume: ask only for the bytes we do not have yet.
        for (int attempt = 0; attempt < 6 && ! dl->cancelled; ++attempt)
        {
            const auto have = dl->partialFile.getSize();
            int status = 0;
            juce::StringPairArray responseHeaders;
            auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                               .withExtraHeaders (have > 0 ? "Range: bytes=" + juce::String (have) + "-" : juce::String())
                               .withConnectionTimeoutMs (15000)
                               .withStatusCode (&status)
                               .withResponseHeaders (&responseHeaders);
            auto stream = dl->url.createInputStream (options);
            if (stream == nullptr || (status != 200 && status != 206))
            {
                juce::Thread::sleep (1000 * (1 << attempt)); // back off and retry after a drop
                continue;
            }

            if (status == 200 && have > 0)
                dl->partialFile.deleteFile(); // server ignored the range: start over

            const auto total = stream->getTotalLength() + (status == 206 ? have : 0);
            juce::FileOutputStream out (dl->partialFile);
            if (! out.openedOk()) { finish (juce::Result::fail ("Cannot write to the packs folder.")); return; }

            char buf[65536];
            bool dropped = false;
            while (! stream->isExhausted() && ! dl->cancelled)
            {
                const int n = stream->read (buf, sizeof (buf));
                if (n <= 0) { dropped = ! stream->isExhausted(); break; }
                out.write (buf, (size_t) n);
                if (total > 0)
                {
                    dl->progress = (double) out.getPosition() / (double) total;
                    auto p = dl->progress.load();
                    juce::MessageManager::callAsync ([dl, p] { if (dl->onProgress) dl->onProgress (p); });
                }
            }
            out.flush();
            if (dl->cancelled) { finish (juce::Result::fail ("Download cancelled. It will resume next time.")); return; }
            if (dropped) continue;

            auto zip = dl->partialFile.withFileExtension ("").withFileExtension ("zip");
            dl->partialFile.moveFileTo (zip);
            auto r = installFromFile (zip);
            zip.deleteFile();
            finish (r);
            return;
        }
        finish (juce::Result::fail ("The download kept dropping. It will resume from where it stopped next time."));
    });
    return dl;
}

std::unique_ptr<Instrument> createSamplerInstrument (const juce::String& spec)
{
    if (auto def = SamplePackManager::getInstance().loadInstrument (spec))
        return std::make_unique<SamplerInstrument> (def);
    return nullptr;
}

} // namespace studio
