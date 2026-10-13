#include "LibraryIndex.h"
#include <sqlite3.h>
#include "../analysis/AudioAnalysis.h"
#include "../beatgen/BeatGenerator.h"
#include "../engine/ClipCache.h"
#include "../engine/OfflineRenderer.h"
#include "../model/TempoMap.h"
#include "../project/ProjectStore.h"

namespace studio
{

namespace
{
    struct Statement
    {
        Statement (sqlite3* db, const char* sql) { sqlite3_prepare_v2 (db, sql, -1, &stmt, nullptr); }
        ~Statement() { sqlite3_finalize (stmt); }
        void bind (int i, const juce::String& s) { sqlite3_bind_text (stmt, i, s.toRawUTF8(), -1, SQLITE_TRANSIENT); }
        void bind (int i, double v) { sqlite3_bind_double (stmt, i, v); }
        void bind (int i, juce::int64 v) { sqlite3_bind_int64 (stmt, i, v); }
        juce::String text (int col) const { auto* t = sqlite3_column_text (stmt, col); return t != nullptr ? juce::String::fromUTF8 ((const char*) t) : juce::String(); }
        double real (int col) const { return sqlite3_column_double (stmt, col); }
        juce::int64 integer (int col) const { return sqlite3_column_int64 (stmt, col); }
        bool step() { return sqlite3_step (stmt) == SQLITE_ROW; }
        bool exec() { const int r = sqlite3_step (stmt); return r == SQLITE_DONE || r == SQLITE_ROW; }
        sqlite3_stmt* stmt = nullptr;
    };

    LoopInfo rowToLoop (const Statement& s)
    {
        LoopInfo l;
        l.id = s.integer (0);
        l.file = juce::File (s.text (1));
        l.name = s.text (2);
        l.style = s.text (3);
        l.instrument = s.text (4);
        l.tags = s.text (5);
        l.bpm = s.real (6);
        l.key = s.text (7);
        l.lengthSeconds = s.real (8);
        l.source = s.text (9);
        return l;
    }
}

LibraryIndex::LibraryIndex() = default;
LibraryIndex::~LibraryIndex() { close(); }

juce::File LibraryIndex::defaultLocation()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Studio").getChildFile ("library.db");
}

juce::Result LibraryIndex::open (const juce::File& f)
{
    close();
    f.getParentDirectory().createDirectory();
    if (sqlite3_open (f.getFullPathName().toRawUTF8(), &db) != SQLITE_OK)
        return juce::Result::fail ("Could not open the library database.");
    const char* schema =
        "CREATE TABLE IF NOT EXISTS loops ("
        " id INTEGER PRIMARY KEY, path TEXT UNIQUE, name TEXT, style TEXT, instrument TEXT, tags TEXT,"
        " bpm REAL, musical_key TEXT, length REAL, source TEXT, added INTEGER);"
        "CREATE INDEX IF NOT EXISTS loops_style ON loops(style);"
        "CREATE INDEX IF NOT EXISTS loops_bpm ON loops(bpm);"
        "CREATE TABLE IF NOT EXISTS presets (id INTEGER PRIMARY KEY, kind TEXT, name TEXT, path TEXT UNIQUE);";
    char* err = nullptr;
    if (sqlite3_exec (db, schema, nullptr, nullptr, &err) != SQLITE_OK)
    {
        juce::String msg (err != nullptr ? err : "schema error");
        sqlite3_free (err);
        return juce::Result::fail (msg);
    }
    return juce::Result::ok();
}

void LibraryIndex::close()
{
    if (db != nullptr) sqlite3_close (db);
    db = nullptr;
}

bool LibraryIndex::parseFileName (const juce::String& nameIn, double& bpm, juce::String& key)
{
    bool found = false;
    auto tokens = juce::StringArray::fromTokens (nameIn.replaceCharacters ("-. ", "___"), "_", "");
    for (auto& t : tokens)
    {
        auto lower = t.toLowerCase();
        if (lower.endsWith ("bpm") && lower.dropLastCharacters (3).containsOnly ("0123456789."))
        {
            bpm = lower.dropLastCharacters (3).getDoubleValue();
            found = true;
        }
        else if (t.length() <= 4 && t.isNotEmpty() && juce::String ("ABCDEFG").containsChar (t[0]))
        {
            // "Am", "C#", "Ebm", "F#min"
            auto rest = t.substring (1);
            if (rest.startsWith ("#") || rest.startsWith ("b")) rest = rest.substring (1);
            if (rest.isEmpty() || rest == "m" || rest == "min" || rest == "maj")
            {
                key = t;
                found = true;
            }
        }
    }
    return found;
}

juce::Result LibraryIndex::addLoop (LoopInfo& info)
{
    if (db == nullptr) return juce::Result::fail ("Library not open.");
    Statement s (db, "INSERT OR REPLACE INTO loops (path, name, style, instrument, tags, bpm, musical_key, length, source, added)"
                     " VALUES (?,?,?,?,?,?,?,?,?,?)");
    s.bind (1, info.file.getFullPathName());
    s.bind (2, info.name);
    s.bind (3, info.style);
    s.bind (4, info.instrument);
    s.bind (5, info.tags);
    s.bind (6, info.bpm);
    s.bind (7, info.key);
    s.bind (8, info.lengthSeconds);
    s.bind (9, info.source);
    s.bind (10, (juce::int64) juce::Time::currentTimeMillis());
    if (! s.exec()) return juce::Result::fail (sqlite3_errmsg (db));
    info.id = sqlite3_last_insert_rowid (db);
    return juce::Result::ok();
}

int LibraryIndex::scanFolder (const juce::File& folder, const juce::String& source, std::function<bool (float)> progress)
{
    if (db == nullptr) return 0;
    auto files = folder.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    int added = 0;
    sqlite3_exec (db, "BEGIN", nullptr, nullptr, nullptr);
    for (int i = 0; i < files.size(); ++i)
    {
        if (progress && ! progress ((float) i / (float) files.size())) break;
        auto& f = files.getReference (i);
        LoopInfo info;
        info.file = f;
        info.name = f.getFileNameWithoutExtension().replaceCharacter ('_', ' ');
        info.source = source;
        // Folder names give style and instrument ("Afrobeats/Shaker/...").
        auto rel = f.getRelativePathFrom (folder);
        auto parts = juce::StringArray::fromTokens (rel, "/\\", "");
        if (parts.size() >= 2) info.style = parts[0];
        if (parts.size() >= 3) info.instrument = parts[1];
        for (auto inst : { "drums", "percussion", "bass", "keys", "guitar", "pad", "shaker", "shekere", "conga", "talking", "log", "piano", "organ", "vocal" })
            if (f.getFileName().containsIgnoreCase (inst) && info.instrument.isEmpty()) info.instrument = inst;

        juce::AudioBuffer<float> audio;
        double rate = 0;
        if (! ClipCache::readFile (f, audio, rate)) continue;
        info.lengthSeconds = audio.getNumSamples() / rate;

        double bpm = 0;
        juce::String key;
        parseFileName (f.getFileNameWithoutExtension(), bpm, key);
        if (bpm <= 0 && info.lengthSeconds > 1.5)
        {
            auto t = analysis::detectTempo (audio, rate);
            if (t.confidence > 0.2f) bpm = t.bpm;
        }
        if (key.isEmpty() && info.instrument != "drums" && info.instrument != "percussion" && info.lengthSeconds > 1.0)
        {
            auto k = analysis::detectKey (audio, rate);
            if (k.confidence > 0.35f)
                key = theory::pitchClassName (k.key.tonic) + (k.key.isMinor() ? "m" : "");
        }
        info.bpm = bpm;
        info.key = key;
        if (addLoop (info).wasOk()) ++added;
    }
    sqlite3_exec (db, "COMMIT", nullptr, nullptr, nullptr);
    return added;
}

bool LibraryIndex::removeMissing()
{
    if (db == nullptr) return false;
    std::vector<juce::int64> missing;
    {
        Statement s (db, "SELECT id, path FROM loops");
        while (s.step())
            if (! juce::File (s.text (1)).existsAsFile()) missing.push_back (s.integer (0));
    }
    for (auto id : missing)
    {
        Statement d (db, "DELETE FROM loops WHERE id = ?");
        d.bind (1, id);
        d.exec();
    }
    return ! missing.empty();
}

std::vector<LoopInfo> LibraryIndex::search (const LoopQuery& q) const
{
    std::vector<LoopInfo> out;
    if (db == nullptr) return out;

    juce::String sql = "SELECT id, path, name, style, instrument, tags, bpm, musical_key, length, source FROM loops WHERE 1=1";
    if (q.text.isNotEmpty()) sql << " AND (name LIKE ?1 OR tags LIKE ?1 OR instrument LIKE ?1 OR style LIKE ?1)";
    if (q.style.isNotEmpty()) sql << " AND style = ?2";
    if (q.instrument.isNotEmpty()) sql << " AND instrument LIKE ?3";
    if (q.bpmMax > 0) sql << " AND (bpm = 0 OR (bpm >= ?4 AND bpm <= ?5))";
    sql << " ORDER BY style, name LIMIT " << q.limit;

    Statement s (db, sql.toRawUTF8());
    if (q.text.isNotEmpty()) s.bind (1, "%" + q.text + "%");
    if (q.style.isNotEmpty()) s.bind (2, q.style);
    if (q.instrument.isNotEmpty()) s.bind (3, "%" + q.instrument + "%");
    if (q.bpmMax > 0) { s.bind (4, q.bpmMin); s.bind (5, q.bpmMax); }
    while (s.step()) out.push_back (rowToLoop (s));

    // Same-key and unpitched loops first when a key is given.
    if (q.key.isNotEmpty())
    {
        const auto want = Key::fromString (q.key);
        std::stable_sort (out.begin(), out.end(), [&] (const LoopInfo& a, const LoopInfo& b)
        {
            auto score = [&] (const LoopInfo& l) { if (l.key.isEmpty()) return 1; auto k = Key::fromString (l.key); return k == want ? 0 : 2; };
            return score (a) < score (b);
        });
    }
    return out;
}

int LibraryIndex::count() const
{
    if (db == nullptr) return 0;
    Statement s (db, "SELECT COUNT(*) FROM loops");
    return s.step() ? (int) s.integer (0) : 0;
}

juce::StringArray LibraryIndex::styles() const
{
    juce::StringArray out;
    if (db == nullptr) return out;
    Statement s (db, "SELECT DISTINCT style FROM loops WHERE style != '' ORDER BY style");
    while (s.step()) out.add (s.text (0));
    return out;
}

juce::StringArray LibraryIndex::instrumentNames() const
{
    juce::StringArray out;
    if (db == nullptr) return out;
    Statement s (db, "SELECT DISTINCT instrument FROM loops WHERE instrument != '' ORDER BY instrument");
    while (s.step()) out.add (s.text (0));
    return out;
}

//==============================================================================
namespace library
{
    LoopMatch matchLoop (const LoopInfo& loop, double songBpm, const Key& songKey)
    {
        LoopMatch m;
        if (loop.bpm > 0)
        {
            // Allow half/double time if it is closer.
            double bpm = loop.bpm;
            if (std::abs (bpm * 2 - songBpm) < std::abs (bpm - songBpm)) bpm *= 2;
            if (std::abs (bpm / 2 - songBpm) < std::abs (bpm - songBpm)) bpm /= 2;
            m.stretchRatio = bpm / songBpm;
            m.lengthBeats = loop.lengthSeconds * bpm / 60.0;
        }
        else
            m.lengthBeats = loop.lengthSeconds * songBpm / 60.0;

        if (loop.key.isNotEmpty())
        {
            auto k = Key::fromString (loop.key);
            // Compare relative-major tonics so Am fits C.
            const int a = k.isMinor() ? (k.tonic + 3) % 12 : k.tonic;
            const int b = songKey.isMinor() ? (songKey.tonic + 3) % 12 : songKey.tonic;
            int diff = ((b - a) % 12 + 12) % 12;
            if (diff > 6) diff -= 12;
            m.semitones = diff;
        }
        // Round the length to whole beats.
        m.lengthBeats = std::max (1.0, std::round (m.lengthBeats));
        return m;
    }

    juce::Result addLoopToProject (Project& p, const juce::File& folder, const LoopInfo& loop, double atBeat, const juce::String& trackId)
    {
        auto dest = store::audioFolder (folder).getChildFile (loop.file.getFileName()).getNonexistentSibling();
        store::audioFolder (folder).createDirectory();
        if (! loop.file.copyFileTo (dest))
            return juce::Result::fail ("Could not copy the loop into the project.");

        auto m = matchLoop (loop, p.bpm, p.keyAt (atBeat));
        Track* t = trackId.isNotEmpty() ? p.findTrack (trackId) : nullptr;
        if (t == nullptr) t = &p.addTrack (TrackType::Audio, loop.name, TrackRole::Unknown);

        Clip c;
        c.id = makeId ("clip");
        c.name = loop.name;
        c.audioFile = store::relativePath (folder, dest);
        c.startBeat = atBeat;
        c.lengthBeats = m.lengthBeats;
        c.stretchRatio = m.stretchRatio;
        c.pitchSemitones = m.semitones;
        t->clips.push_back (c);
        return juce::Result::ok();
    }

    int generateStarterLoops (const juce::File& out, double sr, std::function<bool (float)> progress)
    {
        int written = 0;
        const auto& all = beatgen::styles();
        for (size_t si = 0; si < all.size(); ++si)
        {
            const auto& style = all[si];
            Project p;
            beatgen::Settings s;
            s.style = style.id;
            s.seed = 1000 + (juce::int64) si;
            s.lengthSeconds = 60;
            beatgen::BeatGenerator::generate (p, s);

            const Section* chorus = nullptr;
            for (auto& sec : p.sections) if (sec.type == SectionType::Chorus) { chorus = &sec; break; }
            if (chorus == nullptr) continue;
            const double start = chorus->startBeat, end = start + 4 * p.beatsPerBar();
            const auto keyName = theory::pitchClassName (p.key.tonic) + (p.key.isMinor() ? "m" : "");

            for (auto& t : p.tracks)
            {
                if (t.generator.part.isEmpty() || t.generator.part == "pad") continue;
                RenderRequest req;
                req.sampleRate = sr;
                req.includeMastering = false;
                req.startBeat = start;
                req.endBeat = end;
                req.tailSeconds = 0;
                Project solo = p;
                for (auto& st : solo.tracks) { st.volumeDb = st.id == t.id ? -3.0f : st.volumeDb; st.outputBusId.clear(); st.sends.clear(); }
                auto audio = OfflineRenderer::renderTrack (solo, t.id, req);
                if (audio.getMagnitude (0, audio.getNumSamples()) < 1.0e-4f) continue;

                const bool pitched = t.generator.part != "drums" && t.generator.part != "percussion";
                auto name = style.name.upToFirstOccurrenceOf (" (", false, false).replaceCharacter (' ', '_') + "_" + t.generator.part
                          + "_" + juce::String ((int) std::round (p.bpm)) + "bpm" + (pitched ? "_" + keyName : juce::String());
                auto f = out.getChildFile (style.name.upToFirstOccurrenceOf (" (", false, false))
                            .getChildFile (t.generator.part).getChildFile (juce::File::createLegalFileName (name) + ".wav");
                if (writeAudioFile (f, audio, sr, 24)) ++written;
            }
            if (progress && ! progress ((float) (si + 1) / (float) all.size())) break;
        }
        return written;
    }
}

} // namespace studio
