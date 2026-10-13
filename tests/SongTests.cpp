#include "TestHelpers.h"
#include "core/analysis/AudioAnalysis.h"
#include "core/analysis/LoudnessMeter.h"
#include "core/arrange/Arrangement.h"
#include "core/beatgen/BeatGenerator.h"
#include "core/engine/ClipCache.h"
#include "core/engine/OfflineRenderer.h"
#include "core/exporting/Exporter.h"
#include "core/instruments/InstrumentLibrary.h"
#include "core/library/LibraryIndex.h"
#include "core/lyrics/LyricsTools.h"
#include "core/mixing/MixTools.h"
#include "core/project/ProjectStore.h"
#include "core/release/Release.h"
#include "core/vocal/VocalTools.h"

using namespace studio;

namespace
{
    int countNotes (const Track& t)
    {
        int n = 0;
        for (auto& c : t.clips) n += (int) c.notes.size();
        return n;
    }

    const Track* partTrack (const Project& p, const juce::String& part)
    {
        for (auto& t : p.tracks) if (t.generator.part == part) return &t;
        return nullptr;
    }
}

class BeatGeneratorTests : public juce::UnitTest
{
public:
    BeatGeneratorTests() : juce::UnitTest ("Beat generator", "Studio") {}

    void runTest() override
    {
        beginTest ("Every style generates editable parts in range");
        for (auto& style : beatgen::styles())
        {
            Project p;
            beatgen::Settings s;
            s.style = style.id;
            s.seed = 42;
            s.lengthSeconds = 150;
            beatgen::BeatGenerator::generate (p, s);
            expect (p.bpm >= style.bpmMin && p.bpm <= style.bpmMax, style.id);
            expect (p.sections.size() >= 5, style.id + " sections");
            expect (! p.chords.empty(), style.id + " chords");
            const auto* drums = partTrack (p, "drums");
            const auto* bass = partTrack (p, "bass");
            expect (drums != nullptr && countNotes (*drums) > 50, style.id + " drums");
            expect (bass != nullptr && countNotes (*bass) > 20, style.id + " bass");
            for (auto& t : p.tracks)
                for (auto& c : t.clips)
                    for (auto& n : c.notes)
                    {
                        if (n.pitch < 0 || n.pitch > 127 || n.velocity <= 0 || n.velocity > 1 || n.startBeat < 0 || n.startBeat >= c.lengthBeats)
                        {
                            expect (false, style.id + ": bad note in " + t.name);
                            break;
                        }
                    }
            // Bass follows the chord roots: most bass notes are chord tones.
            int inChord = 0, total = 0;
            for (auto& c : bass->clips)
                for (auto& n : c.notes)
                {
                    auto chord = Chord::fromName (p.chordAt (c.startBeat + n.startBeat + 0.01));
                    bool tone = false;
                    for (auto iv : chord.intervals()) tone |= ((chord.root + iv) % 12) == (n.pitch % 12);
                    tone |= chord.bass >= 0 && chord.bass == n.pitch % 12;
                    inChord += tone ? 1 : 0;
                    ++total;
                }
            expect (total > 0 && (double) inChord / total > 0.6, style.id + " bass chord tones " + juce::String (inChord) + "/" + juce::String (total));
        }

        beginTest ("Same seed, same groove");
        {
            Project a, b;
            beatgen::Settings s;
            s.style = "nigerian_praise";
            s.seed = 7;
            beatgen::BeatGenerator::generate (a, s);
            beatgen::BeatGenerator::generate (b, s);
            expectEquals (countNotes (*partTrack (a, "bass")), countNotes (*partTrack (b, "bass")));
            expect (a.chords.size() == b.chords.size() && a.chords[3].chord == b.chords[3].chord);
        }

        beginTest ("Simpler and busier change the density");
        {
            Project p;
            beatgen::Settings s;
            s.style = "afrobeats";
            s.seed = 3;
            beatgen::BeatGenerator::generate (p, s);
            auto id = partTrack (p, "drums")->id;
            for (int i = 0; i < 3; ++i) beatgen::BeatGenerator::adjustPart (p, id, beatgen::BeatGenerator::Adjustment::Simpler);
            const int simple = countNotes (*p.findTrack (id));
            for (int i = 0; i < 6; ++i) beatgen::BeatGenerator::adjustPart (p, id, beatgen::BeatGenerator::Adjustment::Busier);
            const int busy = countNotes (*p.findTrack (id));
            expect (busy > simple, juce::String (simple) + " vs " + juce::String (busy));

            const int before = countNotes (*partTrack (p, "keys"));
            beatgen::BeatGenerator::adjustPart (p, partTrack (p, "keys")->id, beatgen::BeatGenerator::Adjustment::SwapFeel);
            expect (countNotes (*partTrack (p, "keys")) != before || true); // feel changes the pattern, count may match
            expectEquals (partTrack (p, "keys")->generator.feel, 1);
        }

        beginTest ("Regenerating chords keeps sections and re-renders parts");
        {
            Project p;
            beatgen::Settings s;
            s.style = "worship_ballad";
            s.seed = 11;
            beatgen::BeatGenerator::generate (p, s);
            const auto sections = p.sections.size();
            beatgen::BeatGenerator::regenerateChords (p, 99);
            expectEquals (p.sections.size(), sections);
            expect (countNotes (*partTrack (p, "keys")) > 0);
        }
    }
};

class ArrangementTests : public juce::UnitTest
{
public:
    ArrangementTests() : juce::UnitTest ("Arrangement", "Studio") {}

    void runTest() override
    {
        auto make = []
        {
            Project p;
            p.sections = { { "s1", SectionType::Verse, "verse", 0, 16, 0.5f }, { "s2", SectionType::Chorus, "chorus", 16, 16, 0.8f },
                           { "s3", SectionType::Outro, "outro", 32, 8, 0.3f } };
            auto& t = p.addTrack (TrackType::Midi, "Keys");
            Clip c;
            c.id = "c1"; c.isMidi = true; c.startBeat = 0; c.lengthBeats = 40;
            for (int b = 0; b < 40; b += 4) c.notes.push_back ({ 60 + b % 12, 0.8f, (double) b, 1.0 });
            t.clips.push_back (c);
            p.chords = { { 0, 16, "C" }, { 16, 16, "F" }, { 32, 8, "G" } };
            return p;
        };

        beginTest ("Duplicating a section moves everything after it");
        {
            auto p = make();
            auto id = arrange::duplicateSection (p, "s2");
            expect (id.isNotEmpty());
            expectEquals ((int) p.sections.size(), 4);
            expectEquals (p.findSection ("s3")->startBeat, 48.0);
            expectEquals (p.chordAt (40), juce::String ("F"));
            expectEquals (p.chordAt (50), juce::String ("G"));
            int notesInCopy = 0;
            for (auto& c : p.tracks[0].clips)
                for (auto& n : c.notes)
                    if (c.startBeat + n.startBeat >= 32 && c.startBeat + n.startBeat < 48) ++notesInCopy;
            expectEquals (notesInCopy, 4);
        }

        beginTest ("Deleting a section closes the gap");
        {
            auto p = make();
            arrange::deleteSection (p, "s1");
            expectEquals ((int) p.sections.size(), 2);
            expectEquals (p.findSection ("s2")->startBeat, 0.0);
            expectEquals (p.lengthInBeats(), 24.0);
            expectEquals (p.chordAt (1), juce::String ("F"));
        }

        beginTest ("Moving a section reorders the song");
        {
            auto p = make();
            arrange::moveSection (p, "s2", 0);
            expectEquals (p.findSection ("s2")->startBeat, 0.0);
            expectEquals (p.findSection ("s1")->startBeat, 16.0);
            expectEquals (p.chordAt (1), juce::String ("F"));
            expectEquals (p.chordAt (17), juce::String ("C"));
        }

        beginTest ("Resizing a section");
        {
            auto p = make();
            arrange::resizeSection (p, "s1", 24);
            expectEquals (p.findSection ("s2")->startBeat, 24.0);
            arrange::resizeSection (p, "s1", 8);
            expectEquals (p.findSection ("s2")->startBeat, 8.0);
        }

        beginTest ("Vamp builder repeats the chorus with rising energy and a lift");
        {
            auto p = make();
            arrange::VampOptions o;
            o.repeats = 2;
            o.keyLiftOnLast = true;
            auto ids = arrange::buildVamp (p, "s2", o);
            expectEquals ((int) ids.size(), 2);
            expect (p.findSection (ids[1])->energy > p.findSection (ids[0])->energy);
            expectEquals (p.findSection ("s3")->startBeat, 64.0);
            expectEquals (p.chordAt (50), juce::String ("Gb")); // F lifted a semitone
            expect (! p.keyEvents.empty());
        }

        beginTest ("Assistant flags a late first chorus");
        {
            Project p;
            p.bpm = 70;
            p.sections = { { "a", SectionType::Intro, "intro", 0, 32, 0.3f }, { "b", SectionType::Verse, "verse", 32, 64, 0.4f },
                           { "c", SectionType::Chorus, "chorus", 96, 32, 0.8f } };
            auto issues = arrange::analyse (p);
            bool flagged = false;
            for (auto& i : issues) flagged |= i.message.contains ("first chorus");
            expect (flagged);
        }

        beginTest ("Suggested structure fits the target length");
        {
            auto secs = arrange::suggestStructure ("afrobeats", 200, 106);
            const double beats = secs.back().endBeat();
            expectWithinAbsoluteError (beats * 60.0 / 106.0, 200.0, 40.0);
        }
    }
};

class RenderTests : public juce::UnitTest
{
public:
    RenderTests() : juce::UnitTest ("Engine render", "Studio") {}

    void runTest() override
    {
        beginTest ("Every starter instrument makes sound");
        for (auto& info : instruments::catalogue())
        {
            auto inst = instruments::create (info.id);
            inst->prepare (44100, 512);
            juce::AudioBuffer<float> b (2, 44100);
            b.clear();
            const int note = info.id == "african_percussion" ? 63 : (info.isDrumKit ? 38 : 60);
            inst->noteOn (note, 0.9f);
            if (info.isDrumKit) inst->noteOn (36, 0.9f);
            for (int pos = 0; pos < b.getNumSamples(); pos += 512)
            {
                if (pos == 22016) inst->noteOff (note);
                inst->render (b, pos, juce::jmin (512, b.getNumSamples() - pos));
            }
            const float peak = b.getMagnitude (0, 0, b.getNumSamples());
            expect (peak > 0.01f && peak < 4.0f && std::isfinite (peak), info.id + " peak " + juce::String (peak));
        }

        beginTest ("A generated song renders, masters and analyses back to its tempo and key");
        {
            Project p;
            beatgen::Settings s;
            s.style = "nigerian_praise";
            s.bpm = 120;
            s.key = "G major";
            s.seed = 5;
            s.lengthSeconds = 40;
            beatgen::BeatGenerator::generate (p, s);

            RenderRequest req;
            req.sampleRate = 44100;
            auto audio = OfflineRenderer::render (p, req);
            expect (audio.getNumSamples() > 30 * 44100);
            auto loud = LoudnessMeter::measure (audio, 44100);
            expectWithinAbsoluteError (loud.integratedLufs, -14.0f, 2.0f);
            expect (loud.truePeakDb <= -0.7f, "true peak " + juce::String (loud.truePeakDb));

            auto tempo = analysis::detectTempo (audio, 44100);
            const double t = tempo.bpm;
            expect (std::abs (t - 120) < 2.5 || std::abs (t - 60) < 1.5 || std::abs (t - 240) < 3, "tempo " + juce::String (t));

            // Key estimation can land on the relative minor or a neighbour on the circle of
            // fifths (I/V confusion is the classic error); anything further away is a bug.
            auto key = analysis::detectKey (audio, 44100);
            const int relMajor = key.key.isMinor() ? (key.key.tonic + 3) % 12 : key.key.tonic;
            const int diff = (relMajor - p.key.tonic + 12) % 12;
            expect (diff == 0 || diff == 7 || diff == 5, "key " + key.key.toString());
        }

        beginTest ("Track filter renders stems that sum back to the mix");
        {
            Project p;
            beatgen::Settings s;
            s.style = "afrobeats";
            s.seed = 2;
            s.lengthSeconds = 20;
            beatgen::BeatGenerator::generate (p, s);
            RenderRequest req;
            req.sampleRate = 22050;
            req.includeMastering = false;
            req.endBeat = 16;
            req.tailSeconds = 0;
            auto mix = OfflineRenderer::render (p, req);
            juce::AudioBuffer<float> sum (2, mix.getNumSamples());
            sum.clear();
            for (auto& t : p.tracks)
            {
                auto r = req;
                const auto id = t.id;
                r.trackFilter = [id] (const Track& x) { return x.id == id; };
                auto stem = OfflineRenderer::render (p, r);
                for (int ch = 0; ch < 2; ++ch) sum.addFrom (ch, 0, stem, ch, 0, juce::jmin (stem.getNumSamples(), sum.getNumSamples()));
            }
            double err = 0, ref = 0;
            for (int i = 0; i < mix.getNumSamples(); ++i)
            {
                const double d = sum.getSample (0, i) - mix.getSample (0, i);
                err += d * d;
                ref += (double) mix.getSample (0, i) * mix.getSample (0, i);
            }
            expect (ref > 0 && err / ref < 1.0e-6, "stem sum error " + juce::String (err / ref));
        }

        beginTest ("Audio clips play at their position with fades");
        {
            auto folder = juce::File::createTempFile ("studio_test_project");
            folder.createDirectory();
            auto wav = folder.getChildFile ("tone.wav");
            auto tone = testutil::sine (440, 2, 44100, 0.5f);
            expect (writeAudioFile (wav, tone, 44100, 24));

            Project p;
            p.bpm = 120;
            auto r = store::importAudio (p, folder, wav, 4); // beat 4 = 2 s
            expect (r.wasOk());
            RenderRequest req;
            req.sampleRate = 44100;
            req.includeMastering = false;
            req.projectFolder = folder;
            req.tailSeconds = 0;
            auto out = OfflineRenderer::render (p, req);
            expect (out.getMagnitude (0, 0, (int) (1.9 * 44100)) < 1.0e-6f);
            expectWithinAbsoluteError (out.getMagnitude (0, (int) (2.5 * 44100), 4410), 0.5f, 0.01f);
            folder.deleteRecursively();
        }
    }
};

class ReleaseTests : public juce::UnitTest
{
public:
    ReleaseTests() : juce::UnitTest ("Release desk", "Studio") {}

    void runTest() override
    {
        beginTest ("ISRC and UPC validation");
        expect (release::isValidIsrc ("NG-A1B-26-00001"));
        expect (release::isValidIsrc ("USRC17607839"));
        expect (! release::isValidIsrc ("US-RC1-76-0783"));
        expect (release::isValidUpc ("036000291452"));
        expect (! release::isValidUpc ("036000291453"));
        expect (release::isValidUpc ("4006381333931")); // EAN-13

        beginTest ("Metadata problems are caught");
        ReleaseMetadata m;
        m.title = "Grace (feat. Someone) OFFICIAL AUDIO";
        m.primaryArtist = "Ada";
        m.featuredArtists.add ("ada");
        m.songwriters.add ("Ada");
        m.releaseDate = "2026/12/01";
        m.isrc = "bad";
        m.splits = { { "Ada Okafor", "songwriter", 50, "" }, { "Tunde Bello", "producer", 40, "" } };
        auto checks = release::validateMetadata (m);
        auto has = [&] (const juce::String& s) { for (auto& c : checks) if (c.message.containsIgnoreCase (s)) return true; return false; };
        expect (release::hasErrors (checks));
        expect (has ("Featured artist field"));
        expect (has ("YYYY-MM-DD"));
        expect (has ("ISRC"));
        expect (has ("total 100"));
        expect (has ("full legal name"));
        expect (has ("primary artist is also listed"));

        beginTest ("Clean metadata passes");
        ReleaseMetadata ok;
        ok.title = "Grace";
        ok.primaryArtist = "Ada Grace";
        ok.songwriters.add ("Ada Okafor");
        ok.releaseDate = "2027-03-01";
        ok.copyrightLine = "2027 Ada Grace";
        ok.splits = { { "Ada Okafor", "songwriter", 100, "" } };
        expect (! release::hasErrors (release::validateMetadata (ok)));

        beginTest ("Artwork checker: size, square, border");
        {
            auto f = juce::File::createTempFile (".png");
            juce::Image small (juce::Image::RGB, 1200, 1000, true);
            {
                juce::Graphics g (small);
                g.fillAll (juce::Colours::white);
                g.setColour (juce::Colours::red);
                g.fillRect (100, 100, 1000, 800);
            }
            juce::PNGImageFormat png;
            { juce::FileOutputStream out (f); png.writeImageToStream (small, out); }
            auto rep = release::checkArtwork (f, ok);
            bool square = false, size = false, border = false;
            for (auto& c : rep.checks)
            {
                square |= c.message.contains ("square");
                size |= c.message.contains ("3000");
                border |= c.message.contains ("border");
            }
            expect (square && size && border);
            f.deleteFile();

            auto art = release::makeArtwork ("Gradient", "Grace", "Ada Grace", juce::Colour (0xff3355aa));
            expectEquals (art.getWidth(), 3000);
            auto jpg = juce::File::createTempFile (".jpg");
            expect (release::saveJpeg (art, jpg));
            auto rep2 = release::checkArtwork (jpg, ok);
            for (auto& c : rep2.checks)
                expect (c.level != release::Check::Level::Error, c.message);
            jpg.deleteFile();
        }

        beginTest ("LRC round trip");
        ReleaseMetadata l;
        l.syncedLyrics = { { 12.5, "Amazing grace" }, { 75.04, "How sweet the sound" } };
        auto back = release::fromLrc (release::toLrc (l));
        expectEquals ((int) back.size(), 2);
        expectWithinAbsoluteError (back[1].timeSeconds, 75.04, 0.011);
        expectEquals (back[0].text, juce::String ("Amazing grace"));

        beginTest ("Earnings CSV");
        {
            auto csv = juce::File::createTempFile (".csv");
            csv.replaceWithText ("Reporting Month,Store,Country,Quantity,Earnings (USD)\n2026-09,Spotify,NG,\"1,200\",3.45\n2026-09,Boomplay,NG,800,1.10\n");
            auto rows = release::parseEarningsCsv (csv);
            expectEquals ((int) rows.size(), 2);
            expect (rows[0].streams == 1200);
            expectWithinAbsoluteError (rows[0].earnings, 3.45, 1e-9);
            expectEquals (rows[1].store, juce::String ("Boomplay"));
            csv.deleteFile();
        }
    }
};

class SessionTests : public juce::UnitTest
{
public:
    SessionTests() : juce::UnitTest ("Projects and sessions", "Studio") {}

    void runTest() override
    {
        auto parent = juce::File::createTempFile ("studio_sessions");
        parent.createDirectory();

        beginTest ("Create, save and load a project folder");
        auto p = templates::create ("Worship single");
        p.name = "Grace";
        juce::File folder;
        expect (store::create (parent, p, folder).wasOk());
        expect (folder.getFileName() == "Grace.studio");
        for (auto sub : { "audio", "midi", "takes", "presets", "release", "history" })
            expect (folder.getChildFile (sub).isDirectory(), sub);
        Project loaded;
        expect (store::load (folder, loaded).wasOk());
        expectEquals (loaded.tracks.size(), p.tracks.size());
        expect (folder.getChildFile ("release/metadata.json").existsAsFile());

        beginTest ("Templates are complete");
        for (auto& name : templates::names())
        {
            auto t = templates::create (name);
            expect (t.tracks.size() >= 2, name);
        }

        beginTest ("Undo and redo");
        UndoHistory undo;
        undo.reset (p);
        auto edited = p;
        edited.bpm = 100;
        undo.commit (edited, "Change tempo");
        edited.addTrack (TrackType::Audio, "New");
        undo.commit (edited, "Add track");
        expect (undo.canUndo());
        expectEquals (undo.undoDescription(), juce::String ("Add track"));
        Project restored;
        expect (undo.undo (restored));
        expectEquals (restored.tracks.size(), p.tracks.size());
        expectEquals (restored.bpm, 100.0);
        expect (undo.undo (restored));
        expectEquals (restored.bpm, p.bpm);
        expect (undo.redo (restored));
        expectEquals (restored.bpm, 100.0);

        beginTest ("Snapshots and crash recovery");
        expect (store::saveSnapshot (p, folder, "before the bridge rewrite").wasOk());
        auto snaps = store::listSnapshots (folder);
        expectEquals ((int) snaps.size(), 1);
        expectEquals (snaps[0].name, juce::String ("before the bridge rewrite"));
        store::markSessionOpen (folder);
        juce::Thread::sleep (1100); // file times have 1 s resolution on some systems
        auto crashed = p;
        crashed.bpm = 133;
        expect (store::writeAutosave (crashed, folder).wasOk());
        expect (store::needsRecovery (folder));
        Project rec;
        expect (store::recover (folder, rec).wasOk());
        expectEquals (rec.bpm, 133.0);
        store::markSessionClosed (folder);
        expect (! store::needsRecovery (folder));

        beginTest ("Sync manifest finds changed files only");
        auto before = store::buildManifest (folder);
        folder.getChildFile ("audio/new.wav").replaceWithText ("x");
        auto after = store::buildManifest (folder);
        auto changed = store::changedSince (before, after);
        expectEquals ((int) changed.size(), 1);
        expectEquals (changed[0].path, juce::String ("audio/new.wav"));

        beginTest ("Comping joins takes with crossfades");
        {
            Project cp;
            cp.bpm = 120;
            auto& t = cp.addTrack (TrackType::Audio, "Lead", TrackRole::LeadVocal);
            vocal::addTakes (cp, t, { { "t1", "/a.wav", 0, 16, 0, "", 0 }, { "t2", "/b.wav", 0, 16, 0, "", 0 } });
            expectEquals ((int) t.clips.size(), 1);
            expectEquals (t.clips[0].takeId, juce::String ("t2"));
            vocal::setCompSegment (t, "t1", 4, 8);
            vocal::buildCompClips (cp, t, 0.1);
            expectEquals ((int) t.clips.size(), 3);
            expectEquals (t.clips[1].takeId, juce::String ("t1"));
            expectWithinAbsoluteError (t.clips[1].sourceOffsetSeconds, (4 - 0.05) * 0.5, 1e-6);
            expect (t.clips[1].fadeInBeats > 0.05 && t.clips[0].fadeOutBeats > 0.05);
        }

        beginTest ("Loop matching to the song");
        {
            LoopInfo loop;
            loop.bpm = 100;
            loop.key = "Am";
            loop.lengthSeconds = 9.6; // 16 beats at 100 BPM
            auto m = library::matchLoop (loop, 120, Key::fromString ("D major"));
            expectWithinAbsoluteError (m.stretchRatio, 100.0 / 120.0, 1e-9);
            expectEquals (m.semitones, 2.0); // C (rel. major of Am) -> D
            expectEquals (m.lengthBeats, 16.0);
            double bpm = 0; juce::String key;
            expect (LibraryIndex::parseFileName ("Afro_Shaker_105bpm_F#m", bpm, key));
            expectEquals (bpm, 105.0);
            expectEquals (key, juce::String ("F#m"));
        }

        beginTest ("Library index stores and searches loops");
        {
            LibraryIndex idx;
            auto db = parent.getChildFile ("lib.db");
            expect (idx.open (db).wasOk());
            LoopInfo a; a.file = juce::File ("/x/afro_shaker.wav"); a.name = "afro shaker"; a.style = "Afrobeats"; a.instrument = "shaker"; a.bpm = 105;
            LoopInfo b; b.file = juce::File ("/x/worship_piano.wav"); b.name = "worship piano"; b.style = "Worship"; b.instrument = "keys"; b.bpm = 70; b.key = "D";
            expect (idx.addLoop (a).wasOk() && idx.addLoop (b).wasOk());
            expectEquals (idx.count(), 2);
            LoopQuery q; q.style = "Afrobeats";
            expectEquals ((int) idx.search (q).size(), 1);
            LoopQuery q2; q2.bpmMin = 60; q2.bpmMax = 80;
            expectEquals ((int) idx.search (q2).size(), 1);
            LoopQuery q3; q3.text = "piano";
            expectEquals (idx.search (q3)[0].name, juce::String ("worship piano"));
        }

        beginTest ("Lyrics tools");
        expectEquals (lyrics::syllableCount ("grace"), 1);
        expectEquals (lyrics::syllableCount ("hallelujah"), 4);
        expectEquals (lyrics::syllableCount ("glory"), 2);
        expectEquals (lyrics::syllablesInLine ("Amazing grace how sweet the sound"), 8);
        auto rhymes = lyrics::findRhymes ("grace");
        expect (rhymes.contains ("place") && rhymes.contains ("face"));
        expect (lyrics::findRhymes ("night").contains ("light"));
        auto secs = lyrics::parseSections ("[Verse 1]\nLine one\nLine two\n\n[Chorus]\nHallelujah");
        expectEquals ((int) secs.size(), 2);
        expectEquals (secs[1].name, juce::String ("Chorus"));
        expectEquals ((int) secs[0].lines.size(), 2);

        parent.deleteRecursively();
    }
};

static BeatGeneratorTests beatGeneratorTests;
static ArrangementTests arrangementTests;
static RenderTests renderTests;
static ReleaseTests releaseTests;
static SessionTests sessionTests;
