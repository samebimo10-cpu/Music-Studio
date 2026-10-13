#include "Session.h"
#include "core/vocal/VocalTools.h"
#include "core/vocal/VocalChain.h"
#include "core/mastering/MasteringChain.h"

namespace studio
{

//==============================================================================
AppSettings::AppSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Studio";
    o.filenameSuffix = "settings";
    o.folderName = "Studio";
    o.osxLibrarySubFolder = "Application Support";
    file = std::make_unique<juce::PropertiesFile> (o);
}

juce::File AppSettings::projectsFolder()
{
    auto f = juce::File (file->getValue ("projectsFolder", store::defaultProjectsFolder().getFullPathName()));
    f.createDirectory();
    return f;
}

void AppSettings::setProjectsFolder (const juce::File& f) { file->setValue ("projectsFolder", f.getFullPathName()); }

juce::StringArray AppSettings::recentProjects()
{
    juce::StringArray r;
    r.addTokens (file->getValue ("recent"), "|", "");
    r.removeEmptyStrings();
    juce::StringArray existing;
    for (auto& p : r) if (juce::File (p).getChildFile ("project.json").existsAsFile()) existing.add (p);
    return existing;
}

void AppSettings::addRecent (const juce::File& f)
{
    auto r = recentProjects();
    r.removeString (f.getFullPathName());
    r.insert (0, f.getFullPathName());
    while (r.size() > 20) r.remove (r.size() - 1);
    file->setValue ("recent", r.joinIntoString ("|"));
}

bool AppSettings::guidedByDefault() { return file->getBoolValue ("guided", true); }
float AppSettings::uiScale() { return (float) file->getDoubleValue ("uiScale", 1.0); }

//==============================================================================
Session::Session()
{
    std::unique_ptr<juce::XmlElement> savedDevice (appSettings.props().getXmlValue ("audioDevice"));
    audio.initialiseDevice (savedDevice.get(), 2, 2);
    audio.onRecordingFinished = [this] (std::vector<RecordedTakes> results)
    {
        if (results.empty()) { status ("Nothing was recorded."); return; }
        perform ("Record", [&] (Project& p)
        {
            for (auto& r : results)
                if (auto* t = p.findTrack (r.trackId))
                {
                    // Keep paths relative to the project.
                    auto takes = r.takes;
                    for (auto& tk : takes) tk.audioFile = store::relativePath (folder, juce::File (tk.audioFile));
                    vocal::addTakes (p, *t, takes);
                }
        });
        status (juce::String ((int) results.size()) + " track(s) recorded. Every take is kept for comping.");
    };

    lib.open (LibraryIndex::defaultLocation());
    startTimer (1000);
}

Session::~Session()
{
    stopTimer();
    if (auto* dev = audio.getDeviceManager().createStateXml().release())
    {
        appSettings.props().setValue ("audioDevice", dev);
        delete dev;
    }
    if (hasProject())
    {
        if (dirty) store::writeAutosave (proj, folder);
        else store::markSessionClosed (folder);
    }
    appSettings.props().saveIfNeeded();
}

juce::Result Session::newProject (Project p)
{
    stopAll();
    juce::File created;
    auto r = store::create (appSettings.projectsFolder(), p, created);
    if (r.failed()) return r;
    if (hasProject()) store::markSessionClosed (folder);
    folder = created;
    proj = p;
    undoHistory.reset (proj);
    dirty = false;
    store::markSessionOpen (folder);
    appSettings.addRecent (folder);
    selectedTrackId = proj.tracks.empty() ? juce::String() : proj.tracks.front().id;
    rebuild();
    changed (true);
    return juce::Result::ok();
}

juce::Result Session::open (const juce::File& f)
{
    stopAll();
    Project p;
    auto r = store::load (f, p);
    if (r.failed()) return r;
    if (hasProject() && folder != f) store::markSessionClosed (folder);
    folder = f;
    proj = p;
    undoHistory.reset (proj);
    dirty = false;
    store::markSessionOpen (folder);
    appSettings.addRecent (folder);
    selectedTrackId = proj.tracks.empty() ? juce::String() : proj.tracks.front().id;
    rebuild();
    changed (true);
    return juce::Result::ok();
}

juce::Result Session::save()
{
    if (! hasProject()) return juce::Result::fail ("No project open.");
    auto r = store::save (proj, folder);
    if (r.wasOk())
    {
        dirty = false;
        store::autosaveFile (folder).deleteFile();
        store::saveSnapshot (proj, folder, "Auto save");
        store::pruneSnapshots (folder);
        status ("Saved.");
        sendChangeMessage();
    }
    return r;
}

juce::Result Session::saveAs (const juce::File& parent, const juce::String& name)
{
    auto old = folder;
    proj.name = name;
    juce::File created;
    Project copy = proj;
    // Make audio paths absolute so the new folder can copy them in.
    for (auto& t : copy.tracks)
    {
        for (auto& c : t.clips) if (c.audioFile.isNotEmpty()) c.audioFile = store::resolve (old, c.audioFile).getFullPathName();
        for (auto& tk : t.takes) if (tk.audioFile.isNotEmpty()) tk.audioFile = store::resolve (old, tk.audioFile).getFullPathName();
    }
    auto r = store::create (parent, copy, created);
    if (r.failed()) return r;
    for (auto sub : { "audio", "takes", "release" })
        old.getChildFile (sub).copyDirectoryTo (created.getChildFile (sub));
    return open (created);
}

void Session::rebuild()
{
    audio.setProject (proj, folder);
}

void Session::changed (bool structural)
{
    lastChangeWasStructural = structural;
    sendChangeMessage();
}

void Session::perform (const juce::String& description, const std::function<void (Project&)>& fn, bool rebuildEngine)
{
    fn (proj);
    undoHistory.commit (proj, description);
    dirty = true;
    if (rebuildEngine) rebuild();
    changed (true);
}

void Session::setTrackVolume (const juce::String& id, float db, bool gestureEnd)
{
    if (auto* t = proj.findTrack (id)) t->volumeDb = db;
    if (auto* g = audio.getGraph()) g->setTrackVolume (id, db);
    dirty = true;
    if (gestureEnd) undoHistory.commit (proj, "Volume");
    changed (false);
}

void Session::setTrackPan (const juce::String& id, float pan, bool gestureEnd)
{
    if (auto* t = proj.findTrack (id)) t->pan = pan;
    if (auto* g = audio.getGraph()) g->setTrackPan (id, pan);
    dirty = true;
    if (gestureEnd) undoHistory.commit (proj, "Pan");
    changed (false);
}

void Session::setTrackMute (const juce::String& id, bool m)
{
    if (auto* t = proj.findTrack (id)) t->mute = m;
    if (auto* g = audio.getGraph()) g->setTrackMute (id, m);
    undoHistory.commit (proj, m ? "Mute" : "Unmute");
    dirty = true;
    changed (false);
}

void Session::setTrackSolo (const juce::String& id, bool s)
{
    if (auto* t = proj.findTrack (id)) t->solo = s;
    if (auto* g = audio.getGraph()) g->setTrackSolo (id, s);
    undoHistory.commit (proj, s ? "Solo" : "Unsolo");
    dirty = true;
    changed (false);
}

void Session::setEffectParam (const juce::String& trackId, const juce::String& slotId, const juce::String& param, float value, bool gestureEnd)
{
    if (auto* t = proj.findTrack (trackId))
        for (auto& e : t->effects)
            if (e.id == slotId) e.params[param] = value;
    if (auto* g = audio.getGraph())
        if (auto* fx = g->findEffect (trackId, slotId)) fx->setParam (param, value);
    dirty = true;
    if (gestureEnd) undoHistory.commit (proj, "Effect setting");
    changed (false);
}

void Session::setVocalChain (const juce::String& trackId, const VocalChainSettings& s, bool gestureEnd)
{
    auto* t = proj.findTrack (trackId);
    if (t == nullptr) return;
    const bool wasEnabled = t->vocalChain.enabled;
    t->vocalChain = s;
    dirty = true;
    if (wasEnabled != s.enabled) { rebuild(); undoHistory.commit (proj, "Vocal chain"); changed (true); return; }
    if (auto* g = audio.getGraph())
        if (auto* vc = g->findVocalChain (trackId)) vc->applySettings (s, proj.keyAt (0));
    if (gestureEnd) undoHistory.commit (proj, "Vocal chain");
    changed (false);
}

void Session::setMastering (const MasteringSettings& s, bool gestureEnd)
{
    proj.mastering = s;
    if (auto* g = audio.getGraph())
    {
        if (auto* m = g->getMasteringChain()) m->applySettings (s);
        g->setMasteringEnabled (s.enabled);
    }
    dirty = true;
    if (gestureEnd) undoHistory.commit (proj, "Mastering");
    changed (false);
}

bool Session::undo()
{
    Project p;
    if (! undoHistory.undo (p)) return false;
    proj = p;
    dirty = true;
    rebuild();
    changed (true);
    status ("Undone.");
    return true;
}

bool Session::redo()
{
    Project p;
    if (! undoHistory.redo (p)) return false;
    proj = p;
    dirty = true;
    rebuild();
    changed (true);
    status ("Redone.");
    return true;
}

void Session::select (const juce::String& trackId, const juce::String& clipId)
{
    selectedTrackId = trackId;
    selectedClipId = clipId;
    if (auto* t = proj.findTrack (trackId); t != nullptr && t->type == TrackType::Midi)
        audio.setLiveTrack (trackId);
    changed (false);
}

void Session::togglePlay()
{
    if (audio.isPlaying()) audio.stop();
    else audio.play();
    changed (false);
}

juce::String Session::toggleRecord()
{
    if (audio.isRecording()) { audio.stopRecording(); audio.stop(); changed (false); return {}; }
    auto err = audio.startRecording (recordSettings, proj, store::takesFolder (folder));
    if (err.isNotEmpty()) status (err);
    changed (false);
    return err;
}

void Session::stopAll()
{
    if (audio.isRecording()) audio.stopRecording();
    audio.stop();
}

void Session::timerCallback()
{
    // Autosave every 30 s while there are unsaved changes.
    if (hasProject() && dirty && ++autosaveCounter >= 30)
    {
        autosaveCounter = 0;
        store::writeAutosave (proj, folder);
    }
}

} // namespace studio
