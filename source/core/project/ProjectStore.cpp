#include "ProjectStore.h"
#include <juce_cryptography/juce_cryptography.h>
#include "../engine/ClipCache.h"
#include "../model/TempoMap.h"
#include "../mixing/MixTools.h"

namespace studio
{

namespace store
{
    juce::File projectFile (const juce::File& f)   { return f.getChildFile ("project.json"); }
    juce::File audioFolder (const juce::File& f)   { return f.getChildFile ("audio"); }
    juce::File midiFolder (const juce::File& f)    { return f.getChildFile ("midi"); }
    juce::File takesFolder (const juce::File& f)   { return f.getChildFile ("takes"); }
    juce::File presetsFolder (const juce::File& f) { return f.getChildFile ("presets"); }
    juce::File releaseFolder (const juce::File& f) { return f.getChildFile ("release"); }
    juce::File historyFolder (const juce::File& f) { return f.getChildFile ("history"); }
    juce::File exportsFolder (const juce::File& f) { return f.getChildFile ("release").getChildFile ("exports"); }

    juce::File defaultProjectsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Studio Projects");
    }

    juce::Result create (const juce::File& parent, Project& p, juce::File& folderOut)
    {
        const auto safe = juce::File::createLegalFileName (p.name.isEmpty() ? juce::String ("Untitled") : p.name);
        folderOut = parent.getChildFile (safe + ".studio").getNonexistentSibling (false);
        if (! folderOut.createDirectory())
            return juce::Result::fail ("Could not create the project folder in " + parent.getFullPathName());
        for (auto f : { audioFolder (folderOut), midiFolder (folderOut), takesFolder (folderOut), presetsFolder (folderOut),
                        releaseFolder (folderOut), historyFolder (folderOut), exportsFolder (folderOut) })
            f.createDirectory();
        if (p.id.isEmpty()) p.id = makeId ("prj");
        if (p.createdAt.isEmpty()) p.createdAt = juce::Time::getCurrentTime().toISO8601 (true);
        return save (p, folderOut);
    }

    juce::Result save (Project& p, const juce::File& folder)
    {
        if (! folder.isDirectory() && ! folder.createDirectory())
            return juce::Result::fail ("The project folder is missing: " + folder.getFullPathName());
        p.modifiedAt = juce::Time::getCurrentTime().toISO8601 (true);

        // Store paths relative to the project folder when possible.
        Project copy = p;
        for (auto& t : copy.tracks)
        {
            for (auto& c : t.clips) if (c.audioFile.isNotEmpty()) c.audioFile = relativePath (folder, resolve (folder, c.audioFile));
            for (auto& tk : t.takes) if (tk.audioFile.isNotEmpty()) tk.audioFile = relativePath (folder, resolve (folder, tk.audioFile));
        }
        if (copy.release.artworkFile.isNotEmpty())
            copy.release.artworkFile = relativePath (folder, resolve (folder, copy.release.artworkFile));

        auto target = projectFile (folder);
        juce::TemporaryFile temp (target);
        if (! temp.getFile().replaceWithText (copy.toJson()))
            return juce::Result::fail ("Could not write the project file. Is the disk full?");
        if (! temp.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("Could not replace project.json.");

        auto meta = copy.toVar().getProperty ("release", {});
        releaseFolder (folder).createDirectory();
        releaseFolder (folder).getChildFile ("metadata.json").replaceWithText (juce::JSON::toString (meta, false));
        return juce::Result::ok();
    }

    juce::Result load (const juce::File& folder, Project& out)
    {
        auto f = projectFile (folder);
        if (! f.existsAsFile())
            return juce::Result::fail ("No project.json in " + folder.getFullPathName());
        auto v = juce::JSON::parse (f);
        if (! v.isObject())
            return juce::Result::fail ("project.json is damaged. Try recovering from history/.");
        out = Project::fromVar (v);
        return juce::Result::ok();
    }

    juce::String relativePath (const juce::File& folder, const juce::File& file)
    {
        if (file.isAChildOf (folder))
            return file.getRelativePathFrom (folder).replaceCharacter ('\\', '/');
        return file.getFullPathName();
    }

    juce::File resolve (const juce::File& folder, const juce::String& path)
    {
        if (juce::File::isAbsolutePath (path)) return juce::File (path);
        return folder.getChildFile (path);
    }

    juce::Result importAudio (Project& p, const juce::File& folder, const juce::File& source, double atBeat,
                              const juce::String& trackId, juce::String* newTrackIdOut)
    {
        juce::AudioBuffer<float> audio;
        double rate = 0;
        if (! ClipCache::readFile (source, audio, rate))
            return juce::Result::fail ("Can't read " + source.getFileName() + ". Supported: WAV, AIFF, FLAC, MP3, OGG.");

        auto dest = audioFolder (folder).getChildFile (source.getFileName()).getNonexistentSibling();
        audioFolder (folder).createDirectory();
        if (! source.copyFileTo (dest))
            return juce::Result::fail ("Could not copy the file into the project.");

        Track* t = trackId.isNotEmpty() ? p.findTrack (trackId) : nullptr;
        if (t == nullptr)
        {
            auto& nt = p.addTrack (TrackType::Audio, source.getFileNameWithoutExtension());
            nt.role = mixing::guessRole (nt);
            t = &nt;
        }
        if (newTrackIdOut != nullptr) *newTrackIdOut = t->id;

        TempoMap tm (p);
        const double seconds = audio.getNumSamples() / rate;
        Clip c;
        c.id = makeId ("clip");
        c.name = source.getFileNameWithoutExtension();
        c.audioFile = relativePath (folder, dest);
        c.startBeat = atBeat;
        c.lengthBeats = tm.secondsToBeats (tm.beatsToSeconds (atBeat) + seconds) - atBeat;
        t->clips.push_back (c);
        return juce::Result::ok();
    }

    //==========================================================================
    juce::Result saveSnapshot (const Project& p, const juce::File& folder, const juce::String& name)
    {
        historyFolder (folder).createDirectory();
        auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
        auto file = historyFolder (folder).getChildFile (stamp + "_" + juce::File::createLegalFileName (name) + ".json").getNonexistentSibling();
        auto v = p.toVar();
        if (auto* o = v.getDynamicObject()) o->setProperty ("snapshotName", name);
        return file.replaceWithText (juce::JSON::toString (v, true)) ? juce::Result::ok() : juce::Result::fail ("Could not write the snapshot.");
    }

    std::vector<Snapshot> listSnapshots (const juce::File& folder)
    {
        std::vector<Snapshot> out;
        for (auto& f : historyFolder (folder).findChildFiles (juce::File::findFiles, false, "*.json"))
        {
            Snapshot s;
            s.file = f;
            s.time = f.getLastModificationTime();
            auto base = f.getFileNameWithoutExtension();
            s.name = base.fromFirstOccurrenceOf ("_", false, false).replaceCharacter ('_', ' ');
            out.push_back (s);
        }
        std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.time > b.time; });
        return out;
    }

    juce::Result loadSnapshot (const Snapshot& s, Project& out)
    {
        auto v = juce::JSON::parse (s.file);
        if (! v.isObject()) return juce::Result::fail ("The snapshot is damaged.");
        out = Project::fromVar (v);
        return juce::Result::ok();
    }

    void pruneSnapshots (const juce::File& folder, int keep)
    {
        auto all = listSnapshots (folder);
        int automatic = 0;
        for (auto& s : all)
            if (s.name.startsWith ("Auto"))
                if (++automatic > keep) s.file.deleteFile();
    }

    //==========================================================================
    juce::File autosaveFile (const juce::File& folder) { return folder.getChildFile ("project.autosave.json"); }
    juce::File lockFile (const juce::File& folder)     { return folder.getChildFile (".session.lock"); }

    juce::Result writeAutosave (const Project& p, const juce::File& folder)
    {
        juce::TemporaryFile temp (autosaveFile (folder));
        if (! temp.getFile().replaceWithText (p.toJson()) || ! temp.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("Autosave failed.");
        return juce::Result::ok();
    }

    bool needsRecovery (const juce::File& folder)
    {
        auto a = autosaveFile (folder);
        return lockFile (folder).existsAsFile() && a.existsAsFile()
            && a.getLastModificationTime() > projectFile (folder).getLastModificationTime();
    }

    juce::Result recover (const juce::File& folder, Project& out)
    {
        auto v = juce::JSON::parse (autosaveFile (folder));
        if (! v.isObject()) return juce::Result::fail ("The autosave is damaged.");
        out = Project::fromVar (v);
        return juce::Result::ok();
    }

    void markSessionOpen (const juce::File& folder)
    {
        lockFile (folder).replaceWithText (juce::String (juce::Time::getCurrentTime().toISO8601 (true)));
    }

    void markSessionClosed (const juce::File& folder)
    {
        lockFile (folder).deleteFile();
        autosaveFile (folder).deleteFile();
    }

    //==========================================================================
    std::vector<ManifestEntry> buildManifest (const juce::File& folder)
    {
        std::vector<ManifestEntry> out;
        for (auto& f : folder.findChildFiles (juce::File::findFiles, true))
        {
            if (f == autosaveFile (folder) || f == lockFile (folder) || f.getFileName().startsWith (".")) continue;
            ManifestEntry e;
            e.path = f.getRelativePathFrom (folder).replaceCharacter ('\\', '/');
            e.size = f.getSize();
            juce::FileInputStream in (f);
            e.sha256 = juce::SHA256 (in).toHexString();
            out.push_back (e);
        }
        std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.path < b.path; });
        return out;
    }

    juce::var manifestToVar (const std::vector<ManifestEntry>& m)
    {
        juce::Array<juce::var> arr;
        for (auto& e : m)
        {
            auto o = new juce::DynamicObject();
            o->setProperty ("path", e.path);
            o->setProperty ("size", e.size);
            o->setProperty ("sha256", e.sha256);
            arr.add (juce::var (o));
        }
        return arr;
    }

    std::vector<ManifestEntry> manifestFromVar (const juce::var& v)
    {
        std::vector<ManifestEntry> out;
        if (auto* arr = v.getArray())
            for (auto& e : *arr)
                out.push_back ({ e.getProperty ("path", "").toString(), (juce::int64) e.getProperty ("size", 0), e.getProperty ("sha256", "").toString() });
        return out;
    }

    std::vector<ManifestEntry> changedSince (const std::vector<ManifestEntry>& before, const std::vector<ManifestEntry>& now)
    {
        std::map<juce::String, juce::String> old;
        for (auto& e : before) old[e.path] = e.sha256;
        std::vector<ManifestEntry> out;
        for (auto& e : now)
        {
            auto it = old.find (e.path);
            if (it == old.end() || it->second != e.sha256) out.push_back (e);
        }
        return out;
    }
}

//==============================================================================
void UndoHistory::reset (const Project& p)
{
    states.clear();
    states.push_back ({ p.toJson(), "Open" });
    position = 0;
}

void UndoHistory::commit (const Project& p, const juce::String& description)
{
    auto json = p.toJson();
    if (position >= 0 && states[(size_t) position].json == json)
        return; // nothing changed
    while ((int) states.size() > position + 1) states.pop_back();
    states.push_back ({ json, description });
    position = (int) states.size() - 1;

    // Keep memory bounded on very long sessions by dropping the oldest states.
    size_t bytes = 0;
    for (auto& s : states) bytes += (size_t) s.json.getNumBytesAsUTF8();
    while (bytes > maxBytes && states.size() > 2)
    {
        bytes -= (size_t) states.front().json.getNumBytesAsUTF8();
        states.pop_front();
        --position;
    }
}

juce::String UndoHistory::undoDescription() const { return canUndo() ? states[(size_t) position].description : juce::String(); }
juce::String UndoHistory::redoDescription() const { return canRedo() ? states[(size_t) position + 1].description : juce::String(); }

bool UndoHistory::undo (Project& out)
{
    if (! canUndo()) return false;
    --position;
    out = Project::fromJson (states[(size_t) position].json);
    return true;
}

bool UndoHistory::redo (Project& out)
{
    if (! canRedo()) return false;
    ++position;
    out = Project::fromJson (states[(size_t) position].json);
    return true;
}

juce::StringArray UndoHistory::descriptions() const
{
    juce::StringArray d;
    for (auto& s : states) d.add (s.description);
    return d;
}

//==============================================================================
namespace templates
{
    juce::StringArray names() { return { "Worship single", "Praise medley", "Podcast", "Choir rehearsal track" }; }

    Project blank (const juce::String& songName)
    {
        Project p;
        p.id = makeId ("prj");
        p.name = songName;
        p.bpm = 90;
        p.key = Key::fromString ("C major");
        p.release.title = songName;
        p.createdAt = juce::Time::getCurrentTime().toISO8601 (true);
        auto& reverb = mixing::ensureBus (p, "Reverb");
        mixing::setEffect (reverb, "reverb", { { "type", 1 }, { "decay", 2.2f }, { "mix", 1.0f } }, "bus");
        reverb.volumeDb = -6;
        return p;
    }

    namespace
    {
        Track& vocalTrack (Project& p, const juce::String& name, TrackRole role, const juce::String& preset, const juce::String& reverbId, int input)
        {
            auto& t = p.addTrack (TrackType::Audio, name, role);
            t.vocalChain.enabled = true;
            t.vocalChain.preset = preset;
            t.vocalChain.amount = 0.7f;
            t.inputChannel = input;
            t.sends.push_back ({ reverbId, -14.0f, false });
            return t;
        }
    }

    Project create (const juce::String& name)
    {
        Project p = blank (name);
        const auto reverbId = mixing::ensureBus (p, "Reverb").id;

        if (name == "Worship single")
        {
            p.bpm = 72; p.key = Key::fromString ("D major"); p.style = "worship_ballad"; p.mood = "reflective";
            const auto musicId = mixing::ensureBus (p, "Music").id;
            juce::String bvBusId;
            {
                auto& bvBus = mixing::ensureBus (p, "Backing vocals", TrackRole::BackingVocal);
                mixing::setEffect (bvBus, "compressor", { { "threshold", -18 }, { "ratio", 2.5f } }, "bus");
                bvBusId = bvBus.id;
            }
            vocalTrack (p, "Lead vocal", TrackRole::LeadVocal, "Worship lead", reverbId, 0).armed = true;
            for (int i = 0; i < 2; ++i)
            {
                auto& bv = vocalTrack (p, "Backing vocal " + juce::String (i + 1), TrackRole::BackingVocal, "Choir stack", reverbId, 0);
                bv.outputBusId = bvBusId;
                bv.pan = i == 0 ? -0.4f : 0.4f;
            }
            {
                auto& piano = p.addTrack (TrackType::Midi, "Piano", TrackRole::Keys);
                piano.instrument.id = "grand_piano";
                piano.outputBusId = musicId;
                piano.sends.push_back ({ reverbId, -16.0f, false });
            }
            {
                auto& pad = p.addTrack (TrackType::Midi, "Pad", TrackRole::Pad);
                pad.instrument.id = "worship_pad";
                pad.outputBusId = musicId;
                pad.volumeDb = -12;
            }
            p.mastering.preset = "Gentle worship";
        }
        else if (name == "Praise medley")
        {
            p.bpm = 124; p.key = Key::fromString ("Bb major"); p.style = "nigerian_praise"; p.mood = "joyful";
            mixing::setEffect (mixing::ensureBus (p, "Drums", TrackRole::Drums), "compressor", { { "threshold", -16 }, { "ratio", 3 }, { "attack", 15 } }, "bus");
            vocalTrack (p, "Lead vocal", TrackRole::LeadVocal, "Praise lead", reverbId, 0).armed = true;
            vocalTrack (p, "Response vocals", TrackRole::Choir, "Choir stack", reverbId, 1);
            // Medley: tempo lift for the second song and a key change for the third.
            p.sections = {
                { makeId ("sec"), SectionType::Intro, "intro", 0, 16, 0.5f },
                { makeId ("sec"), SectionType::Chorus, "Song 1", 16, 64, 0.75f },
                { makeId ("sec"), SectionType::Chorus, "Song 2", 80, 64, 0.85f },
                { makeId ("sec"), SectionType::Vamp, "Song 3 (vamp)", 144, 64, 0.95f },
                { makeId ("sec"), SectionType::Outro, "outro", 208, 16, 0.5f },
            };
            p.tempoEvents.push_back ({ 80, 128 });
            p.keyEvents.push_back ({ 144, Key::fromString ("C major") });
            p.markers.push_back ({ 80, "Song 2 - tempo lift" });
            p.markers.push_back ({ 144, "Song 3 - key change" });
            p.mastering.preset = "Loud";
        }
        else if (name == "Podcast")
        {
            p.bpm = 120; p.style = ""; p.guidedMode = true;
            for (int i = 0; i < 2; ++i)
            {
                auto& host = p.addTrack (TrackType::Audio, i == 0 ? "Host" : "Guest", TrackRole::LeadVocal);
                host.vocalChain.enabled = true;
                host.vocalChain.preset = "Podcast voice";
                host.vocalChain.amount = 0.8f;
                host.inputChannel = i;
                host.armed = i == 0;
            }
            auto& music = p.addTrack (TrackType::Audio, "Intro music", TrackRole::Fx);
            music.volumeDb = -10;
            p.mastering.preset = "Gentle worship";
            p.mastering.targetLufs = -16.0f; // spoken word sits a little lower
            p.sections.clear();
        }
        else if (name == "Choir rehearsal track")
        {
            p.bpm = 80; p.key = Key::fromString ("F major"); p.style = "worship_ballad";
            for (auto part : { "Soprano", "Alto", "Tenor", "Bass" })
            {
                auto& t = p.addTrack (TrackType::Midi, part, TrackRole::Choir);
                t.instrument.id = "grand_piano";
                t.pan = juce::String (part) == "Soprano" ? -0.5f : juce::String (part) == "Alto" ? -0.2f : juce::String (part) == "Tenor" ? 0.2f : 0.5f;
            }
            {
                auto& choir = vocalTrack (p, "Choir recording", TrackRole::Choir, "Choir stack", reverbId, 0);
                choir.inputChannels = 2;
                choir.armed = true;
            }
            p.mastering.enabled = false; // rehearsal tracks stay natural
        }
        return p;
    }
}

} // namespace studio
