#include "PianoRollScreen.h"
#include "core/midi/MidiTools.h"
#include "core/instruments/InstrumentLibrary.h"
#include "core/instruments/Drums.h"

namespace studio::ui
{

class NoteGrid : public juce::Component
{
public:
    explicit NoteGrid (Session& s) : session (s) { setWantsKeyboardFocus (true); }

    double gridBeats = 0.25;
    static constexpr int keysW = 56, velH = 70;
    int lowNote = 36, highNote = 96;
    double pixelsPerBeat = 60;

    const Clip* clip() const
    {
        auto* t = session.project().findTrack (session.selectedTrackId);
        if (t == nullptr) return nullptr;
        for (auto& c : t->clips) if (c.id == session.selectedClipId && c.isMidi) return &c;
        return nullptr;
    }

    bool isDrums() const
    {
        auto* t = session.project().findTrack (session.selectedTrackId);
        return t != nullptr && (t->instrument.id.startsWith ("drums_") || t->instrument.id == "african_percussion");
    }

    float rowH() const { return (float) (getHeight() - velH) / (float) (highNote - lowNote + 1); }
    float yFor (int note) const { return (float) (highNote - note) * rowH(); }
    int noteAt (int y) const { return juce::jlimit (0, 127, highNote - (int) (y / rowH())); }
    float xFor (double beat) const { return keysW + (float) (beat * pixelsPerBeat); }
    double beatAt (int x) const { return (x - keysW) / pixelsPerBeat; }
    double snap (double b) const { return juce::ModifierKeys::currentModifiers.isAltDown() ? b : std::floor (b / gridBeats + 0.5) * gridBeats; }

    void fitToClip()
    {
        if (auto* c = clip())
        {
            pixelsPerBeat = juce::jmax (8.0, (getWidth() - keysW - 10) / juce::jmax (1.0, c->lengthBeats));
            if (! c->notes.empty())
            {
                int lo = 127, hi = 0;
                for (auto& n : c->notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
                lowNote = juce::jmax (0, std::min (lo - 6, hi - 24));
                highNote = juce::jmin (127, std::max (hi + 6, lowNote + 30));
            }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panel);
        auto* c = clip();
        if (c == nullptr)
        {
            g.setColour (colours::textDim);
            g.drawText ("Double-click a MIDI clip in Arrange (or pick one above) to edit it here.", getLocalBounds(), juce::Justification::centred);
            return;
        }
        const auto key = session.project().keyAt (c->startBeat);
        const bool drums = isDrums();
        const float rh = rowH();
        for (int n = lowNote; n <= highNote; ++n)
        {
            const float y = yFor (n);
            const bool inKey = key.containsPitchClass (n);
            g.setColour (inKey ? colours::panelRaised : colours::background);
            g.fillRect ((float) keysW, y, (float) getWidth(), rh);
            g.setColour (juce::MidiMessage::isMidiNoteBlack (n) ? juce::Colour (0xff2a2d36) : juce::Colour (0xffd8dbe2));
            g.fillRect (0.0f, y, (float) keysW - 2, rh - 1);
            if (rh >= 9)
            {
                g.setColour (juce::MidiMessage::isMidiNoteBlack (n) ? colours::textDim : juce::Colours::black);
                g.setFont (juce::FontOptions (juce::jmin (11.0f, rh - 1)));
                juce::String label = theory::noteName (n);
                if (drums)
                    for (auto& [note, piece] : drumNames()) if (note == n) label = piece;
                g.drawText (label, 2, (int) y, keysW - 6, (int) rh, juce::Justification::centredRight);
            }
        }
        for (double b = 0; b <= c->lengthBeats + 1e-6; b += gridBeats)
        {
            const bool beat = std::fmod (b, 1.0) < 1e-6, bar = std::fmod (b, session.project().beatsPerBar()) < 1e-6;
            g.setColour (bar ? colours::textDim.withAlpha (0.6f) : beat ? colours::line : colours::line.withAlpha (0.35f));
            g.drawVerticalLine ((int) xFor (b), 0, (float) (getHeight() - velH));
        }
        g.setColour (colours::background.withAlpha (0.6f));
        g.fillRect (xFor (c->lengthBeats), 0.0f, (float) getWidth(), (float) (getHeight() - velH));

        const auto col = juce::Colour (session.project().findTrack (session.selectedTrackId)->colour);
        for (size_t i = 0; i < c->notes.size(); ++i)
        {
            auto& n = c->notes[i];
            if (n.pitch < lowNote || n.pitch > highNote) continue;
            auto r = juce::Rectangle<float> (xFor (n.startBeat), yFor (n.pitch), juce::jmax (3.0f, (float) (n.lengthBeats * pixelsPerBeat)), rh).reduced (0.5f);
            g.setColour (col.withMultipliedBrightness (0.6f + 0.6f * n.velocity));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (selected.count ((int) i) ? colours::text : juce::Colours::black.withAlpha (0.4f));
            g.drawRoundedRectangle (r, 2.0f, selected.count ((int) i) ? 1.5f : 0.8f);
        }

        // Velocity lane
        auto vel = getLocalBounds().removeFromBottom (velH);
        g.setColour (colours::background);
        g.fillRect (vel);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("Velocity", 2, vel.getY(), keysW - 4, 14, juce::Justification::centredLeft);
        for (auto& n : c->notes)
        {
            const float x = xFor (n.startBeat), h = n.velocity * (velH - 8);
            g.setColour (colours::accent);
            g.fillRect (x, (float) vel.getBottom() - h, 3.0f, h);
        }

        const double pos = session.engine().getPositionBeats() - c->startBeat;
        if (pos >= 0 && pos <= c->lengthBeats)
        {
            g.setColour (colours::text);
            g.drawVerticalLine ((int) xFor (pos), 0, (float) getHeight());
        }
    }

    static const std::vector<std::pair<int, juce::String>>& drumNames()
    {
        static std::vector<std::pair<int, juce::String>> names = []
        {
            std::vector<std::pair<int, juce::String>> v;
            auto kit = instruments::create ("drums_afro");
            if (auto* dk = dynamic_cast<DrumKit*> (kit.get()))
                for (auto& [n, p] : dk->getPieces()) v.push_back ({ n, p.name });
            return v;
        }();
        return names;
    }

    // ---- editing --------------------------------------------------------------
    void edit (const juce::String& what, std::function<void (Clip&)> fn)
    {
        const auto tid = session.selectedTrackId, cid = session.selectedClipId;
        session.perform (what, [=] (Project& p) { if (auto* t = p.findTrack (tid)) if (auto* c = t->findClip (cid)) fn (*c); });
    }

    int noteIndexAt (juce::Point<int> pos) const
    {
        auto* c = clip();
        if (c == nullptr) return -1;
        for (int i = (int) c->notes.size() - 1; i >= 0; --i)
        {
            auto& n = c->notes[(size_t) i];
            auto r = juce::Rectangle<float> (xFor (n.startBeat), yFor (n.pitch), juce::jmax (3.0f, (float) (n.lengthBeats * pixelsPerBeat)), rowH());
            if (r.contains (pos.toFloat())) return i;
        }
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto* c = clip();
        if (c == nullptr || e.x < keysW)
        {
            if (e.x < keysW && e.y < getHeight() - velH) { session.engine().liveNoteOn (noteAt (e.y), 0.8f); auditioned = noteAt (e.y); }
            return;
        }
        before = *c;
        working = *c;
        dragMode = 0;
        if (e.y >= getHeight() - velH)
        {
            dragMode = 4; // velocity drawing
            mouseDrag (e);
            return;
        }
        const int idx = noteIndexAt (e.getPosition());
        if (e.mods.isPopupMenu())
        {
            if (idx >= 0) edit ("Delete note", [idx] (Clip& cl) { if (idx < (int) cl.notes.size()) cl.notes.erase (cl.notes.begin() + idx); });
            return;
        }
        if (idx >= 0)
        {
            if (! e.mods.isShiftDown() && ! selected.count (idx)) selected.clear();
            selected.insert (idx);
            auto& n = c->notes[(size_t) idx];
            const float right = xFor (n.startBeat + n.lengthBeats);
            dragMode = e.x > right - 6 ? 2 : 1;
            session.engine().liveNoteOn (n.pitch, n.velocity);
            auditioned = n.pitch;
        }
        else
        {
            // Add a note one grid step long.
            selected.clear();
            const double b = std::floor (beatAt (e.x) / gridBeats) * gridBeats;
            const int note = noteAt (e.y);
            working.notes.push_back ({ note, 0.8f, b, isDrums() ? 0.2 : gridBeats });
            selected.insert ((int) working.notes.size() - 1);
            dragMode = 2;
            session.engine().liveNoteOn (note, 0.8f);
            auditioned = note;
        }
        anchorBeat = beatAt (e.x);
        anchorNote = noteAt (e.y);
        original = working;
        const_cast<Clip&> (*c) = working; // live preview while dragging; committed on mouse up
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (clip() == nullptr || dragMode == 0) return;
        auto& live = const_cast<Clip&> (*clip());
        if (dragMode == 4)
        {
            for (auto& n : live.notes)
                if (std::abs (xFor (n.startBeat) - (float) e.x) < 5)
                    n.velocity = juce::jlimit (0.05f, 1.0f, (float) (getHeight() - e.y) / (float) (velH - 8));
            repaint();
            return;
        }
        const double db = snap (beatAt (e.x) - anchorBeat);
        const int dn = noteAt (e.y) - anchorNote;
        live = original;
        for (int i : selected)
        {
            if (i >= (int) live.notes.size()) continue;
            auto& n = live.notes[(size_t) i];
            if (dragMode == 1) { n.startBeat = juce::jmax (0.0, n.startBeat + db); n.pitch = juce::jlimit (0, 127, n.pitch + dn); }
            else n.lengthBeats = juce::jmax (gridBeats * 0.25, snap (n.startBeat + n.lengthBeats + db) - n.startBeat);
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (auditioned >= 0) { session.engine().liveNoteOff (auditioned); auditioned = -1; }
        if (clip() == nullptr || dragMode == 0) return;
        // Put the clip back as it was and commit the result through the session (one undo step).
        Clip result = *clip();
        const_cast<Clip&> (*clip()) = before;
        edit (dragMode == 4 ? "Velocity" : "Edit notes", [result] (Clip& c) { c.notes = result.notes; });
        dragMode = 0;
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && ! selected.empty())
        {
            auto sel = selected;
            selected.clear();
            edit ("Delete notes", [sel] (Clip& c)
            {
                std::vector<MidiNote> keep;
                for (int i = 0; i < (int) c.notes.size(); ++i) if (! sel.count (i)) keep.push_back (c.notes[(size_t) i]);
                c.notes = keep;
            });
            return true;
        }
        if (k.getKeyCode() == 'A' && k.getModifiers().isCommandDown())
        {
            if (auto* c = clip()) for (int i = 0; i < (int) c->notes.size(); ++i) selected.insert (i);
            repaint();
            return true;
        }
        return false;
    }

    std::set<int> selected;

private:
    Session& session;
    int dragMode = 0, anchorNote = 0, auditioned = -1;
    double anchorBeat = 0;
    Clip working, original, before;
};

//==============================================================================
PianoRollScreen::PianoRollScreen (Session& s) : Screen (s)
{
    grid = std::make_unique<NoteGrid> (s);
    addAndMakeVisible (*grid);
    styleHeading (clipInfo, 15);
    addAndMakeVisible (clipInfo);
    styleCaption (chordInfo);
    addAndMakeVisible (chordInfo);

    gridSize.addItemList ({ "1/4", "1/8", "1/16", "1/8 triplet", "1/32" }, 1);
    gridSize.setSelectedId (3);
    gridSize.onChange = [this]
    {
        static const double v[] { 1.0, 0.5, 0.25, 1.0 / 3.0, 0.125 };
        grid->gridBeats = v[gridSize.getSelectedId() - 1];
        grid->repaint();
    };
    addAndMakeVisible (gridSize);

    clipChoice.onChange = [this]
    {
        auto* t = session.project().findTrack (session.selectedTrackId);
        const int i = clipChoice.getSelectedId() - 1;
        if (t != nullptr && i >= 0 && i < (int) t->clips.size())
        {
            session.select (t->id, t->clips[(size_t) i].id);
            grid->fitToClip();
        }
    };
    addAndMakeVisible (clipChoice);

    strength.setRange (0, 1);
    strength.setValue (0.7);
    strength.setSliderStyle (juce::Slider::LinearHorizontal);
    strength.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    strength.setTooltip ("Quantize strength: lower keeps more human feel");
    addAndMakeVisible (strength);

    auto op = [this] (juce::TextButton& b, const juce::String& what, std::function<void (Clip&)> fn, const juce::String& tip)
    {
        b.setTooltip (tip);
        b.onClick = [this, what, fn]
        {
            auto sel = grid->selected;
            grid->edit (what, [sel, fn] (Clip& c)
            {
                if (sel.empty()) { fn (c); return; }
                // Apply only to the selected notes.
                Clip part = c;
                part.notes.clear();
                for (int i : sel) if (i < (int) c.notes.size()) part.notes.push_back (c.notes[(size_t) i]);
                fn (part);
                int k = 0;
                for (int i : sel) if (i < (int) c.notes.size() && k < (int) part.notes.size()) c.notes[(size_t) i] = part.notes[(size_t) k++];
            });
        };
        addAndMakeVisible (b);
    };
    op (quantize, "Quantize", [this] (Clip& c) { midi::quantize (c.notes, grid->gridBeats, (float) strength.getValue(), 0.0f, grid->gridBeats * 0.1); },
        "Smart quantize: tightens timing, keeps a human feel");
    op (humanize, "Humanize", [] (Clip& c) { midi::humanize (c.notes, 0.015, 0.1f, juce::Random::getSystemRandom().nextInt64()); }, "Small timing and velocity variation");
    op (legato, "Legato", [] (Clip& c) { midi::legato (c.notes); }, "Stretch notes to the next note");
    op (toKey, "Snap to key", [this] (Clip& c) { midi::snapToKey (c.notes, session.project().keyAt (c.startBeat)); }, "Move wrong notes into the song key");
    op (transposeUp, "Transpose", [] (Clip& c) { midi::transpose (c.notes, 1); }, "Up a semitone");
    op (transposeDown, "Transpose", [] (Clip& c) { midi::transpose (c.notes, -1); }, "Down a semitone");
    op (octaveUp, "Transpose", [] (Clip& c) { midi::transpose (c.notes, 12); }, "Up an octave");
    op (octaveDown, "Transpose", [] (Clip& c) { midi::transpose (c.notes, -12); }, "Down an octave");
    detect.setTooltip ("Detect the chords in this clip and write them to the chord track");
    detect.onClick = [this]
    {
        auto* c = grid->clip();
        if (c == nullptr) return;
        auto chords = midi::detectChords (c->notes, c->startBeat, c->lengthBeats, 2.0);
        const double s = c->startBeat, e = c->endBeat();
        session.perform ("Detect chords", [=] (Project& p)
        {
            p.chords.erase (std::remove_if (p.chords.begin(), p.chords.end(), [&] (auto& ch) { return ch.beat >= s && ch.beat < e; }), p.chords.end());
            p.chords.insert (p.chords.end(), chords.begin(), chords.end());
            std::sort (p.chords.begin(), p.chords.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
        });
    };
    addAndMakeVisible (detect);
}

PianoRollScreen::~PianoRollScreen() = default;

void PianoRollScreen::refresh (bool structural)
{
    auto* t = session.project().findTrack (session.selectedTrackId);
    clipChoice.clear (juce::dontSendNotification);
    if (t != nullptr)
        for (int i = 0; i < (int) t->clips.size(); ++i)
            if (t->clips[(size_t) i].isMidi)
            {
                auto& c = t->clips[(size_t) i];
                clipChoice.addItem ((c.name.isNotEmpty() ? c.name : juce::String ("Clip")) + " (bar " + juce::String ((int) (c.startBeat / session.project().beatsPerBar()) + 1) + ")", i + 1);
                if (c.id == session.selectedClipId) clipChoice.setSelectedId (i + 1, juce::dontSendNotification);
            }
    if (auto* c = grid->clip())
    {
        clipInfo.setText (t->name + "  -  " + (c->name.isNotEmpty() ? c->name : juce::String ("MIDI clip")) + "  (" + juce::String ((int) c->notes.size()) + " notes)",
                          juce::dontSendNotification);
        std::vector<int> pitches;
        for (auto& n : c->notes) pitches.push_back (n.pitch);
        Chord ch;
        auto chords = midi::detectChords (c->notes, 0, c->lengthBeats, juce::jmax (2.0, c->lengthBeats / 8));
        juce::StringArray names;
        for (auto& e : chords) names.add (e.chord);
        chordInfo.setText (names.isEmpty() ? juce::String ("Key: ") + session.project().keyAt (c->startBeat).toString()
                                           : "Chords: " + names.joinIntoString (" - ") + "   |   Key: " + session.project().keyAt (c->startBeat).toString(),
                           juce::dontSendNotification);
        if (structural) grid->fitToClip();
    }
    else
    {
        clipInfo.setText ("No MIDI clip selected", juce::dontSendNotification);
        chordInfo.setText ({}, juce::dontSendNotification);
    }
    grid->repaint();
}

void PianoRollScreen::paint (juce::Graphics& g) { g.fillAll (colours::background); }

void PianoRollScreen::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (30);
    clipInfo.setBounds (top.removeFromLeft (top.getWidth() / 2));
    clipChoice.setBounds (top.removeFromLeft (260).reduced (2));
    chordInfo.setBounds (r.removeFromTop (22));
    auto bar = r.removeFromTop (36);
    gridSize.setBounds (bar.removeFromLeft (110).reduced (2));
    quantize.setBounds (bar.removeFromLeft (90).reduced (2));
    strength.setBounds (bar.removeFromLeft (100).reduced (2));
    for (auto* b : { &humanize, &legato, &toKey, &detect }) b->setBounds (bar.removeFromLeft (110).reduced (2));
    for (auto* b : { &transposeDown, &transposeUp, &octaveDown, &octaveUp }) b->setBounds (bar.removeFromLeft (46).reduced (2));
    r.removeFromTop (6);
    grid->setBounds (r);
}

} // namespace studio::ui
