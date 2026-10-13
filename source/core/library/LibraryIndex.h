#pragma once

#include <memory>
#include "../model/Project.h"

struct sqlite3;

namespace studio
{

struct LoopInfo
{
    juce::int64 id = 0;
    juce::File file;
    juce::String name, style, instrument, tags;
    double bpm = 0;          // 0 = unknown / one-shot
    juce::String key;        // "" = unpitched
    double lengthSeconds = 0;
    juce::String source;     // "starter", "pack:<id>", "user"
};

struct LoopQuery
{
    juce::String text;       // matches name, tags, instrument
    juce::String style;
    juce::String instrument;
    juce::String key;        // compatible keys are ranked first
    double bpmMin = 0, bpmMax = 0;
    int limit = 200;
};

/** The sound library index (SQLite): loops, one-shots and presets, searchable by
    style, tempo, key and instrument. Lives in the app data folder. */
class LibraryIndex
{
public:
    LibraryIndex();
    ~LibraryIndex();

    juce::Result open (const juce::File& dbFile);
    void close();
    static juce::File defaultLocation();

    /** Indexes audio files in a folder. Tempo/key come from the file name
        ("Afro_Shaker_105bpm_Am.wav") or are detected from the audio. */
    int scanFolder (const juce::File& folder, const juce::String& source, std::function<bool (float)> progress = {});

    juce::Result addLoop (LoopInfo& info);
    bool removeMissing();
    std::vector<LoopInfo> search (const LoopQuery& q) const;
    int count() const;
    juce::StringArray styles() const;
    juce::StringArray instrumentNames() const;

    /** Parses tempo and key from a file name; returns false if nothing found. */
    static bool parseFileName (const juce::String& name, double& bpm, juce::String& key);

private:
    sqlite3* db = nullptr;
};

namespace library
{
    /** How a loop should be stretched/transposed to fit the song. */
    struct LoopMatch
    {
        double stretchRatio = 1.0;   // >1 slower
        double semitones = 0;        // nearest transposition (-6..+6)
        double lengthBeats = 4;
    };
    LoopMatch matchLoop (const LoopInfo& loop, double songBpm, const Key& songKey);

    /** Drag-in: adds the loop as a clip matched to the song's tempo and key. */
    juce::Result addLoopToProject (Project& p, const juce::File& projectFolder, const LoopInfo& loop, double atBeat,
                                   const juce::String& trackId = {});

    /** Renders royalty-free starter loops from the beat generator (owned content, no licences needed). */
    int generateStarterLoops (const juce::File& outFolder, double sampleRate = 44100, std::function<bool (float)> progress = {});
}

} // namespace studio
