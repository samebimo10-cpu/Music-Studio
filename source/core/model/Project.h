#pragma once

#include <map>
#include <vector>
#include <juce_core/juce_core.h>
#include "../theory/MusicTheory.h"

namespace studio
{

/** Generates short unique ids for tracks, clips, takes and sections. */
juce::String makeId (const juce::String& prefix);

//==============================================================================
enum class TrackType { Audio, Midi, Bus };

/** What a track does in the song. Auto-mix, the mix assistant and stem export use it. */
enum class TrackRole
{
    Unknown, LeadVocal, BackingVocal, Choir, Kick, Snare, HiHat, Drums, Percussion,
    Bass, Keys, Guitar, Pad, Strings, Brass, Lead, Fx, Click
};

juce::String toString (TrackType);
juce::String toString (TrackRole);
TrackType trackTypeFromString (const juce::String&);
TrackRole trackRoleFromString (const juce::String&);
bool isVocalRole (TrackRole);

enum class SectionType { Intro, Verse, PreChorus, Chorus, Bridge, Vamp, Instrumental, Outro };
juce::String toString (SectionType);
SectionType sectionTypeFromString (const juce::String&);

//==============================================================================
struct MidiNote
{
    int pitch = 60;
    float velocity = 0.8f;   // 0..1
    double startBeat = 0;    // relative to the clip start
    double lengthBeats = 1;

    bool operator== (const MidiNote& o) const noexcept
    {
        return pitch == o.pitch && velocity == o.velocity && startBeat == o.startBeat && lengthBeats == o.lengthBeats;
    }
};

struct Clip
{
    juce::String id;
    juce::String name;
    double startBeat = 0;
    double lengthBeats = 4;

    // MIDI clips
    bool isMidi = false;
    std::vector<MidiNote> notes;

    // Audio clips: the file is relative to the project folder.
    juce::String audioFile;
    double sourceOffsetSeconds = 0;
    double fadeInBeats = 0;
    double fadeOutBeats = 0;
    float gainDb = 0;
    double stretchRatio = 1.0;   // >1 plays slower (longer)
    double pitchSemitones = 0;
    juce::String takeId;         // set when the clip came from comping

    double endBeat() const noexcept { return startBeat + lengthBeats; }
};

/** A recorded take. Loop recording writes one file and slices it into takes by offset. */
struct Take
{
    juce::String id;
    juce::String audioFile;
    double startBeat = 0;
    double lengthBeats = 0;
    double sourceOffsetSeconds = 0;
    juce::String recordedAt;
    int rating = 0; // 0..5, set by the user while comping
};

/** One region of the comp: which take plays between two beats. */
struct CompSegment
{
    juce::String takeId;
    double startBeat = 0;
    double endBeat = 0;
};

struct EffectSlot
{
    juce::String id;
    juce::String type;            // "eq", "compressor", "reverb", ... or "plugin"
    bool bypassed = false;
    std::map<juce::String, float> params;
    juce::String pluginIdentifier;   // for third-party plugins
    juce::String pluginState;        // base64
    juce::String sidechainTrackId;   // compressor sidechain source
};

struct Send
{
    juce::String busId;
    float levelDb = -12.0f;
    bool preFader = false;
};

struct AutomationPoint
{
    double beat = 0;
    float value = 0;
};

/** Parameter path: "volume", "pan", or "fx/<slotId>/<param>". */
struct AutomationLane
{
    juce::String parameter;
    std::vector<AutomationPoint> points;
    bool enabled = true;

    /** Linear interpolation between points; returns fallback when empty. */
    float valueAt (double beat, float fallback) const;
};

struct InstrumentSpec
{
    juce::String id;   // "grand_piano", "drums_afro", ... or "sampler:<packId>/<instrument>" or "plugin:<id>"
    std::map<juce::String, float> params;
};

/** The 10-step vocal chain from the spec, as stored in the project. */
struct VocalChainSettings
{
    bool enabled = false;
    juce::String preset = "Worship lead";
    float amount = 0.7f;
    std::map<juce::String, bool> bypass;            // step id -> bypassed
    std::map<juce::String, float> params;           // "step.param" -> value
};

/** Remembers how the beat generator made a track, so parts can be regenerated,
    made simpler/busier, given a new feel or varied ("more like this"). */
struct GeneratorInfo
{
    juce::String part;          // "drums", "bass", "keys", "guitar", "percussion", "pad", "lead"; empty = not generated
    juce::int64 seed = 0;       // structural choices
    juce::int64 variation = 0;  // small details ("more like this" changes only this)
    float density = 0.5f;       // simpler (0) .. busier (1)
    int feel = 0;               // pattern/feel variant ("swap feel" cycles this)
};

struct Track
{
    juce::String id;
    juce::String name;
    TrackType type = TrackType::Audio;
    TrackRole role = TrackRole::Unknown;
    juce::uint32 colour = 0xff4f8cff;
    juce::String folder;                // track folder (group) name
    juce::String outputBusId;           // empty = master
    float volumeDb = 0;
    float pan = 0;                      // -1..1
    bool mute = false;
    bool solo = false;
    bool armed = false;
    bool monitorInput = false;
    int inputChannel = 0;               // first input channel
    int inputChannels = 1;              // 1 = mono, 2 = stereo
    InstrumentSpec instrument;          // MIDI tracks
    std::vector<EffectSlot> effects;    // inserts, in order
    std::vector<Send> sends;
    std::vector<Clip> clips;
    std::vector<Take> takes;
    std::vector<CompSegment> comp;
    std::vector<AutomationLane> automation;
    VocalChainSettings vocalChain;
    GeneratorInfo generator;

    Clip* findClip (const juce::String& clipId);
    AutomationLane& automationFor (const juce::String& parameter);
};

struct Section
{
    juce::String id;
    SectionType type = SectionType::Verse;
    juce::String name;
    double startBeat = 0;
    double lengthBeats = 32;
    float energy = 0.5f;   // 0..1, drives generator density and vamp lifts

    double endBeat() const noexcept { return startBeat + lengthBeats; }
};

struct TempoEvent { double beat = 0; double bpm = 120; };

/** Chord track entry (written by the generator, chord detection or the user). */
struct ChordEvent
{
    double beat = 0;
    double lengthBeats = 4;
    juce::String chord;   // "C", "Am7", "F/A"
};
struct KeyEvent   { double beat = 0; Key key; };
struct Marker     { double beat = 0; juce::String name; };

/** A comment pinned to a moment on the timeline (collaboration). */
struct TimelineComment
{
    juce::String id;
    double beat = 0;
    juce::String author;
    juce::String text;
    juce::String createdAt;
    bool resolved = false;
};

//==============================================================================
struct Contributor
{
    juce::String name;
    juce::StringArray roles; // "songwriter", "composer", "producer", "featured artist"
};

struct Split
{
    juce::String name;
    juce::String role;
    double percent = 0;
    juce::String email;
};

struct SyncedLyricLine
{
    double timeSeconds = 0;
    juce::String text;
};

struct ReleaseMetadata
{
    juce::String releaseType = "single";     // single, ep, album
    juce::String title;
    juce::String primaryArtist;
    juce::StringArray featuredArtists;
    juce::StringArray songwriters;
    juce::StringArray composers;
    juce::StringArray producers;
    juce::String genre = "Christian & Gospel";
    juce::String subgenre;
    juce::String language = "English";
    bool explicitContent = false;
    juce::String releaseDate;                 // YYYY-MM-DD
    juce::String originalReleaseDate;
    juce::String isrc;
    juce::String upc;
    juce::String label;
    juce::String copyrightLine;               // "2026 Artist Name"
    juce::String lyrics;
    std::vector<SyncedLyricLine> syncedLyrics;
    juce::String artworkFile;                 // relative to the project folder
    std::vector<Split> splits;
};

/** The lyrics pad: text in sections, chord chart lines and voice memos. */
struct LyricLine
{
    juce::String text;
    juce::String chords;       // chord chart above the line, e.g. "C   G/B   Am"
    juce::String memoFile;     // voice memo attached to the line
};

struct LyricSection
{
    juce::String name;         // "Verse 1"
    std::vector<LyricLine> lines;
};

struct MasteringSettings
{
    juce::String preset = "Warm";
    float targetLufs = -14.0f;
    float ceilingDbtp = -1.0f;
    std::map<juce::String, float> params;
    bool enabled = true;
};

//==============================================================================
struct Project
{
    juce::String name = "Untitled";
    juce::String id;
    double bpm = 120;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    Key key;
    juce::String style;       // style preset id used by the generator
    juce::String mood = "joyful";
    double sampleRate = 48000;
    bool guidedMode = true;
    int guidedStep = 0;
    juce::String createdAt;
    juce::String modifiedAt;

    std::vector<TempoEvent> tempoEvents;   // after beat 0; bpm above is the start tempo
    std::vector<KeyEvent> keyEvents;
    std::vector<Section> sections;
    std::vector<ChordEvent> chords;
    std::vector<Track> tracks;
    std::vector<Marker> markers;
    std::vector<TimelineComment> comments;
    std::vector<LyricSection> lyricSheet;
    MasteringSettings mastering;
    ReleaseMetadata release;

    // Track helpers
    Track* findTrack (const juce::String& trackId);
    const Track* findTrack (const juce::String& trackId) const;
    Track& addTrack (TrackType type, const juce::String& name, TrackRole role = TrackRole::Unknown);
    bool removeTrack (const juce::String& trackId);
    std::vector<const Track*> buses() const;

    Section* findSection (const juce::String& sectionId);
    const Section* sectionAt (double beat) const;
    Key keyAt (double beat) const;
    juce::String chordAt (double beat) const;
    double beatsPerBar() const noexcept { return timeSigNumerator * 4.0 / timeSigDenominator; }

    /** End of the last clip or section, in beats. */
    double lengthInBeats() const;

    // JSON
    juce::var toVar() const;
    static Project fromVar (const juce::var&);
    juce::String toJson() const;
    static Project fromJson (const juce::String&);
};

} // namespace studio
