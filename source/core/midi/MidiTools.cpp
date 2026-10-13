#include "MidiTools.h"
#include "../model/TempoMap.h"
#include <algorithm>

namespace studio::midi
{

void quantize (std::vector<MidiNote>& notes, double grid, float strength, float swing, double window, bool ends)
{
    if (grid <= 0) return;
    for (auto& n : notes)
    {
        const double idx = std::round (n.startBeat / grid);
        double target = idx * grid;
        if (((long long) idx) % 2 != 0) target += swing * grid; // swing every other grid step
        const double diff = target - n.startBeat;
        if (std::abs (diff) <= window) continue;  // close enough: keep the feel
        n.startBeat += diff * strength;
        if (ends)
        {
            const double end = n.startBeat + n.lengthBeats;
            const double endTarget = std::max (grid, std::round (end / grid) * grid);
            n.lengthBeats = std::max (grid * 0.25, endTarget - n.startBeat);
        }
        n.startBeat = std::max (0.0, n.startBeat);
    }
}

void humanize (std::vector<MidiNote>& notes, double timing, float vel, juce::int64 seed)
{
    juce::Random r (seed);
    for (auto& n : notes)
    {
        n.startBeat = std::max (0.0, n.startBeat + (r.nextDouble() - 0.5) * 2.0 * timing);
        n.velocity = juce::jlimit (0.02f, 1.0f, n.velocity * (1.0f + (r.nextFloat() - 0.5f) * 2.0f * vel));
    }
}

void transpose (std::vector<MidiNote>& notes, int semis)
{
    for (auto& n : notes) n.pitch = juce::jlimit (0, 127, n.pitch + semis);
}

void scaleVelocity (std::vector<MidiNote>& notes, float factor, float offset)
{
    for (auto& n : notes) n.velocity = juce::jlimit (0.02f, 1.0f, n.velocity * factor + offset);
}

void snapToKey (std::vector<MidiNote>& notes, const Key& key)
{
    for (auto& n : notes) n.pitch = key.snapToScale (n.pitch);
}

void legato (std::vector<MidiNote>& notes)
{
    std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
    for (size_t i = 0; i + 1 < notes.size(); ++i)
    {
        // Extend each note to the next distinct onset.
        for (size_t j = i + 1; j < notes.size(); ++j)
            if (notes[j].startBeat > notes[i].startBeat + 1.0e-6)
            {
                notes[i].lengthBeats = notes[j].startBeat - notes[i].startBeat;
                break;
            }
    }
}

std::vector<ChordEvent> detectChords (const std::vector<MidiNote>& notes, double clipStart, double length, double step)
{
    std::vector<ChordEvent> out;
    for (double t = 0; t < length - 1.0e-6; t += step)
    {
        std::vector<int> sounding;
        for (auto& n : notes)
            if (n.startBeat < t + step && n.startBeat + n.lengthBeats > t)
                sounding.push_back (n.pitch);
        Chord c;
        if (! theory::detectChord (sounding, c)) continue;
        const auto name = c.name();
        if (! out.empty() && out.back().chord == name && std::abs (out.back().beat + out.back().lengthBeats - (clipStart + t)) < 1.0e-6)
            out.back().lengthBeats += step;
        else
            out.push_back ({ clipStart + t, step, name });
    }
    return out;
}

Chord chordPad (const Key& key, int pad, bool sevenths)
{
    if (pad == 7)
    {
        // bVII: borrowed chord common in worship and gospel.
        Chord c { (key.tonic + 10) % 12, ChordQuality::Major, -1 };
        if (key.isMinor()) c.root = (key.tonic + 10) % 12;
        return c;
    }
    Key k7 = key;
    if (key.intervals().size() != 7) k7.scale = key.isMinor() ? ScaleType::NaturalMinor : ScaleType::Major;
    return theory::diatonicChord (k7, juce::jlimit (0, 6, pad), sevenths);
}

juce::StringArray chordPadLabels (const Key& key, bool sevenths)
{
    juce::StringArray out;
    for (int i = 0; i < 8; ++i)
        out.add (chordPad (key, i, sevenths).name());
    return out;
}

std::vector<MidiNote> arpeggiate (const std::vector<MidiNote>& chords, const ArpSettings& s)
{
    std::vector<MidiNote> out;
    if (chords.empty()) return out;

    // Group notes that start together into chords.
    auto sorted = chords;
    std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
    juce::Random r (7);

    size_t i = 0;
    while (i < sorted.size())
    {
        const double start = sorted[i].startBeat;
        std::vector<MidiNote> group;
        while (i < sorted.size() && sorted[i].startBeat - start < 0.05) group.push_back (sorted[i++]);
        double end = start;
        for (auto& g : group) end = std::max (end, g.startBeat + g.lengthBeats);

        std::vector<int> pitches;
        for (int o = 0; o < juce::jmax (1, s.octaves); ++o)
            for (auto& g : group) pitches.push_back (g.pitch + 12 * o);
        if (s.mode != ArpSettings::Mode::AsPlayed) std::sort (pitches.begin(), pitches.end());
        if (s.mode == ArpSettings::Mode::Down) std::reverse (pitches.begin(), pitches.end());
        if (s.mode == ArpSettings::Mode::UpDown && pitches.size() > 2)
            for (int k = (int) pitches.size() - 2; k > 0; --k) pitches.push_back (pitches[(size_t) k]);

        const float vel = group.front().velocity;
        int k = 0;
        for (double t = start; t < end - 1.0e-6; t += s.rateBeats, ++k)
        {
            const int pitch = s.mode == ArpSettings::Mode::Random ? pitches[(size_t) r.nextInt ((int) pitches.size())]
                                                                  : pitches[(size_t) k % pitches.size()];
            const double sw = (k % 2 == 1) ? s.swing * s.rateBeats : 0.0;
            out.push_back ({ pitch, vel * (k % 4 == 0 ? 1.0f : 0.85f), t + sw, s.rateBeats * s.gate });
        }
    }
    return out;
}

std::vector<MidiNote> strum (const std::vector<MidiNote>& chords, const StrumSettings& s)
{
    std::vector<MidiNote> out;
    auto sorted = chords;
    std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });

    size_t i = 0;
    while (i < sorted.size())
    {
        const double start = sorted[i].startBeat;
        std::vector<MidiNote> group;
        while (i < sorted.size() && sorted[i].startBeat - start < 0.05) group.push_back (sorted[i++]);
        double end = start;
        for (auto& g : group) end = std::max (end, g.startBeat + g.lengthBeats);
        std::sort (group.begin(), group.end(), [] (auto& a, auto& b) { return a.pitch < b.pitch; });

        int k = 0;
        for (double t = start; t < end - 1.0e-6; t += s.stepBeats, ++k)
        {
            const auto c = s.pattern[k % juce::jmax (1, s.pattern.length())];
            if (c != 'D' && c != 'U') continue;
            const bool down = c == 'D';
            const double spread = s.strumTimeBeats / juce::jmax (1, (int) group.size() - 1);
            for (size_t n = 0; n < group.size(); ++n)
            {
                const size_t idx = down ? n : group.size() - 1 - n;
                const float v = group[idx].velocity * (down ? 1.0f : s.upVelocity);
                out.push_back ({ group[idx].pitch, v, t + n * spread, s.stepBeats * 0.95 });
            }
        }
    }
    return out;
}

//==============================================================================
bool exportMidiFile (const Project& p, const juce::File& file, const juce::String& onlyTrackId)
{
    constexpr int ppq = 960;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);

    // Conductor track: tempo, time signature, key, section markers.
    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (p.timeSigNumerator, p.timeSigDenominator), 0);
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / p.bpm)), 0);
    for (auto& e : p.tempoEvents)
        conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / e.bpm)), e.beat * ppq);
    {
        // Key signature: number of sharps/flats from the circle of fifths.
        static const int sharps[12] { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
        int sf = sharps[p.key.isMinor() ? (p.key.tonic + 3) % 12 : p.key.tonic];
        conductor.addEvent (juce::MidiMessage::keySignatureMetaEvent (sf, p.key.isMinor()), 0);
    }
    for (auto& s : p.sections)
        conductor.addEvent (juce::MidiMessage::textMetaEvent (6, toString (s.type)), s.startBeat * ppq);
    conductor.updateMatchedPairs();
    mf.addTrack (conductor);

    int channel = 1;
    int written = 0;
    for (auto& t : p.tracks)
    {
        if (t.type != TrackType::Midi) continue;
        if (onlyTrackId.isNotEmpty() && t.id != onlyTrackId) continue;
        const bool drums = t.role == TrackRole::Drums || t.instrument.id.startsWith ("drums_") || t.instrument.id == "african_percussion";
        const int ch = drums ? 10 : channel;
        if (! drums) { ++channel; if (channel == 10) ++channel; if (channel > 16) channel = 1; }

        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, t.name), 0);
        for (auto& c : t.clips)
        {
            if (! c.isMidi) continue;
            for (auto& n : c.notes)
            {
                if (n.startBeat >= c.lengthBeats) continue;
                const double s = (c.startBeat + n.startBeat) * ppq;
                const double e = (c.startBeat + std::min (n.startBeat + n.lengthBeats, c.lengthBeats)) * ppq;
                seq.addEvent (juce::MidiMessage::noteOn (ch, n.pitch, n.velocity), s);
                seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitch), std::max (s + 1, e));
            }
        }
        seq.sort();
        seq.updateMatchedPairs();
        mf.addTrack (seq);
        ++written;
    }

    file.deleteFile();
    juce::FileOutputStream out (file);
    return out.openedOk() && mf.writeTo (out) && written > 0;
}

int importMidiFile (Project& p, const juce::File& file, double atBeat)
{
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in))
        return 0;

    const int ppq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960;
    int created = 0;

    for (int ti = 0; ti < mf.getNumTracks(); ++ti)
    {
        auto seq = *mf.getTrack (ti);
        seq.updateMatchedPairs();

        // Tempo from the first tempo event if the project is empty.
        for (int e = 0; e < seq.getNumEvents(); ++e)
        {
            auto& m = seq.getEventPointer (e)->message;
            if (m.isTempoMetaEvent() && p.tracks.empty())
                p.bpm = 60.0 / m.getTempoSecondsPerQuarterNote();
        }

        std::map<int, std::vector<MidiNote>> byChannel;
        juce::String name;
        for (int e = 0; e < seq.getNumEvents(); ++e)
        {
            auto* ev = seq.getEventPointer (e);
            auto& m = ev->message;
            if (m.isTrackNameEvent()) name = m.getTextFromTextMetaEvent();
            if (! m.isNoteOn()) continue;
            const double start = m.getTimeStamp() / ppq;
            const double end = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() / ppq : start + 0.25;
            byChannel[m.getChannel()].push_back ({ m.getNoteNumber(), m.getFloatVelocity(), start, std::max (0.01, end - start) });
        }

        for (auto& [ch, notes] : byChannel)
        {
            if (notes.empty()) continue;
            double last = 0;
            for (auto& n : notes) last = std::max (last, n.startBeat + n.lengthBeats);
            const bool drums = ch == 10;
            auto& t = p.addTrack (TrackType::Midi, name.isNotEmpty() ? name : (drums ? juce::String ("Drums") : "MIDI " + juce::String (created + 1)),
                                  drums ? TrackRole::Drums : TrackRole::Keys);
            t.instrument.id = drums ? "drums_gospel" : "grand_piano";
            Clip c;
            c.id = makeId ("clip");
            c.name = file.getFileNameWithoutExtension();
            c.isMidi = true;
            c.startBeat = atBeat;
            c.lengthBeats = std::ceil (last / p.beatsPerBar()) * p.beatsPerBar();
            c.notes = notes;
            t.clips.push_back (c);
            ++created;
        }
    }
    return created;
}

} // namespace studio::midi
