#pragma once

#include <deque>
#include "../model/Project.h"

namespace studio
{

/** A project is a self-contained folder:

        My Song.studio/
          project.json   tempo, key, tracks, sections, routing, automation
          audio/         recorded and imported audio (WAV, 32-bit float)
          midi/          MIDI clips (exported copies)
          takes/         every recorded take, kept for comping
          presets/       effect and instrument settings used
          release/       metadata.json, artwork, lyrics, exports
          history/       snapshots for undo and versions
*/
namespace store
{
    juce::File projectFile (const juce::File& folder);
    juce::File audioFolder (const juce::File& folder);
    juce::File midiFolder (const juce::File& folder);
    juce::File takesFolder (const juce::File& folder);
    juce::File presetsFolder (const juce::File& folder);
    juce::File releaseFolder (const juce::File& folder);
    juce::File historyFolder (const juce::File& folder);
    juce::File exportsFolder (const juce::File& folder);

    /** Default location for new projects (Documents/Studio Projects). */
    juce::File defaultProjectsFolder();

    /** Creates the folder layout for a new project ("My Song" -> "My Song.studio"). */
    juce::Result create (const juce::File& parent, Project& project, juce::File& folderOut);

    /** Saves atomically (temp file + rename). Also writes release/metadata.json. */
    juce::Result save (Project& project, const juce::File& folder);
    juce::Result load (const juce::File& folder, Project& projectOut);

    /** Stores audio paths relative to the folder when they live inside it. */
    juce::String relativePath (const juce::File& folder, const juce::File& file);
    juce::File resolve (const juce::File& folder, const juce::String& path);

    /** Copies an audio file into audio/ and adds it as a clip on a new (or given) track. */
    juce::Result importAudio (Project& p, const juce::File& folder, const juce::File& source, double atBeat,
                              const juce::String& trackId = {}, juce::String* newTrackIdOut = nullptr);

    // ---- snapshots and versions -------------------------------------------------------
    struct Snapshot
    {
        juce::File file;
        juce::String name;
        juce::Time time;
    };
    juce::Result saveSnapshot (const Project& p, const juce::File& folder, const juce::String& name);
    std::vector<Snapshot> listSnapshots (const juce::File& folder);
    juce::Result loadSnapshot (const Snapshot& s, Project& out);
    void pruneSnapshots (const juce::File& folder, int keepAutomatic = 50);

    // ---- autosave and crash recovery ------------------------------------------------------
    juce::File autosaveFile (const juce::File& folder);
    juce::File lockFile (const juce::File& folder);
    juce::Result writeAutosave (const Project& p, const juce::File& folder);
    /** True when the last session did not close cleanly and the autosave is newer than the save. */
    bool needsRecovery (const juce::File& folder);
    juce::Result recover (const juce::File& folder, Project& out);
    void markSessionOpen (const juce::File& folder);
    void markSessionClosed (const juce::File& folder);

    // ---- sync manifest ------------------------------------------------------------------
    /** File manifest with content hashes, so cloud sync only uploads changed audio. */
    struct ManifestEntry { juce::String path; juce::int64 size = 0; juce::String sha256; };
    std::vector<ManifestEntry> buildManifest (const juce::File& folder);
    juce::var manifestToVar (const std::vector<ManifestEntry>& m);
    std::vector<ManifestEntry> manifestFromVar (const juce::var& v);
    /** Entries in `now` that are new or changed compared with `before`. */
    std::vector<ManifestEntry> changedSince (const std::vector<ManifestEntry>& before, const std::vector<ManifestEntry>& now);
}

//==============================================================================
/** Unlimited undo over whole-project snapshots (JSON). Cheap enough for songs,
    and every destructive action is undoable without per-command code. */
class UndoHistory
{
public:
    void reset (const Project& p);
    /** Call after a change: stores the new state with a description ("Delete clip"). */
    void commit (const Project& p, const juce::String& description);

    bool canUndo() const { return position > 0; }
    bool canRedo() const { return position + 1 < (int) states.size(); }
    juce::String undoDescription() const;
    juce::String redoDescription() const;

    /** Returns the restored project, or false. */
    bool undo (Project& out);
    bool redo (Project& out);

    int size() const { return (int) states.size(); }
    juce::StringArray descriptions() const;

private:
    struct State { juce::String json; juce::String description; };
    std::deque<State> states;
    int position = -1;
    size_t maxBytes = 512u * 1024u * 1024u;
};

//==============================================================================
namespace templates
{
    juce::StringArray names(); // "Worship single", "Praise medley", "Podcast", "Choir rehearsal track"
    Project create (const juce::String& name);
    /** A blank project with sensible buses. */
    Project blank (const juce::String& songName = "Untitled");
}

} // namespace studio
