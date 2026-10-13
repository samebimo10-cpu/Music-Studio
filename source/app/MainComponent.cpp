#include "MainComponent.h"
#include "screens/HomeScreens.h"
#include "screens/ArrangeScreen.h"
#include "screens/RecordScreen.h"
#include "screens/VocalBoothScreen.h"
#include "screens/BeatLabScreen.h"
#include "screens/PianoRollScreen.h"
#include "screens/MixerScreen.h"
#include "screens/MasterScreen.h"
#include "screens/ReleaseScreen.h"
#include "screens/LibraryScreen.h"
#include "screens/MoreScreens.h"

namespace studio::ui
{

const std::vector<std::pair<juce::String, juce::String>>& MainComponent::guidedSteps()
{
    static const std::vector<std::pair<juce::String, juce::String>> s {
        { "newsong", "1 Idea" }, { "beatlab", "2 Beat" }, { "record", "3 Record" }, { "vocal", "4 Vocals" },
        { "mixer", "5 Mix" }, { "master", "6 Master" }, { "release", "7 Release" }
    };
    return s;
}

MainComponent::MainComponent (Session& s) : session (s)
{
    auto add = [this] (const juce::String& id, const juce::String& label, const juce::String& key, Screen* screen)
    {
        Entry e;
        e.id = id;
        e.label = label;
        e.shortcut = key;
        e.screen.reset (screen);
        e.screen->navigate = [this] (const juce::String& target) { show (target); };
        addChildComponent (*e.screen);
        e.button = std::make_unique<juce::TextButton> (label);
        e.button->setClickingTogglesState (true);
        e.button->setRadioGroupId (1);
        e.button->setTooltip (label + (key.isNotEmpty() ? "  (" + key + ")" : juce::String()));
        e.button->onClick = [this, id] { show (id); };
        addAndMakeVisible (*e.button);
        entries.push_back (std::move (e));
    };
    add ("home", "Home", "Ctrl+1", new HomeScreen (s));
    add ("newsong", "New song", "Ctrl+N", new NewSongScreen (s));
    add ("arrange", "Arrange", "Ctrl+2", new ArrangeScreen (s));
    add ("record", "Record", "Ctrl+3", new RecordScreen (s));
    add ("vocal", "Vocal booth", "Ctrl+4", new VocalBoothScreen (s));
    add ("beatlab", "Beat lab", "Ctrl+5", new BeatLabScreen (s));
    add ("pianoroll", "Piano roll", "Ctrl+6", new PianoRollScreen (s));
    add ("mixer", "Mixer", "Ctrl+7", new MixerScreen (s));
    add ("master", "Master", "Ctrl+8", new MasterScreen (s));
    add ("release", "Release", "Ctrl+9", new ReleaseScreen (s));
    add ("library", "Library", "", new LibraryScreen (s));
    add ("lyrics", "Lyrics", "", new LyricsScreen (s));
    add ("tools", "Tools", "", new ToolsScreen (s));
    add ("settings", "Settings", "Ctrl+,", new SettingsScreen (s));

    play.setTooltip ("Play / pause (Space)");
    play.onClick = [this] { session.togglePlay(); };
    stop.setTooltip ("Stop and return (Enter)");
    stop.onClick = [this] { session.stopAll(); session.engine().setPositionBeats (0); };
    home.setTooltip ("Back to the start (Home)");
    home.onClick = [this] { session.engine().setPositionBeats (0); };
    record.setColour (juce::TextButton::buttonColourId, colours::record.withAlpha (0.7f));
    record.setTooltip ("Record armed tracks (R)");
    record.onClick = [this] { session.toggleRecord(); };
    makeToggle (loop, "Loop", false);
    loop.setTooltip ("Loop the region set in the Arrange ruler (L)");
    loop.onClick = [this]
    {
        auto& e = session.engine();
        const double a = session.recordSettings.punchInBeat, b = session.recordSettings.punchOutBeat;
        e.setLoop (loop.getToggleState(), a, b > a ? b : a + session.project().beatsPerBar() * 4);
    };
    makeToggle (click, "Click", false);
    click.setTooltip ("Metronome (M)");
    click.onClick = [this] { session.engine().setMetronome (click.getToggleState()); };
    undo.onClick = [this] { session.undo(); };
    redo.onClick = [this] { session.redo(); };
    save.onClick = [this] { session.save(); };
    makeToggle (guidedToggle, "Guided", true);
    guidedToggle.setTooltip ("Guided walks you from idea to release in steps; turn off for the full Studio workspace");
    guidedToggle.onClick = [this] { guided = guidedToggle.getToggleState(); resized(); };
    for (auto* c : { &home, &play, &stop, &record, &loop, &click, &undo, &redo, &save, &guidedToggle }) addAndMakeVisible (c);

    position.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    position.setJustificationType (juce::Justification::centred);
    songInfo.setFont (juce::FontOptions (13.0f));
    songInfo.setColour (juce::Label::textColourId, colours::textDim);
    cpu.setFont (juce::FontOptions (12.0f));
    cpu.setColour (juce::Label::textColourId, colours::textDim);
    status.setFont (juce::FontOptions (13.0f));
    status.setColour (juce::Label::textColourId, colours::textDim);
    styleHeading (titleLabel, 20);
    styleCaption (subtitleLabel);
    for (auto* l : { &position, &songInfo, &cpu, &status, &titleLabel, &subtitleLabel }) addAndMakeVisible (l);
    masterMeter.source = masterMeterSource (session.engine());
    addAndMakeVisible (masterMeter);

    for (auto& [id, label] : guidedSteps())
    {
        auto* b = steps.add (new juce::TextButton (label));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (2);
        b->onClick = [this, id = id] { show (id); };
        addAndMakeVisible (b);
    }

    guided = session.settings().guidedByDefault();
    guidedToggle.setToggleState (guided, juce::dontSendNotification);
    session.onStatus = [this] (const juce::String& m) { status.setText (m, juce::dontSendNotification); statusHold = 80; };
    session.addChangeListener (this);
    setWantsKeyboardFocus (true);
    show ("home");
    startTimerHz (15);
}

MainComponent::~MainComponent()
{
    session.removeChangeListener (this);
    session.onStatus = nullptr;
}

juce::StringArray MainComponent::screenIds() const
{
    juce::StringArray ids;
    for (auto& e : entries) ids.add (e.id);
    return ids;
}

Screen* MainComponent::screen (const juce::String& id)
{
    for (auto& e : entries) if (e.id == id) return e.screen.get();
    return nullptr;
}

void MainComponent::show (const juce::String& id)
{
    // Screens that edit a song need one open.
    if (! session.hasProject() && id != "home" && id != "newsong" && id != "settings" && id != "library" && id != "tools")
    {
        session.status ("Create or open a song first.");
        if (current.isEmpty()) current = "home";
        return;
    }
    current = id;
    for (auto& e : entries)
    {
        const bool on = e.id == id;
        e.button->setToggleState (on, juce::dontSendNotification);
        e.screen->setVisible (on);
        if (on)
        {
            titleLabel.setText (e.screen->title(), juce::dontSendNotification);
            subtitleLabel.setText (e.screen->subtitle(), juce::dontSendNotification);
            e.screen->setTitle (e.screen->title());
            e.screen->setDescription (e.screen->subtitle());
        }
    }
    const auto& gs = guidedSteps();
    for (size_t i = 0; i < gs.size(); ++i) steps[(int) i]->setToggleState (gs[i].first == id, juce::dontSendNotification);
    if (session.hasProject())
    {
        for (size_t i = 0; i < gs.size(); ++i)
            if (gs[i].first == id && (int) i > session.project().guidedStep)
                session.perform ("Guided step", [i] (Project& p) { p.guidedStep = (int) i; }, false);
    }
    resized();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateTransport();
}

void MainComponent::updateTransport()
{
    auto& e = session.engine();
    const auto& p = session.project();
    const double beat = e.getPositionBeats();
    const double bpb = p.beatsPerBar();
    position.setText (juce::String::formatted ("%d.%d   %s", (int) (beat / bpb) + 1, (int) std::fmod (beat, bpb) + 1, formatTime (e.getPositionSeconds()).toRawUTF8()),
                      juce::dontSendNotification);
    songInfo.setText (session.hasProject() ? p.name + (session.isDirty() ? " *" : "") + "\n" + juce::String (p.bpm, 0) + " BPM  " + p.keyAt (beat).toString()
                                           : juce::String ("No song open"), juce::dontSendNotification);
    play.setButtonText (e.isPlaying() ? "Pause" : "Play");
    record.setButtonText (e.isCountingIn() ? "Count" : e.isRecording() ? "Stop rec" : "Rec");
    record.setColour (juce::TextButton::buttonColourId, e.isRecording() ? colours::record : colours::record.withAlpha (0.55f));
    undo.setEnabled (session.history().canUndo());
    redo.setEnabled (session.history().canRedo());
    undo.setTooltip ("Undo " + session.history().undoDescription() + " (Ctrl+Z)");
    redo.setTooltip ("Redo " + session.history().redoDescription() + " (Ctrl+Shift+Z)");
    click.setToggleState (e.isMetronomeOn(), juce::dontSendNotification);
    loop.setToggleState (e.isLooping(), juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    updateTransport();
    auto& e = session.engine();
    cpu.setText ("CPU " + juce::String ((int) (e.getCpuLoad() * 100)) + "%" + (e.getXruns() > 0 ? "  dropouts " + juce::String (e.getXruns()) : juce::String()),
                 juce::dontSendNotification);
    if (statusHold > 0 && --statusHold == 0) status.setText ({}, juce::dontSendNotification);
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const bool cmd = mods.isCommandDown();
    if (k == juce::KeyPress::spaceKey) { session.togglePlay(); return true; }
    if (k == juce::KeyPress::returnKey && ! cmd) { session.stopAll(); session.engine().setPositionBeats (0); return true; }
    if (k == juce::KeyPress::homeKey) { session.engine().setPositionBeats (0); return true; }
    if (cmd && k.getKeyCode() == 'Z') { mods.isShiftDown() ? session.redo() : session.undo(); return true; }
    if (cmd && k.getKeyCode() == 'Y') { session.redo(); return true; }
    if (cmd && k.getKeyCode() == 'S') { session.save(); return true; }
    if (cmd && k.getKeyCode() == 'N') { show ("newsong"); return true; }
    if (cmd && k.getKeyCode() == ',') { show ("settings"); return true; }
    if (cmd && k.getKeyCode() >= '1' && k.getKeyCode() <= '9')
    {
        static const char* ids[] { "home", "arrange", "record", "vocal", "beatlab", "pianoroll", "mixer", "master", "release" };
        show (ids[k.getKeyCode() - '1']);
        return true;
    }
    if (! cmd && k.getKeyCode() == 'R') { session.toggleRecord(); return true; }
    if (! cmd && k.getKeyCode() == 'M') { session.engine().setMetronome (! session.engine().isMetronomeOn()); return true; }
    if (! cmd && k.getKeyCode() == 'L') { loop.triggerClick(); return true; }
    return false;
}

void MainComponent::askToSaveThen (std::function<void()> then)
{
    if (! session.hasProject() || ! session.isDirty()) { then(); return; }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Save changes?")
                                      .withMessage ("Save the changes to \"" + session.project().name + "\" before closing?")
                                      .withButton ("Save").withButton ("Don't save").withButton ("Cancel"),
                                  [this, then] (int r)
    {
        if (r == 1) { session.save(); then(); }
        else if (r == 2) then();
    });
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::panel);
    g.fillRect (0, 0, getWidth(), 56);
    g.fillRect (0, 56, 160, getHeight() - 56);
    g.setColour (colours::line);
    g.drawHorizontalLine (56, 0, (float) getWidth());
    g.drawVerticalLine (160, 56, (float) getHeight());
    g.setColour (colours::accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("Studio", 16, 0, 140, 56, juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (56).reduced (8, 8);
    top.removeFromLeft (152);
    for (auto* b : { &home, &play, &stop, &record })
        b->setBounds (top.removeFromLeft (b == &home ? 40 : 70).reduced (2));
    top.removeFromLeft (8);
    loop.setBounds (top.removeFromLeft (60).reduced (2));
    click.setBounds (top.removeFromLeft (60).reduced (2));
    position.setBounds (top.removeFromLeft (220));
    songInfo.setBounds (top.removeFromLeft (220));
    guidedToggle.setBounds (top.removeFromRight (84).reduced (2));
    save.setBounds (top.removeFromRight (64).reduced (2));
    redo.setBounds (top.removeFromRight (64).reduced (2));
    undo.setBounds (top.removeFromRight (64).reduced (2));
    masterMeter.setBounds (top.removeFromRight (120).reduced (4, 10));
    cpu.setBounds (top.removeFromRight (140));

    auto side = r.removeFromLeft (160).reduced (8);
    for (auto& e : entries)
    {
        e.button->setBounds (side.removeFromTop (38).reduced (0, 2));
        if (e.id == "release" || e.id == "newsong") side.removeFromTop (8);
    }

    status.setBounds (r.removeFromBottom (24).reduced (12, 0));
    auto header = r.removeFromTop (guided ? 92 : 50).reduced (16, 6);
    auto titleRow = header.removeFromTop (40);
    titleLabel.setBounds (titleRow.removeFromLeft (220));
    subtitleLabel.setBounds (titleRow);
    for (auto* b : steps) b->setVisible (guided);
    if (guided)
    {
        const int w = header.getWidth() / juce::jmax (1, steps.size());
        for (auto* b : steps) b->setBounds (header.removeFromLeft (w).reduced (3, 4));
    }
    for (auto& e : entries) e.screen->setBounds (r);
}

} // namespace studio::ui
