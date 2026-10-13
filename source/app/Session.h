#pragma once

#include "AppCommon.h"
#include "core/engine/AudioEngine.h"
#include "core/library/LibraryIndex.h"
#include "core/project/ProjectStore.h"

namespace studio
{

/** App-wide settings stored in the user's app data folder. */
class AppSettings
{
public:
    AppSettings();
    juce::PropertiesFile& props() { return *file; }

    juce::File projectsFolder();
    void setProjectsFolder (const juce::File&);
    juce::StringArray recentProjects();
    void addRecent (const juce::File& folder);
    bool guidedByDefault();
    float uiScale();

private:
    std::unique_ptr<juce::PropertiesFile> file;
};

/** The open song and everything that acts on it: the model, the engine, undo,
    autosave and the library. Screens listen for changes and call perform() to edit. */
class Session : public juce::ChangeBroadcaster,
                private juce::Timer
{
public:
    Session();
    ~Session() override;

    // ---- project lifecycle -------------------------------------------------------
    juce::Result newProject (Project p);
    juce::Result open (const juce::File& folder);
    juce::Result save();
    juce::Result saveAs (const juce::File& parent, const juce::String& name);
    bool hasProject() const { return folder.isDirectory(); }
    const juce::File& getFolder() const { return folder; }
    bool isDirty() const { return dirty; }

    const Project& project() const { return proj; }

    /** Edits the project: runs fn, records an undo step, rebuilds the engine graph. */
    void perform (const juce::String& description, const std::function<void (Project&)>& fn, bool rebuildEngine = true);

    /** Live control changes (faders, pans): update model + graph immediately, undo on gesture end. */
    void setTrackVolume (const juce::String& trackId, float db, bool gestureEnd);
    void setTrackPan (const juce::String& trackId, float pan, bool gestureEnd);
    void setTrackMute (const juce::String& trackId, bool);
    void setTrackSolo (const juce::String& trackId, bool);
    void setEffectParam (const juce::String& trackId, const juce::String& slotId, const juce::String& param, float value, bool gestureEnd);
    void setVocalChain (const juce::String& trackId, const VocalChainSettings& s, bool gestureEnd);
    void setMastering (const MasteringSettings& s, bool gestureEnd);

    bool undo();
    bool redo();
    UndoHistory& history() { return undoHistory; }

    // ---- selection (shared between screens) -----------------------------------------
    juce::String selectedTrackId, selectedClipId, selectedSectionId;
    void select (const juce::String& trackId, const juce::String& clipId = {});

    // ---- engine ------------------------------------------------------------------------
    AudioEngine& engine() { return audio; }
    void togglePlay();
    juce::String toggleRecord();
    void stopAll();
    AudioEngine::RecordSettings recordSettings;

    // ---- library and settings -------------------------------------------------------------
    LibraryIndex& library() { return lib; }
    AppSettings& settings() { return appSettings; }

    /** Listeners get this for every change; `structural` is false for live control tweaks. */
    bool lastChangeWasStructural = true;

    std::function<void (const juce::String&)> onStatus;   // status bar messages
    void status (const juce::String& s) { if (onStatus) onStatus (s); }

private:
    void timerCallback() override;
    void changed (bool structural);
    void rebuild();

    AppSettings appSettings;
    AudioEngine audio;
    LibraryIndex lib;
    Project proj;
    juce::File folder;
    UndoHistory undoHistory;
    bool dirty = false;
    juce::String pendingGesture;
    int autosaveCounter = 0;
};

} // namespace studio
