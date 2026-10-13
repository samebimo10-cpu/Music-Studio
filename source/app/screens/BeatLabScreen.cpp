#include "BeatLabScreen.h"
#include "core/arrange/Arrangement.h"
#include "core/beatgen/BeatGenerator.h"
#include "core/instruments/InstrumentLibrary.h"
#include "core/midi/MidiTools.h"

namespace studio::ui
{

using Adj = beatgen::BeatGenerator::Adjustment;

//==============================================================================
/** 16-step grid for the drum part in the selected bar of the selected section. */
class DrumGrid : public juce::Component
{
public:
    explicit DrumGrid (Session& s) : session (s) {}

    struct Lane { int note; const char* name; };
    static const std::vector<Lane>& lanes()
    {
        using namespace instruments::gm;
        static const std::vector<Lane> l { { kick, "Kick" }, { snare, "Snare" }, { clap, "Clap" }, { rim, "Rim" }, { closedHat, "Hat" },
                                           { openHat, "Open hat" }, { shaker, "Shaker" }, { lowTom, "Low tom" }, { highTom, "High tom" }, { crash, "Crash" } };
        return l;
    }

    int bar = 0;

    const Track* drumTrack() const
    {
        if (auto* t = session.project().findTrack (session.selectedTrackId); t != nullptr && t->instrument.id.startsWith ("drums_")) return t;
        for (auto& t : session.project().tracks) if (t.instrument.id.startsWith ("drums_")) return &t;
        return nullptr;
    }

    double barStart() const
    {
        const auto& p = session.project();
        double base = 0;
        if (auto* s = const_cast<Project&> (p).findSection (session.selectedSectionId)) base = s->startBeat;
        return base + bar * p.beatsPerBar();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panel);
        auto* t = drumTrack();
        if (t == nullptr)
        {
            g.setColour (colours::textDim);
            g.drawText ("Add a drum kit track (or generate a beat) to edit drums here.", getLocalBounds(), juce::Justification::centred);
            return;
        }
        const int labelW = 70;
        const float cw = (float) (getWidth() - labelW) / 16.0f, ch = (float) getHeight() / (float) lanes().size();
        const double b0 = barStart();
        for (size_t l = 0; l < lanes().size(); ++l)
        {
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (lanes()[l].name, 4, (int) (l * ch), labelW - 6, (int) ch, juce::Justification::centredLeft);
            for (int s = 0; s < 16; ++s)
            {
                auto r = juce::Rectangle<float> (labelW + s * cw, l * ch, cw, ch).reduced (2);
                const float v = velocityAt (*t, lanes()[l].note, b0 + s * 0.25);
                g.setColour (s % 4 == 0 ? colours::panelRaised.brighter (0.06f) : colours::panelRaised);
                g.fillRoundedRectangle (r, 3.0f);
                if (v > 0)
                {
                    g.setColour (colours::accent.withAlpha (0.35f + 0.65f * v));
                    g.fillRoundedRectangle (r, 3.0f);
                }
            }
        }
        const double pos = session.engine().getPositionBeats() - b0;
        if (pos >= 0 && pos < 4)
        {
            g.setColour (colours::text);
            g.drawVerticalLine (labelW + (int) (pos / 0.25 * cw), 0, (float) getHeight());
        }
    }

    static float velocityAt (const Track& t, int note, double beat)
    {
        for (auto& c : t.clips)
            for (auto& n : c.notes)
                if (n.pitch == note && std::abs (c.startBeat + n.startBeat - beat) < 0.11) return n.velocity;
        return 0.0f;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* t = drumTrack();
        if (t == nullptr) return;
        const int labelW = 70;
        const float cw = (float) (getWidth() - labelW) / 16.0f, ch = (float) getHeight() / (float) lanes().size();
        if (e.x < labelW) return;
        const int step = (int) ((e.x - labelW) / cw), lane = (int) (e.y / ch);
        if (step < 0 || step >= 16 || lane < 0 || lane >= (int) lanes().size()) return;
        const int note = lanes()[(size_t) lane].note;
        const double beat = barStart() + step * 0.25;
        const auto id = t->id;
        const float vel = e.mods.isShiftDown() ? 0.45f : 0.85f; // Shift = ghost note
        session.perform ("Drum grid", [=] (Project& p)
        {
            auto* tr = p.findTrack (id);
            // Remove if present, else add (into the clip covering the beat, or a new one).
            for (auto& c : tr->clips)
            {
                const auto before = c.notes.size();
                c.notes.erase (std::remove_if (c.notes.begin(), c.notes.end(), [&] (auto& n) { return n.pitch == note && std::abs (c.startBeat + n.startBeat - beat) < 0.11; }), c.notes.end());
                if (c.notes.size() != before) return;
            }
            Clip* target = nullptr;
            for (auto& c : tr->clips) if (beat >= c.startBeat && beat < c.endBeat()) target = &c;
            if (target == nullptr)
            {
                Clip c;
                c.id = makeId ("clip");
                c.isMidi = true;
                c.name = "Drums";
                c.startBeat = std::floor (beat / p.beatsPerBar()) * p.beatsPerBar();
                c.lengthBeats = p.beatsPerBar();
                tr->clips.push_back (c);
                target = &tr->clips.back();
            }
            target->notes.push_back ({ note, vel, beat - target->startBeat, 0.2 });
        });
        session.engine().liveNoteOn (note, vel); // audition (on the live track if it is the drums)
    }

private:
    Session& session;
};

//==============================================================================
struct BeatLabScreen::PartRow : public juce::Component
{
    PartRow (Session& s, juce::String trackId) : session (s), id (std::move (trackId))
    {
        name.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        addAndMakeVisible (name);
        int k = 1;
        for (auto& i : instruments::catalogue()) instrument.addItem (i.family + ": " + i.name, k++);
        instrument.onChange = [this]
        {
            const int idx = instrument.getSelectedId() - 1;
            if (idx < 0) return;
            const auto inst = instruments::catalogue()[(size_t) idx].id;
            session.perform ("Change instrument", [this, inst] (Project& p) { if (auto* t = p.findTrack (id)) t->instrument.id = inst; });
        };
        addAndMakeVisible (instrument);
        struct B { juce::TextButton* b; Adj a; const char* tip; };
        for (auto [b, a, tip] : { B { &regen, Adj::Regenerate, "A new take on this part" }, B { &simpler, Adj::Simpler, "Fewer notes" },
                                  B { &busier, Adj::Busier, "More notes and fills" }, B { &feel, Adj::SwapFeel, "Try a different pattern feel" },
                                  B { &more, Adj::MoreLikeThis, "Keep the idea, change small details" } })
        {
            b->setTooltip (tip);
            b->onClick = [this, a = a]
            {
                session.perform ("Beat lab: " + juce::String (a == Adj::Regenerate ? "regenerate" : a == Adj::Simpler ? "simpler" : a == Adj::Busier ? "busier"
                                                               : a == Adj::SwapFeel ? "swap feel" : "more like this"),
                                 [this, a] (Project& p) { beatgen::BeatGenerator::adjustPart (p, id, a); });
            };
            addAndMakeVisible (b);
        }
        for (auto* b : { &mute, &solo }) { b->setClickingTogglesState (true); addAndMakeVisible (b); }
        mute.onClick = [this] { session.setTrackMute (id, mute.getToggleState()); };
        solo.onClick = [this] { session.setTrackSolo (id, solo.getToggleState()); };
        solo.setColour (juce::TextButton::buttonOnColourId, colours::accentWarm);
        meter.source = trackMeterSource (session.engine(), id);
        addAndMakeVisible (meter);
        refresh();
    }

    void refresh()
    {
        auto* t = session.project().findTrack (id);
        if (t == nullptr) return;
        name.setText (t->generator.part.isNotEmpty() ? t->generator.part.substring (0, 1).toUpperCase() + t->generator.part.substring (1) : t->name,
                      juce::dontSendNotification);
        int k = 1;
        for (auto& i : instruments::catalogue()) { if (i.id == t->instrument.id) instrument.setSelectedId (k, juce::dontSendNotification); ++k; }
        mute.setToggleState (t->mute, juce::dontSendNotification);
        solo.setToggleState (t->solo, juce::dontSendNotification);
        const bool gen = t->generator.part.isNotEmpty();
        for (auto* b : { &regen, &simpler, &busier, &feel, &more }) b->setEnabled (gen);
        density = t->generator.density;
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override { session.select (id); }

    void paint (juce::Graphics& g) override
    {
        paintPanel (g, getLocalBounds().toFloat().reduced (1));
        if (session.selectedTrackId == id)
        {
            g.setColour (colours::accent);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 8.0f, 1.5f);
        }
        // Density indicator under the name.
        g.setColour (colours::line);
        g.fillRect (12, getHeight() - 10, 100, 3);
        g.setColour (colours::accent);
        g.fillRect (12, getHeight() - 10, (int) (100 * density), 3);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        meter.setBounds (r.removeFromRight (10));
        r.removeFromRight (6);
        name.setBounds (r.removeFromLeft (110).withTrimmedBottom (8));
        instrument.setBounds (r.removeFromLeft (240).reduced (2, 6));
        r.removeFromLeft (6);
        for (auto* b : { &regen, &simpler, &busier, &feel, &more }) b->setBounds (r.removeFromLeft (r.getWidth() >= 560 ? 100 : 80).reduced (2, 6));
        mute.setBounds (r.removeFromLeft (34).reduced (2, 6));
        solo.setBounds (r.removeFromLeft (34).reduced (2, 6));
    }

    Session& session;
    juce::String id;
    juce::Label name;
    juce::ComboBox instrument;
    juce::TextButton regen { "Regenerate" }, simpler { "Simpler" }, busier { "Busier" }, feel { "Swap feel" }, more { "More like this" },
        mute { "M" }, solo { "S" };
    LevelMeter meter;
    float density = 0.5f;
};

//==============================================================================
BeatLabScreen::BeatLabScreen (Session& s) : Screen (s)
{
    for (auto& st : beatgen::styles()) style.addItem (st.name, style.getNumItems() + 1);
    for (int pc = 0; pc < 12; ++pc) { key.addItem (theory::pitchClassName (pc) + " major", pc + 1); key.addItem (theory::pitchClassName (pc) + " minor", pc + 13); }
    for (auto& m : beatgen::moods()) mood.addItem (m.substring (0, 1).toUpperCase() + m.substring (1), mood.getNumItems() + 1);
    tempo.setRange (40, 200, 1);
    tempo.setSliderStyle (juce::Slider::LinearHorizontal);
    tempo.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 22);
    length.setRange (60, 420, 15);
    length.setValue (210);
    length.setSliderStyle (juce::Slider::LinearHorizontal);
    length.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 22);
    length.textFromValueFunction = [] (double v) { return juce::String::formatted ("%d:%02d", (int) v / 60, (int) v % 60); };
    style.onChange = [this]
    {
        const auto& st = beatgen::styles()[(size_t) juce::jmax (0, style.getSelectedId() - 1)];
        tempo.setValue (juce::jlimit (st.bpmMin, st.bpmMax, tempo.getValue()), juce::dontSendNotification);
    };
    auto label = [this] (juce::Label& l, const juce::String& t) { l.setText (t, juce::dontSendNotification); styleCaption (l); addAndMakeVisible (l); };
    label (styleL, "Style"); label (keyL, "Key"); label (moodL, "Mood"); label (tempoL, "Tempo"); label (lengthL, "Length");
    for (auto* c : { (juce::Component*) &style, (juce::Component*) &key, (juce::Component*) &mood, (juce::Component*) &tempo, (juce::Component*) &length })
        addAndMakeVisible (c);

    generateButton.setColour (juce::TextButton::buttonColourId, colours::accent);
    generateButton.setTooltip ("Writes chords, drums, bass, keys, guitar, percussion and pads (replaces generated parts, keeps your recordings)");
    generateButton.onClick = [this] { generate(); };
    newChords.setTooltip ("A new chord progression; every generated part follows it");
    newChords.onClick = [this] { session.perform ("New chords", [] (Project& p) { beatgen::BeatGenerator::regenerateChords (p, 0); }); };
    addAndMakeVisible (generateButton);
    addAndMakeVisible (newChords);

    for (auto* l : { &partsTitle, &padsTitle, &gridTitle }) { styleHeading (*l, 15); addAndMakeVisible (l); }
    partsTitle.setText ("Parts", juce::dontSendNotification);
    padsTitle.setText ("Chord pads (play with one finger)", juce::dontSendNotification);
    gridTitle.setText ("Drum grid (Shift-click for a ghost note)", juce::dontSendNotification);

    partsView.setViewedComponent (&partsHolder, false);
    partsView.setScrollBarsShown (true, false);
    addAndMakeVisible (partsView);

    grid = std::make_unique<DrumGrid> (s);
    addAndMakeVisible (*grid);

    for (int i = 0; i < 8; ++i)
    {
        auto* b = pads.add (new juce::TextButton());
        b->onStateChange = [this, i, b]
        {
            const bool down = b->isDown();
            if (down != (b->getProperties()["down"].toString() == "1"))
            {
                b->getProperties().set ("down", down ? "1" : "0");
                playPad (i, down);
            }
        };
        addAndMakeVisible (b);
    }
    padMode.addItemList ({ "Block chord", "Strum", "Arpeggio" }, 1);
    padMode.setSelectedId (1);
    addAndMakeVisible (padMode);
    makeToggle (padSevenths, "7ths", false);
    padSevenths.onClick = [this] { refresh (false); };
    makeToggle (padRecord, "Write to clip", false);
    padRecord.setTooltip ("Each pad press writes that chord for one bar at the playhead on the selected MIDI track");
    addAndMakeVisible (padSevenths);
    addAndMakeVisible (padRecord);

    keyboardState.addListener (this);
    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (48);
    keyboard.setKeyWidth (22);
    addAndMakeVisible (keyboard);
}

BeatLabScreen::~BeatLabScreen() { keyboardState.removeListener (this); }

void BeatLabScreen::refresh (bool structural)
{
    const auto& p = session.project();
    for (int i = 0; i < (int) beatgen::styles().size(); ++i)
        if (beatgen::styles()[(size_t) i].id == p.style) style.setSelectedId (i + 1, juce::dontSendNotification);
    if (style.getSelectedId() == 0) style.setSelectedId (1, juce::dontSendNotification);
    key.setSelectedId (p.key.tonic + (p.key.isMinor() ? 13 : 1), juce::dontSendNotification);
    mood.setSelectedId (juce::jmax (1, beatgen::moods().indexOf (p.mood) + 1), juce::dontSendNotification);
    tempo.setValue (p.bpm, juce::dontSendNotification);

    if (structural)
    {
        parts.clear();
        for (auto& t : p.tracks)
            if (t.type == TrackType::Midi)
                partsHolder.addAndMakeVisible (parts.add (new PartRow (session, t.id)));
        if (session.selectedSectionId.isEmpty() && ! p.sections.empty())
        {
            // Show the first chorus in the drum grid.
            session.selectedSectionId = p.sections.front().id;
            for (auto& sec : p.sections) if (sec.type == SectionType::Chorus) { session.selectedSectionId = sec.id; break; }
        }
        resized();
    }
    else
        for (auto* r : parts) r->refresh();

    auto labels = midi::chordPadLabels (p.keyAt (session.engine().getPositionBeats()), padSevenths.getToggleState());
    static const char* numerals[] { "I", "ii", "iii", "IV", "V", "vi", "vii", "bVII" };
    for (int i = 0; i < pads.size(); ++i) pads[i]->setButtonText (labels[i] + "\n" + numerals[i]);
    grid->repaint();
}

void BeatLabScreen::generate()
{
    beatgen::Settings bs;
    bs.style = beatgen::styles()[(size_t) juce::jmax (0, style.getSelectedId() - 1)].id;
    bs.bpm = tempo.getValue();
    const int k = key.getSelectedId();
    bs.key = theory::pitchClassName ((k - 1) % 12) + (k > 12 ? " minor" : " major");
    bs.mood = beatgen::moods()[juce::jmax (0, mood.getSelectedId() - 1)];
    bs.lengthSeconds = length.getValue();
    const bool hasRecordings = [this] { for (auto& t : session.project().tracks) if (t.type == TrackType::Audio && ! t.clips.empty()) return true; return false; }();
    auto run = [this, bs]
    {
        session.perform ("Generate beat", [bs] (Project& p) { beatgen::BeatGenerator::generate (p, bs); });
        session.status ("New groove ready. Undo (Ctrl+Z) brings the previous one back.");
    };
    if (hasRecordings && ! session.project().sections.empty())
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Generate a new arrangement?")
                                          .withMessage ("Generating rebuilds the sections and every generated part. Your recordings stay, but they may no "
                                                        "longer line up with the new sections. To change one part only, use its Regenerate button.")
                                          .withButton ("Generate").withButton ("Cancel"),
                                      [run] (int r) { if (r == 1) run(); });
        return;
    }
    run();
}

void BeatLabScreen::playPad (int index, bool down)
{
    const auto& p = session.project();
    auto chord = midi::chordPad (p.keyAt (session.engine().getPositionBeats()), index, padSevenths.getToggleState());
    auto voicing = chord.voicing (60, 4);
    voicing.insert (voicing.begin(), 36 + ((chord.root - 36) % 12 + 12) % 12);
    if (! down)
    {
        for (auto n : padNotesDown) session.engine().liveNoteOff (n);
        padNotesDown.clear();
        return;
    }
    for (auto n : padNotesDown) session.engine().liveNoteOff (n);
    padNotesDown = voicing;
    const int mode = padMode.getSelectedId();
    for (size_t i = 0; i < voicing.size(); ++i)
    {
        const int n = voicing[i];
        if (mode == 1) session.engine().liveNoteOn (n, 0.8f);
        else
        {
            // Strum or arpeggio: spread the notes out in time.
            const int delay = mode == 2 ? (int) i * 25 : (int) i * 140;
            juce::Timer::callAfterDelay (delay, [this, n] { session.engine().liveNoteOn (n, 0.75f); });
        }
    }
    if (padRecord.getToggleState()) recordPadChord (index);
}

void BeatLabScreen::recordPadChord (int index)
{
    auto* t = session.project().findTrack (session.selectedTrackId);
    if (t == nullptr || t->type != TrackType::Midi) { session.status ("Select a MIDI track to write chords into."); return; }
    const auto& p = session.project();
    const double bar = std::floor (session.engine().getPositionBeats() / p.beatsPerBar()) * p.beatsPerBar();
    auto chord = midi::chordPad (p.keyAt (bar), index, padSevenths.getToggleState());
    std::vector<MidiNote> notes;
    for (auto n : chord.voicing (60, 4)) notes.push_back ({ n, 0.75f, 0.0, p.beatsPerBar() - 0.05 });
    const int mode = padMode.getSelectedId();
    if (mode == 2) { midi::StrumSettings ss; notes = midi::strum (notes, ss); }
    if (mode == 3) { midi::ArpSettings as; as.mode = midi::ArpSettings::Mode::UpDown; notes = midi::arpeggiate (notes, as); }
    const auto id = t->id;
    const auto chordName = chord.name();
    const double len = p.beatsPerBar();
    session.perform ("Chord pad", [=] (Project& pr)
    {
        auto* tr = pr.findTrack (id);
        Clip c;
        c.id = makeId ("clip");
        c.isMidi = true;
        c.name = chordName;
        c.startBeat = bar;
        c.lengthBeats = len;
        c.notes = notes;
        tr->clips.push_back (c);
        pr.chords.push_back ({ bar, len, chordName });
        std::sort (pr.chords.begin(), pr.chords.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
    });
    session.engine().setPositionBeats (bar + len);
}

void BeatLabScreen::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity) { session.engine().liveNoteOn (note, velocity); }
void BeatLabScreen::handleNoteOff (juce::MidiKeyboardState*, int, int note, float) { session.engine().liveNoteOff (note); }

void BeatLabScreen::paint (juce::Graphics& g) { g.fillAll (colours::background); }

void BeatLabScreen::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto top = r.removeFromTop (54);
    auto field = [&] (juce::Label& l, juce::Component& c, int w)
    {
        auto a = top.removeFromLeft (w);
        l.setBounds (a.removeFromTop (18));
        c.setBounds (a.reduced (0, 2));
        top.removeFromLeft (10);
    };
    field (styleL, style, 240);
    field (keyL, key, 120);
    field (moodL, mood, 110);
    field (tempoL, tempo, 180);
    field (lengthL, length, 180);
    newChords.setBounds (top.removeFromRight (110).withTrimmedTop (18));
    top.removeFromRight (8);
    generateButton.setBounds (top.removeFromRight (130).withTrimmedTop (18));
    r.removeFromTop (12);

    keyboard.setBounds (r.removeFromBottom (90));
    r.removeFromBottom (10);

    auto bottom = r.removeFromBottom (juce::jmin (260, r.getHeight() / 2));
    auto padArea = bottom.removeFromRight (juce::jmin (440, bottom.getWidth() / 2));
    bottom.removeFromRight (12);
    gridTitle.setBounds (bottom.removeFromTop (24));
    grid->setBounds (bottom);
    auto padTop = padArea.removeFromTop (30);
    padsTitle.setBounds (padTop.removeFromLeft (padTop.getWidth() - 300));
    padMode.setBounds (padTop.removeFromLeft (120).reduced (2));
    padSevenths.setBounds (padTop.removeFromLeft (60).reduced (2));
    padRecord.setBounds (padTop.reduced (2));
    for (int i = 0; i < pads.size(); ++i)
        pads[i]->setBounds (padArea.getX() + (i % 4) * padArea.getWidth() / 4, padArea.getY() + (i / 4) * padArea.getHeight() / 2,
                            padArea.getWidth() / 4, padArea.getHeight() / 2);
    for (auto* p : pads) p->setBounds (p->getBounds().reduced (4));

    r.removeFromBottom (10);
    partsTitle.setBounds (r.removeFromTop (24));
    partsView.setBounds (r);
    const int w = r.getWidth() - 12;
    for (int i = 0; i < parts.size(); ++i) parts[i]->setBounds (0, i * 58, w, 54);
    partsHolder.setSize (w, parts.size() * 58);
}

} // namespace studio::ui
