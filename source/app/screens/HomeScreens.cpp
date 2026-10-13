#include "HomeScreens.h"
#include "core/beatgen/BeatGenerator.h"

namespace studio::ui
{

struct HomeScreen::IdeaModel : public juce::ListBoxModel
{
    explicit IdeaModel (HomeScreen& o) : owner (o) {}
    int getNumRows() override { return owner.ideas.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (selected) g.fillAll (colours::accent.withAlpha (0.3f));
        if (row < 0 || row >= owner.ideas.size()) return;
        auto& f = owner.ideas.getReference (row);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (f.getFileNameWithoutExtension(), 10, 0, w - 160, h, juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.drawText (f.getLastModificationTime().toString (true, true, false), w - 150, 0, 140, h, juce::Justification::centredRight);
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < owner.ideas.size()) owner.startFromIdea (owner.ideas[row]);
    }
    HomeScreen& owner;
};

HomeScreen::HomeScreen (Session& s) : Screen (s)
{
    heading.setText ("Welcome to Studio", juce::dontSendNotification);
    styleHeading (heading, 30);
    sub.setText ("Record a clean vocal, build the beat, mix, master and release - all in one place, offline.", juce::dontSendNotification);
    styleCaption (sub);
    for (auto* l : { &recentLabel, &templatesLabel, &ideasLabel, &learnLabel }) styleHeading (*l, 16);
    recentLabel.setText ("Recent projects", juce::dontSendNotification);
    templatesLabel.setText ("Templates", juce::dontSendNotification);
    ideasLabel.setText ("Idea inbox", juce::dontSendNotification);
    learnLabel.setText ("Learn", juce::dontSendNotification);
    for (auto* c : { (juce::Component*) &heading, (juce::Component*) &sub, (juce::Component*) &recentLabel, (juce::Component*) &templatesLabel,
                     (juce::Component*) &ideasLabel, (juce::Component*) &learnLabel })
        addAndMakeVisible (c);

    newSong.setColour (juce::TextButton::buttonColourId, colours::accent);
    newSong.setTooltip ("Pick a style, tempo, key and mood - get a full starting beat (Ctrl+N)");
    newSong.onClick = [this] { if (navigate) navigate ("newsong"); };
    addAndMakeVisible (newSong);

    openButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a project folder (.studio)", session.settings().projectsFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            if (f.getFileName() == "project.json") f = f.getParentDirectory();
            openWithRecovery (f);
        });
    };
    addAndMakeVisible (openButton);

    for (auto& name : templates::names())
    {
        auto* b = templateButtons.add (new juce::TextButton (name));
        b->setTooltip ("New project with tracks, effects and routing already set for: " + name);
        b->onClick = [this, name]
        {
            auto p = templates::create (name);
            p.name = name;
            auto r = session.newProject (p);
            if (r.failed()) tell ("Could not create the project", r.getErrorMessage());
            else if (navigate) navigate (name == "Podcast" || name == "Choir rehearsal track" ? "record" : "arrange");
        };
        addAndMakeVisible (b);
    }

    recentList.setRowHeight (34);
    recentList.setColour (juce::ListBox::backgroundColourId, colours::panel);
    addAndMakeVisible (recentList);

    ideaModel = std::make_unique<IdeaModel> (*this);
    ideaList.setModel (ideaModel.get());
    ideaList.setRowHeight (30);
    addAndMakeVisible (ideaList);
    useIdea.onClick = [this] { if (auto r = ideaList.getSelectedRow(); r >= 0 && r < ideas.size()) startFromIdea (ideas[r]); };
    openInbox.onClick = [this] { ideaInbox().createDirectory(); ideaInbox().startAsProcess(); };
    addAndMakeVisible (useIdea);
    addAndMakeVisible (openInbox);

    lessons.setMultiLine (true);
    lessons.setReadOnly (true);
    lessons.setScrollbarsShown (true);
    lessons.setFont (juce::FontOptions (14.0f));
    lessons.setText (
        "YOUR FIRST SONG (Guided mode)\n"
        "1. New song: choose a style like Worship ballad or Nigerian praise, a tempo and a key.\n"
        "2. Beat lab: change any part with Regenerate, Simpler, Busier or Swap feel.\n"
        "3. Record: run the room check, set your level, then record a few takes.\n"
        "4. Vocal booth: pick a preset (Worship lead, Praise lead...) and turn the Amount knob.\n"
        "5. Mixer: press Auto-mix, then read the Mix assistant.\n"
        "6. Master: choose Warm or press AI master. Aim for -14 LUFS.\n"
        "7. Release: fill in the details, add cover art, run the checklist and export.\n\n"
        "WHY COMPRESSION?\n"
        "Singers get louder and softer. A compressor turns the loud lines down so soft lines can sit higher, "
        "and the vocal stays clear without jumping out.\n\n"
        "HOW TO STACK HARMONIES\n"
        "Record the lead first. Use the harmony generator for a third above and a fourth below, or sing them "
        "yourself on new tracks. Pan harmonies left and right, keep them 5-8 dB under the lead, and tighten "
        "their timing with Timing alignment.\n\n"
        "WHY -14 LUFS?\n"
        "Streaming platforms turn loud songs down to about -14 LUFS. Mastering much louder only squashes the song "
        "without making it louder on Spotify or Apple Music.\n\n"
        "HEADPHONES WHILE RECORDING\n"
        "Use closed headphones so the beat does not leak into the mic. Add a little monitor reverb (Record screen) "
        "if it helps you sing - it is never recorded.");
    addAndMakeVisible (lessons);
}

juce::File HomeScreen::ideaInbox() const
{
    return session.settings().projectsFolder().getChildFile ("Idea inbox");
}

void HomeScreen::refresh (bool)
{
    recent = session.settings().recentProjects();
    recentList.updateContent();
    recentList.repaint();
    ideas = ideaInbox().findChildFiles (juce::File::findFiles, false, "*.wav;*.m4a;*.mp3;*.aif;*.aiff;*.flac;*.ogg");
    std::sort (ideas.begin(), ideas.end(), [] (const juce::File& a, const juce::File& b) { return a.getLastModificationTime() > b.getLastModificationTime(); });
    ideaList.updateContent();
    ideaList.repaint();
}

void HomeScreen::startFromIdea (const juce::File& audio)
{
    auto p = templates::blank (audio.getFileNameWithoutExtension());
    if (session.newProject (p).failed()) return;
    session.perform ("Import idea", [this, audio] (Project& pr)
    {
        juce::String trackId;
        store::importAudio (pr, session.getFolder(), audio, 0, {}, &trackId);
        if (auto* t = pr.findTrack (trackId)) { t->name = "Idea"; t->role = TrackRole::LeadVocal; }
    });
    session.status ("Started a song from \"" + audio.getFileNameWithoutExtension() + "\". Try Voice-to-MIDI in the Vocal booth.");
    if (navigate) navigate ("arrange");
}

void HomeScreen::openWithRecovery (const juce::File& folder)
{
    if (store::needsRecovery (folder))
    {
        auto opts = juce::MessageBoxOptions().withTitle ("Recover unsaved work?")
                        .withMessage ("Studio did not close cleanly last time. There are unsaved changes from the autosave "
                                      + store::autosaveFile (folder).getLastModificationTime().toString (true, true) + ". Recover them?")
                        .withButton ("Recover").withButton ("Open last save").withIconType (juce::MessageBoxIconType::QuestionIcon);
        juce::AlertWindow::showAsync (opts, [this, folder] (int result)
        {
            auto r = session.open (folder);
            if (r.failed()) { tell ("Could not open", r.getErrorMessage()); return; }
            if (result == 1)
            {
                Project rec;
                if (store::recover (folder, rec).wasOk())
                    session.perform ("Recover autosave", [rec] (Project& p) { p = rec; });
            }
            if (navigate) navigate ("arrange");
        });
        return;
    }
    auto r = session.open (folder);
    if (r.failed()) tell ("Could not open", r.getErrorMessage());
    else if (navigate) navigate ("arrange");
}

int HomeScreen::getNumRows() { return recent.size(); }

void HomeScreen::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (selected) g.fillAll (colours::accent.withAlpha (0.3f));
    if (row < 0 || row >= recent.size()) return;
    juce::File f (recent[row]);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (f.getFileNameWithoutExtension(), 12, 0, w - 200, h, juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (store::projectFile (f).getLastModificationTime().toString (true, true, false), w - 190, 0, 180, h, juce::Justification::centredRight);
}

void HomeScreen::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < recent.size()) openWithRecovery (juce::File (recent[row]));
}

void HomeScreen::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void HomeScreen::resized()
{
    auto r = getLocalBounds().reduced (24);
    heading.setBounds (r.removeFromTop (40));
    sub.setBounds (r.removeFromTop (24));
    r.removeFromTop (12);
    auto buttons = r.removeFromTop (52);
    newSong.setBounds (buttons.removeFromLeft (220));
    buttons.removeFromLeft (12);
    openButton.setBounds (buttons.removeFromLeft (180));
    r.removeFromTop (16);

    auto right = r.removeFromRight (juce::jmax (320, r.getWidth() * 2 / 5));
    r.removeFromRight (20);

    templatesLabel.setBounds (r.removeFromTop (26));
    auto trow = r.removeFromTop (44);
    const int tw = trow.getWidth() / juce::jmax (1, templateButtons.size());
    for (auto* b : templateButtons) b->setBounds (trow.removeFromLeft (tw).reduced (4, 0));
    r.removeFromTop (16);
    recentLabel.setBounds (r.removeFromTop (26));
    auto ideasArea = r.removeFromBottom (juce::jmin (220, r.getHeight() / 2));
    recentList.setBounds (r);
    ideasArea.removeFromTop (12);
    ideasLabel.setBounds (ideasArea.removeFromTop (26));
    auto ib = ideasArea.removeFromBottom (36);
    useIdea.setBounds (ib.removeFromLeft (230));
    ib.removeFromLeft (8);
    openInbox.setBounds (ib.removeFromLeft (170));
    ideaList.setBounds (ideasArea.withTrimmedBottom (6));

    learnLabel.setBounds (right.removeFromTop (26));
    lessons.setBounds (right);
}

//==============================================================================
NewSongScreen::NewSongScreen (Session& s) : Screen (s)
{
    heading.setText ("New song", juce::dontSendNotification);
    styleHeading (heading, 28);
    sub.setText (subtitle(), juce::dontSendNotification);
    styleCaption (sub);
    addAndMakeVisible (heading);
    addAndMakeVisible (sub);

    auto label = [this] (juce::Label& l, const juce::String& t) { l.setText (t, juce::dontSendNotification); styleHeading (l, 15); addAndMakeVisible (l); };
    label (nameLabel, "Song name");
    label (styleLabel, "Style");
    label (tempoLabel, "Tempo (BPM)");
    label (keyLabel, "Key");
    label (moodLabel, "Mood");
    label (lengthLabel, "Length");

    name.setText ("My new song");
    name.setFont (juce::FontOptions (16.0f));
    addAndMakeVisible (name);

    int id = 1;
    for (auto& st : beatgen::styles()) style.addItem (st.name, id++);
    style.setSelectedId (1, juce::dontSendNotification);
    style.onChange = [this] { styleChanged(); };
    addAndMakeVisible (style);
    styleCaption (styleInfo);
    addAndMakeVisible (styleInfo);

    tempo.setSliderStyle (juce::Slider::LinearHorizontal);
    tempo.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 24);
    tempo.setRange (40, 200, 1);
    addAndMakeVisible (tempo);

    for (int pc = 0; pc < 12; ++pc)
    {
        key.addItem (theory::pitchClassName (pc) + " major", pc + 1);
        key.addItem (theory::pitchClassName (pc) + " minor", pc + 13);
    }
    addAndMakeVisible (key);

    for (auto* b : { &joyful, &reflective, &intense })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (77);
        addAndMakeVisible (b);
    }
    joyful.setToggleState (true, juce::dontSendNotification);
    joyful.onClick = [this] { mood = "joyful"; };
    reflective.onClick = [this] { mood = "reflective"; };
    intense.onClick = [this] { mood = "intense"; };

    length.setSliderStyle (juce::Slider::LinearHorizontal);
    length.setRange (90, 420, 15);
    length.setValue (210);
    length.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 24);
    length.textFromValueFunction = [] (double v) { return juce::String::formatted ("%d:%02d", (int) v / 60, (int) v % 60); };
    addAndMakeVisible (length);

    generate.setColour (juce::TextButton::buttonColourId, colours::accent);
    generate.onClick = [this] { create (true); };
    blank.onClick = [this] { create (false); };
    addAndMakeVisible (generate);
    addAndMakeVisible (blank);
    styleChanged();
}

void NewSongScreen::styleChanged()
{
    const auto& st = beatgen::styles()[(size_t) juce::jmax (0, style.getSelectedId() - 1)];
    tempo.setValue (st.bpmDefault, juce::dontSendNotification);
    styleInfo.setText (st.description + "  Typical tempo " + juce::String ((int) st.bpmMin) + "-" + juce::String ((int) st.bpmMax)
                       + " BPM" + (st.halfTimeAvailable ? " (or 140-180 half-time)" : juce::String()) + ".", juce::dontSendNotification);
    auto k = Key::fromString (st.defaultKey);
    key.setSelectedId (k.tonic + (k.isMinor() ? 13 : 1), juce::dontSendNotification);
}

void NewSongScreen::create (bool withBeat)
{
    auto p = templates::blank (name.getText().trim().isEmpty() ? juce::String ("My new song") : name.getText().trim());
    auto& lead = p.addTrack (TrackType::Audio, "Lead vocal", TrackRole::LeadVocal);
    lead.vocalChain.enabled = true;
    lead.armed = true;
    lead.sends.push_back ({ mixing::ensureBus (p, "Reverb").id, -14.0f, false });

    const int keyId = key.getSelectedId();
    const juce::String keyName = theory::pitchClassName ((keyId - 1) % 12) + (keyId > 12 ? " minor" : " major");
    p.key = Key::fromString (keyName);
    p.bpm = tempo.getValue();
    p.mood = mood;
    const auto& st = beatgen::styles()[(size_t) juce::jmax (0, style.getSelectedId() - 1)];
    p.style = st.id;
    lead.vocalChain.preset = st.id.contains ("worship") ? "Worship lead" : st.id == "nigerian_praise" ? "Praise lead"
                           : st.id == "hiphop_trap" ? "Rap" : st.id.contains ("afro") || st.id == "amapiano" ? "Afro pop" : "Worship lead";

    if (withBeat)
    {
        beatgen::Settings bs;
        bs.style = st.id;
        bs.bpm = p.bpm;
        bs.key = keyName;
        bs.mood = mood;
        bs.lengthSeconds = length.getValue();
        beatgen::BeatGenerator::generate (p, bs);
    }
    auto r = session.newProject (p);
    if (r.failed()) { tell ("Could not create the song", r.getErrorMessage()); return; }
    session.status (withBeat ? "Your starting beat is ready. Change any part, then record." : "Empty song created.");
    if (navigate) navigate (withBeat ? "beatlab" : "arrange");
}

void NewSongScreen::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    paintPanel (g, getLocalBounds().reduced (24).withTrimmedTop (80).withWidth (juce::jmin (760, getWidth() - 48)).toFloat());
}

void NewSongScreen::resized()
{
    auto r = getLocalBounds().reduced (24);
    heading.setBounds (r.removeFromTop (40));
    sub.setBounds (r.removeFromTop (24));
    r.removeFromTop (16);
    r = r.withWidth (juce::jmin (760, r.getWidth())).reduced (20);
    auto row = [&] (juce::Label& l, int h) { l.setBounds (r.removeFromTop (22)); auto b = r.removeFromTop (h); r.removeFromTop (12); return b; };
    name.setBounds (row (nameLabel, 34));
    style.setBounds (row (styleLabel, 34));
    styleInfo.setBounds (r.removeFromTop (22));
    r.removeFromTop (8);
    tempo.setBounds (row (tempoLabel, 34));
    key.setBounds (row (keyLabel, 34).withWidth (220));
    auto moods = row (moodLabel, 40);
    for (auto* b : { &joyful, &reflective, &intense }) b->setBounds (moods.removeFromLeft (140).reduced (0, 0).withTrimmedRight (8));
    length.setBounds (row (lengthLabel, 34));
    r.removeFromTop (8);
    auto buttons = r.removeFromTop (48);
    generate.setBounds (buttons.removeFromLeft (260));
    buttons.removeFromLeft (12);
    blank.setBounds (buttons.removeFromLeft (160));
}

} // namespace studio::ui
