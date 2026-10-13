#include <juce_core/juce_core.h>
#include "core/theory/MusicTheory.h"
#include "core/model/Project.h"
#include "core/model/TempoMap.h"
#include "core/midi/MidiTools.h"
#include "core/beatgen/BeatGenerator.h"

using namespace studio;

class TheoryTests : public juce::UnitTest
{
public:
    TheoryTests() : juce::UnitTest ("Music theory", "Studio") {}

    void runTest() override
    {
        beginTest ("Key parsing and naming");
        expect (Key::fromString ("Eb major").tonic == 3);
        expect (Key::fromString ("F# minor").tonic == 6 && Key::fromString ("F# minor").isMinor());
        expect (Key::fromString ("Am").isMinor() && Key::fromString ("Am").tonic == 9);
        expectEquals (Key::fromString ("Bb").toString(), juce::String ("Bb major"));

        beginTest ("Scale snapping");
        Key c;
        expectEquals (c.snapToScale (61), 60);   // C# -> C (ties go down)
        expectEquals (c.snapToScale (66), 65);   // F# -> F
        expect (c.containsPitchClass (4) && ! c.containsPitchClass (3));
        expectEquals (c.degreeToNote (0, 4), 60);
        expectEquals (c.degreeToNote (7, 4), 72);
        expectEquals (c.degreeToNote (-1, 4), 59);

        beginTest ("Chords");
        expectEquals (Chord::fromName ("F#m7").name (false), juce::String ("F#m7"));
        expect (Chord::fromName ("Cmaj7").quality == ChordQuality::Major7);
        expect (Chord::fromName ("G/B").bass == 11);
        expect (theory::diatonicChord (c, 5).quality == ChordQuality::Minor);   // vi = Am
        expect (theory::diatonicChord (c, 4, true).quality == ChordQuality::Dominant7); // V7
        Chord detected;
        expect (theory::detectChord ({ 57, 60, 64 }, detected));
        expectEquals (detected.name(), juce::String ("Am"));
        expect (theory::detectChord ({ 43, 59, 62, 65 }, detected));
        expectEquals (detected.name(), juce::String ("G7"));
        expectEquals (theory::romanNumeral (c, Chord::fromName ("Am")), juce::String ("vi"));

        beginTest ("Voicings stay compact");
        auto v = Chord::fromName ("C").voicing (64, 4);
        expect (v.size() == 4);
        expect (v.back() - v.front() <= 14);

        beginTest ("Roman numerals in minor keys use the relative major");
        auto am = Key::fromString ("A minor");
        expectEquals (beatgen::BeatGenerator::romanToChord (am, "vi", false).name(), juce::String ("Am"));
        expectEquals (beatgen::BeatGenerator::romanToChord (am, "IV", false).name(), juce::String ("F"));
        expectEquals (beatgen::BeatGenerator::romanToChord (c, "V/3", false).name(), juce::String ("G/B"));
    }
};

class ModelTests : public juce::UnitTest
{
public:
    ModelTests() : juce::UnitTest ("Project model", "Studio") {}

    void runTest() override
    {
        beginTest ("JSON round trip keeps everything");
        Project p;
        p.name = "Grace";
        p.bpm = 72;
        p.key = Key::fromString ("D major");
        p.sections.push_back ({ makeId ("sec"), SectionType::Chorus, "chorus", 32, 32, 0.8f });
        p.chords.push_back ({ 0, 4, "D" });
        p.tempoEvents.push_back ({ 64, 80 });
        auto& t = p.addTrack (TrackType::Midi, "Keys", TrackRole::Keys);
        t.instrument.id = "rhodes";
        t.generator.part = "keys";
        t.generator.seed = 1234567890123LL;
        t.generator.density = 0.7f;
        Clip c;
        c.id = makeId ("clip");
        c.isMidi = true;
        c.notes.push_back ({ 60, 0.8f, 0.5, 1.0 });
        t.clips.push_back (c);
        t.effects.push_back ({ "fx1", "eq", false, { { "p1Gain", -3.0f } }, {}, {}, {} });
        t.automationFor ("volume").points = { { 0, -6 }, { 16, 0 } };
        p.release.title = "Grace";
        p.release.splits.push_back ({ "Ada Okafor", "songwriter", 60, "" });

        auto q = Project::fromJson (p.toJson());
        expectEquals (q.name, juce::String ("Grace"));
        expectEquals (q.bpm, 72.0);
        expect (q.key == p.key);
        expectEquals ((int) q.sections.size(), 1);
        expect (q.sections[0].type == SectionType::Chorus);
        expectEquals ((int) q.tracks.size(), 1);
        expectEquals (q.tracks[0].instrument.id, juce::String ("rhodes"));
        expect (q.tracks[0].generator.seed == 1234567890123LL);
        expect (q.tracks[0].clips[0].notes[0] == p.tracks[0].clips[0].notes[0]);
        expectEquals (q.tracks[0].effects[0].params["p1Gain"], -3.0f);
        expectEquals (q.tracks[0].automation[0].valueAt (8, 0), -3.0f);
        expectEquals (q.release.splits[0].percent, 60.0);
        expectEquals ((int) q.tempoEvents.size(), 1);

        beginTest ("Tempo map with a tempo change");
        TempoMap tm (120, { { 8, 60 } });
        expectWithinAbsoluteError (tm.beatsToSeconds (8), 4.0, 1e-9);
        expectWithinAbsoluteError (tm.beatsToSeconds (9), 5.0, 1e-9);
        expectWithinAbsoluteError (tm.secondsToBeats (5.0), 9.0, 1e-9);
        expectEquals (tm.bpmAt (10), 60.0);

        beginTest ("Removing a bus reroutes to master");
        Project r;
        auto busId = r.addTrack (TrackType::Bus, "Drums").id;
        auto& d = r.addTrack (TrackType::Midi, "Kit");
        d.outputBusId = busId;
        d.sends.push_back ({ busId, -6, false });
        r.removeTrack (busId);
        expect (r.tracks[0].outputBusId.isEmpty() && r.tracks[0].sends.empty());
    }
};

class MidiToolTests : public juce::UnitTest
{
public:
    MidiToolTests() : juce::UnitTest ("MIDI tools", "Studio") {}

    void runTest() override
    {
        beginTest ("Smart quantize keeps notes inside the window");
        std::vector<MidiNote> notes { { 60, 0.8f, 0.03, 0.5 }, { 62, 0.8f, 0.6, 0.5 } };
        midi::quantize (notes, 0.25, 1.0f, 0.0f, 0.05);
        expectWithinAbsoluteError (notes[0].startBeat, 0.03, 1e-9); // within the window: untouched
        expectWithinAbsoluteError (notes[1].startBeat, 0.5, 1e-9);

        beginTest ("Chord pads");
        auto labels = midi::chordPadLabels (Key::fromString ("G major"));
        expectEquals (labels[0], juce::String ("G"));
        expectEquals (labels[5], juce::String ("Em"));
        expectEquals (labels[7], juce::String ("F"));

        beginTest ("Arpeggiator and strum");
        std::vector<MidiNote> chord { { 60, 0.8f, 0, 1 }, { 64, 0.8f, 0, 1 }, { 67, 0.8f, 0, 1 } };
        midi::ArpSettings as;
        auto arp = midi::arpeggiate (chord, as);
        expectEquals ((int) arp.size(), 4);
        expectEquals (arp[0].pitch, 60);
        expectEquals (arp[1].pitch, 64);
        midi::StrumSettings ss;
        auto st = midi::strum ({ { 52, 0.8f, 0, 4 }, { 55, 0.8f, 0, 4 }, { 59, 0.8f, 0, 4 } }, ss);
        expect (st.size() > 6);
        expect (st[1].startBeat > st[0].startBeat);

        beginTest ("MIDI file round trip");
        Project p;
        p.bpm = 100;
        auto& t = p.addTrack (TrackType::Midi, "Piano", TrackRole::Keys);
        Clip c;
        c.id = makeId ("clip");
        c.isMidi = true;
        c.lengthBeats = 8;
        c.notes = { { 60, 0.75f, 0, 1 }, { 64, 0.5f, 1, 1 }, { 67, 1.0f, 2, 2 } };
        t.clips.push_back (c);
        auto f = juce::File::createTempFile (".mid");
        expect (midi::exportMidiFile (p, f));
        Project q;
        expectEquals (midi::importMidiFile (q, f), 1);
        expectWithinAbsoluteError (q.bpm, 100.0, 0.01);
        expectEquals ((int) q.tracks[0].clips[0].notes.size(), 3);
        expectEquals (q.tracks[0].clips[0].notes[2].pitch, 67);
        expectWithinAbsoluteError (q.tracks[0].clips[0].notes[2].lengthBeats, 2.0, 0.01);
        f.deleteFile();

        beginTest ("Chord detection from MIDI");
        auto chords = midi::detectChords ({ { 48, 0.8f, 0, 4 }, { 52, 0.8f, 0, 4 }, { 55, 0.8f, 0, 4 }, { 45, 0.8f, 4, 4 }, { 52, 0.8f, 4, 4 }, { 57, 0.8f, 4, 4 }, { 60, 0.8f, 4, 4 } }, 0, 8, 2);
        expectEquals ((int) chords.size(), 2);
        expectEquals (chords[0].chord, juce::String ("C"));
        expectEquals (chords[1].chord, juce::String ("Am"));
    }
};

static TheoryTests theoryTests;
static ModelTests modelTests;
static MidiToolTests midiToolTests;
