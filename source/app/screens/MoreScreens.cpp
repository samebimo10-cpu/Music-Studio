#include "MoreScreens.h"
#include "core/analysis/AudioAnalysis.h"
#include "core/analysis/PitchDetector.h"
#include "core/engine/PluginHost.h"
#include "core/lyrics/LyricsTools.h"
#include "core/midi/MidiTools.h"
#include "core/vocal/VocalTools.h"

namespace studio::ui
{

//==============================================================================
// Lyrics pad
//==============================================================================
LyricsScreen::LyricsScreen (Session& s) : Screen (s)
{
    editor.setMultiLine (true);
    editor.setReturnKeyStartsNewLine (true);
    editor.setFont (juce::FontOptions (18.0f));
    editor.addListener (this);
    editor.setTextToShowWhenEmpty ("[Verse 1]\nWrite your first line here...\n\n[Chorus]\n...", colours::textDim);
    addAndMakeVisible (editor);

    for (auto* e : { &syllables, &rhymes, &chart })
    {
        e->setMultiLine (true);
        e->setReadOnly (true);
        e->setFont (juce::FontOptions (14.0f));
        addAndMakeVisible (e);
    }
    syllables.setFont (juce::FontOptions (18.0f));
    syllables.setColour (juce::TextEditor::textColourId, colours::textDim);
    for (auto* l : { &rhymeTitle, &chartTitle, &syllableTitle }) { styleHeading (*l, 14); addAndMakeVisible (l); }
    rhymeTitle.setText ("Rhymes", juce::dontSendNotification);
    chartTitle.setText ("Chord chart", juce::dontSendNotification);
    syllableTitle.setText ("Syl.", juce::dontSendNotification);
    syllables.setTooltip ("Syllables per line - matching counts help lines sing the same way");

    memo.setTooltip ("Records an idea (melody, rhythm) and attaches it to the line the cursor is on");
    memo.onClick = [this]
    {
        if (! session.hasProject()) return;
        const auto line = editor.getText().substring (0, editor.getCaretPosition()).upToLastOccurrenceOf ("\n", false, false).length();
        juce::ignoreUnused (line);
        memo.setEnabled (false);
        memo.setButtonText ("Recording memo (8 s)...");
        session.engine().captureInput (8.0, 0, 1, [this] (juce::AudioBuffer<float> audio)
        {
            memo.setEnabled (true);
            memo.setButtonText ("Record voice memo for this line");
            auto lineIndex = juce::StringArray::fromLines (editor.getText().substring (0, editor.getCaretPosition())).size() - 1;
            auto f = session.getFolder().getChildFile ("memos").getChildFile ("memo_" + juce::String (juce::Time::currentTimeMillis()) + ".wav");
            writeAudioFile (f, audio, session.engine().getSampleRate(), 24);
            const auto rel = store::relativePath (session.getFolder(), f);
            save();
            session.perform ("Voice memo", [rel, lineIndex] (Project& p)
            {
                int k = 0;
                for (auto& sec : p.lyricSheet)
                {
                    ++k; // the section header line
                    for (auto& l : sec.lines) { if (k == lineIndex) { l.memoFile = rel; return; } ++k; }
                }
            }, false);
            session.status ("Voice memo attached to the line.");
        });
    };
    playMemo.onClick = [this]
    {
        const int lineIndex = juce::StringArray::fromLines (editor.getText().substring (0, editor.getCaretPosition())).size() - 1;
        int k = 0;
        for (auto& sec : session.project().lyricSheet)
        {
            ++k;
            for (auto& l : sec.lines)
            {
                if (k == lineIndex && l.memoFile.isNotEmpty()) { store::resolve (session.getFolder(), l.memoFile).startAsProcess(); return; }
                ++k;
            }
        }
        session.status ("No memo on this line.");
    };
    addAndMakeVisible (memo);
    addAndMakeVisible (playMemo);
    startTimerHz (2);
}

void LyricsScreen::refresh (bool)
{
    if (! editor.hasKeyboardFocus (true))
        editor.setText (lyrics::sectionsToText (session.project().lyricSheet), false);
    updateSide();
}

void LyricsScreen::textEditorTextChanged (juce::TextEditor&)
{
    dirty = true;
    updateSide();
}

void LyricsScreen::timerCallback()
{
    // Save a moment after typing stops, and refresh rhymes for the word under the caret.
    if (dirty && ! editor.isMouseButtonDown()) { dirty = false; save(); }
    auto text = editor.getText();
    int pos = editor.getCaretPosition();
    int a = pos, b = pos;
    while (a > 0 && juce::CharacterFunctions::isLetter (text[a - 1])) --a;
    while (b < text.length() && juce::CharacterFunctions::isLetter (text[b])) ++b;
    auto word = editor.getHighlightedText().isNotEmpty() ? editor.getHighlightedText() : text.substring (a, b);
    if (word != lastWord && word.isNotEmpty())
    {
        lastWord = word;
        auto own = juce::StringArray::fromTokens (text, " \n,.!?;:", "");
        auto r = lyrics::findRhymes (word, own);
        rhymes.setText (r.isEmpty() ? "No rhymes found for \"" + word + "\"" : "\"" + word + "\" rhymes with:\n" + r.joinIntoString (", "), false);
    }
}

void LyricsScreen::save()
{
    auto sections = lyrics::parseSections (editor.getText());
    // Keep chords and memos attached to unchanged lines.
    auto old = session.project().lyricSheet;
    for (auto& s : sections)
        for (auto& l : s.lines)
            for (auto& os : old)
                for (auto& ol : os.lines)
                    if (ol.text == l.text) { l.chords = ol.chords; l.memoFile = ol.memoFile; }
    session.perform ("Edit lyrics", [sections] (Project& p) { p.lyricSheet = sections; }, false);
}

void LyricsScreen::updateSide()
{
    juce::String counts;
    for (auto& line : juce::StringArray::fromLines (editor.getText()))
    {
        const auto t = line.trim();
        counts << (t.isEmpty() || t.startsWith ("[") ? juce::String() : juce::String (lyrics::syllablesInLine (t))) << "\n";
    }
    syllables.setText (counts, false);
    chart.setText (lyrics::chordChart (session.project()), false);
}

void LyricsScreen::paint (juce::Graphics& g) { g.fillAll (colours::background); }

void LyricsScreen::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto side = r.removeFromRight (juce::jmin (420, r.getWidth() / 2 - 40));
    r.removeFromRight (12);
    auto buttons = r.removeFromBottom (40);
    memo.setBounds (buttons.removeFromLeft (280).reduced (2));
    playMemo.setBounds (buttons.removeFromLeft (120).reduced (2));
    auto syl = r.removeFromLeft (44);
    syllableTitle.setBounds (syl.removeFromTop (22));
    syllables.setBounds (syl);
    r.removeFromTop (22);
    editor.setBounds (r);
    rhymeTitle.setBounds (side.removeFromTop (22));
    rhymes.setBounds (side.removeFromTop (140));
    side.removeFromTop (12);
    chartTitle.setBounds (side.removeFromTop (22));
    chart.setBounds (side);
}

//==============================================================================
// Tools
//==============================================================================
ToolsScreen::ToolsScreen (Session& s) : Screen (s)
{
    tunerOn.setClickingTogglesState (true);
    tunerOn.setTooltip ("Tunes the selected track's input (guitar, bass, voice)");
    addAndMakeVisible (tunerOn);
    for (auto* l : { &tunerNote, &tunerCents }) { l->setJustificationType (juce::Justification::centred); addAndMakeVisible (l); }
    tunerNote.setFont (juce::FontOptions (64.0f, juce::Font::bold));
    tunerCents.setFont (juce::FontOptions (20.0f));

    metronomeOn.setClickingTogglesState (true);
    metronomeOn.onClick = [this]
    {
        session.engine().setMetronome (metronomeOn.getToggleState());
        if (metronomeOn.getToggleState() && ! session.engine().isPlaying()) session.engine().play();
    };
    metronomeBpm.setRange (40, 220, 1);
    metronomeBpm.setValue (session.project().bpm, juce::dontSendNotification);
    metronomeBpm.setSliderStyle (juce::Slider::LinearHorizontal);
    metronomeBpm.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 24);
    metronomeBpm.onDragEnd = [this] { const double b = metronomeBpm.getValue(); session.perform ("Tempo", [b] (Project& p) { p.bpm = b; }); };
    tap.onClick = [this]
    {
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
        taps.push_back (now);
        if (taps.size() >= 3)
        {
            const double bpm = 60.0 * (double) (taps.size() - 1) / (taps.back() - taps.front());
            metronomeBpm.setValue (std::round (bpm), juce::dontSendNotification);
            results.setText ("Tapped tempo: " + juce::String (bpm, 1) + " BPM (drag the slider to apply it to the song)", juce::dontSendNotification);
        }
    };
    for (auto* c : { (juce::Component*) &metronomeOn, (juce::Component*) &metronomeBpm, (juce::Component*) &tap }) addAndMakeVisible (c);

    detect.onClick = [this] { analyseFile (false); };
    chords.onClick = [this] { analyseFile (true); };
    stems.setTooltip ("Splits a song into vocals, drums, bass and other - for practice tracks or remixing your own old songs");
    stems.onClick = [this] { separate(); };
    importMidi.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Import a MIDI file", juce::File(), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            int n = 0;
            const double at = session.engine().getPositionBeats();
            session.perform ("Import MIDI", [&] (Project& p) { n = midi::importMidiFile (p, f, std::floor (at)); });
            results.setText ("Imported " + juce::String (n) + " MIDI tracks.", juce::dontSendNotification);
        });
    };
    for (auto* b : { &detect, &chords, &stems, &importMidi }) addAndMakeVisible (b);
    results.setJustificationType (juce::Justification::topLeft);
    results.setFont (juce::FontOptions (15.0f));
    addAndMakeVisible (results);
    startTimerHz (15);
}

ToolsScreen::~ToolsScreen() { pool.removeAllJobs (true, 60000); }

void ToolsScreen::timerCallback()
{
    if (! tunerOn.getToggleState() || ! isShowing()) return;
    // Capture short snippets of input continuously and detect pitch.
    static bool capturing = false;
    if (capturing) return;
    capturing = true;
    auto* t = session.project().findTrack (session.selectedTrackId);
    session.engine().captureInput (0.09, t != nullptr ? t->inputChannel : 0, 1, [this] (juce::AudioBuffer<float> audio)
    {
        capturing = false;
        PitchDetector det;
        const double sr = session.engine().getSampleRate();
        det.prepare (sr, 2048, 30.0f, 1200.0f);
        if (audio.getNumSamples() < 2048) return;
        auto r = det.detect (audio.getReadPointer (0, audio.getNumSamples() - 2048));
        if (r.voiced() && r.confidence > 0.8f)
        {
            const float midi = (float) theory::hzToMidi (r.frequency);
            tunerMidi = tunerMidi > 0 ? 0.7f * tunerMidi + 0.3f * midi : midi;
            const int nearest = (int) std::round (tunerMidi);
            const int cents = (int) std::round ((tunerMidi - (float) nearest) * 100.0f);
            tunerNote.setText (theory::noteName (nearest, false), juce::dontSendNotification);
            tunerCents.setText ((cents == 0 ? juce::String ("in tune") : (cents > 0 ? "+" : "") + juce::String (cents) + " cents") + "   ("
                                + juce::String (r.frequency, 1) + " Hz)", juce::dontSendNotification);
            tunerNote.setColour (juce::Label::textColourId, std::abs (cents) <= 5 ? colours::good : colours::warning);
        }
    });
}

void ToolsScreen::analyseFile (bool chordsOnly)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an audio file", juce::File(), "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg;*.m4a");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, chordsOnly] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        results.setText ("Analysing " + f.getFileName() + "...", juce::dontSendNotification);
        pool.addJob ([this, f, chordsOnly]
        {
            juce::AudioBuffer<float> b;
            double sr = 0;
            juce::String text;
            if (! ClipCache::readFile (f, b, sr)) text = "Could not read the file.";
            else
            {
                auto tempo = analysis::detectTempo (b, sr);
                if (! chordsOnly)
                {
                    auto key = analysis::detectKey (b, sr);
                    text << f.getFileName() << "\nKey: " << key.key.toString() << juce::String::formatted ("  (confidence %d%%)", (int) (key.confidence * 100))
                         << "\nTempo: " << juce::String (tempo.bpm, 1) << " BPM" << juce::String::formatted ("  (confidence %d%%)", (int) (tempo.confidence * 100));
                }
                else
                {
                    text << "Chords in " << f.getFileName() << ":\n";
                    for (auto& c : analysis::detectChords (b, sr, 60.0 / tempo.bpm * 2))
                        text << formatTime (c.timeSeconds) << "  " << c.chord.name() << "\n";
                }
            }
            juce::MessageManager::callAsync ([this, text] { results.setText (text, juce::dontSendNotification); });
        });
    });
}

void ToolsScreen::separate()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a song to separate", juce::File(), "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        auto out = (session.hasProject() ? store::audioFolder (session.getFolder()) : session.settings().projectsFolder())
                       .getChildFile (f.getFileNameWithoutExtension() + " stems");
        pool.addJob ([this, f, out]
        {
            juce::AudioBuffer<float> b;
            double sr = 0;
            if (! ClipCache::readFile (f, b, sr)) return;
            auto st = vocal::separateStems (b, sr, [this] (float p)
            {
                juce::MessageManager::callAsync ([this, p] { results.setText ("Separating... " + juce::String ((int) (p * 100)) + "%", juce::dontSendNotification); });
                return true;
            });
            out.createDirectory();
            writeAudioFile (out.getChildFile ("vocals.wav"), st.vocals, sr, 24);
            writeAudioFile (out.getChildFile ("drums.wav"), st.drums, sr, 24);
            writeAudioFile (out.getChildFile ("bass.wav"), st.bass, sr, 24);
            writeAudioFile (out.getChildFile ("other.wav"), st.other, sr, 24);
            juce::MessageManager::callAsync ([this, out]
            {
                results.setText ("Stems written to " + out.getFullPathName() + "\nImport them in Arrange to practise or remix.\n"
                                 "(Built-in DSP separation; an on-device ML separator can replace it for cleaner results.)", juce::dontSendNotification);
            });
        });
    });
}

void ToolsScreen::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    auto r = getLocalBounds().reduced (16);
    paintPanel (g, r.removeFromTop (220).removeFromLeft (r.getWidth() / 2 - 8).toFloat());
}

void ToolsScreen::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto top = r.removeFromTop (220);
    auto tuner = top.removeFromLeft (top.getWidth() / 2 - 8).reduced (12);
    tunerOn.setBounds (tuner.removeFromTop (34).withWidth (120));
    tunerNote.setBounds (tuner.removeFromTop (100));
    tunerCents.setBounds (tuner.removeFromTop (30));
    top.removeFromLeft (16);
    metronomeOn.setBounds (top.removeFromTop (40).withWidth (140).reduced (2));
    metronomeBpm.setBounds (top.removeFromTop (40).reduced (2));
    tap.setBounds (top.removeFromTop (40).withWidth (140).reduced (2));
    r.removeFromTop (16);
    auto bar = r.removeFromTop (40);
    for (auto* b : { &detect, &chords, &stems, &importMidi }) b->setBounds (bar.removeFromLeft (230).reduced (2));
    r.removeFromTop (12);
    results.setBounds (r);
}

//==============================================================================
// Settings
//==============================================================================
SettingsScreen::SettingsScreen (Session& s) : Screen (s)
{
    deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (session.engine().getDeviceManager(), 1, 16, 2, 2, true, false, true, false);
    addAndMakeVisible (*deviceSelector);
    for (auto* l : { &latency, &storage, &plugins, &plan, &uiLabel }) { l->setJustificationType (juce::Justification::topLeft); l->setFont (juce::FontOptions (14.0f)); addAndMakeVisible (l); }

    projectsFolder.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Where new projects are saved", session.settings().projectsFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().isDirectory()) { session.settings().setProjectsFolder (fc.getResult()); refresh (true); }
        });
    };
    scanPlugins.onClick = [this]
    {
        scanPlugins.setEnabled (false);
        plugins.setText ("Scanning plugins (this can take a minute)...", juce::dontSendNotification);
        PluginHost::getInstance().scanAsync ([this] (int n) { scanPlugins.setEnabled (true); refresh (true); session.status (juce::String (n) + " plugins available."); });
    };
    makeToggle (guided, "Guided mode by default", session.settings().guidedByDefault());
    guided.onClick = [this] { session.settings().props().setValue ("guided", guided.getToggleState()); };
    uiScale.setRange (0.8, 1.6, 0.05);
    uiScale.setValue (session.settings().uiScale(), juce::dontSendNotification);
    uiScale.setSliderStyle (juce::Slider::LinearHorizontal);
    uiScale.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 22);
    uiScale.onDragEnd = [this]
    {
        session.settings().props().setValue ("uiScale", uiScale.getValue());
        juce::Desktop::getInstance().setGlobalScaleFactor ((float) uiScale.getValue());
    };
    for (auto* c : { (juce::Component*) &projectsFolder, (juce::Component*) &scanPlugins, (juce::Component*) &guided, (juce::Component*) &uiScale })
        addAndMakeVisible (c);
}

void SettingsScreen::refresh (bool)
{
    auto& e = session.engine();
    juce::String lat;
    if (auto* dev = e.getDeviceManager().getCurrentAudioDevice())
        lat << "Round-trip latency about " << juce::String (e.getRoundTripLatencyMs(), 1) << " ms at " << dev->getCurrentBufferSizeSamples() << " samples, "
            << (int) dev->getCurrentSampleRate() << " Hz.\n"
            << (e.getRoundTripLatencyMs() <= 10 ? "Good for live monitoring." : "For monitoring under 10 ms, choose 128 samples (ASIO on Windows, Core Audio on Mac).")
            << "\nCPU " << juce::String ((int) (e.getCpuLoad() * 100)) << "%, dropouts this session: " << e.getXruns();
    else
        lat = "No audio device open. Choose one above.";
    latency.setText (lat, juce::dontSendNotification);
    storage.setText ("Projects: " + session.settings().projectsFolder().getFullPathName()
                     + "\nLibrary index: " + LibraryIndex::defaultLocation().getFullPathName(), juce::dontSendNotification);
    plugins.setText (juce::String ((int) PluginHost::getInstance().getKnownPlugins().getNumTypes()) + " plugins known (VST3"
#if JUCE_MAC
                     + juce::String (", AU")
#endif
                     + "). Third-party instruments and effects run on any track.", juce::dontSendNotification);
    plan.setText ("Plan: Free (local). Recording, beats, mixing, mastering and export work offline with no account.\n"
                  "Cloud sync, collaboration, in-app distribution and the marketplace arrive with accounts in build phase 4.", juce::dontSendNotification);
    uiLabel.setText ("Interface size", juce::dontSendNotification);
}

void SettingsScreen::paint (juce::Graphics& g) { g.fillAll (colours::background); }

void SettingsScreen::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto left = r.removeFromLeft (juce::jmin (560, r.getWidth() / 2));
    deviceSelector->setBounds (left.removeFromTop (juce::jmin (460, left.getHeight() - 90)));
    latency.setBounds (left);
    r.removeFromLeft (24);
    storage.setBounds (r.removeFromTop (50));
    projectsFolder.setBounds (r.removeFromTop (36).withWidth (180).reduced (2));
    r.removeFromTop (16);
    plugins.setBounds (r.removeFromTop (44));
    scanPlugins.setBounds (r.removeFromTop (36).withWidth (160).reduced (2));
    r.removeFromTop (16);
    guided.setBounds (r.removeFromTop (36).withWidth (220).reduced (2));
    auto ui = r.removeFromTop (36);
    uiLabel.setBounds (ui.removeFromLeft (110));
    uiScale.setBounds (ui.withWidth (260));
    r.removeFromTop (16);
    plan.setBounds (r.removeFromTop (80));
}

} // namespace studio::ui
