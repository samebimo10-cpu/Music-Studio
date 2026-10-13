#pragma once

#include "../Screen.h"

namespace studio::ui
{

class LyricsScreen : public Screen, private juce::TextEditor::Listener, private juce::Timer
{
public:
    explicit LyricsScreen (Session&);
    juce::String title() const override { return "Lyrics"; }
    juce::String subtitle() const override { return "Write in sections ([Verse 1], [Chorus]). Syllables are counted per line; select a word for rhymes."; }
    void refresh (bool structural) override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void timerCallback() override;
    void updateSide();
    void save();

    juce::TextEditor editor, syllables, rhymes, chart;
    juce::Label rhymeTitle, chartTitle, syllableTitle;
    juce::TextButton showChart { "Chord chart" }, memo { "Record voice memo for this line" }, playMemo { "Play memo" };
    bool dirty = false;
    juce::String lastWord;
};

class ToolsScreen : public Screen, private juce::Timer
{
public:
    explicit ToolsScreen (Session&);
    ~ToolsScreen() override;
    juce::String title() const override { return "Tools"; }
    juce::String subtitle() const override { return "Tuner, metronome, key and tempo detector, chord-from-audio, stem separation."; }
    void refresh (bool) override {}
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void analyseFile (bool chordsOnly);
    void separate();

    juce::TextButton tunerOn { "Tuner on" }, tap { "Tap tempo" }, metronomeOn { "Metronome" }, detect { "Detect key & tempo of a file..." },
        chords { "Chords from audio..." }, stems { "Separate stems..." }, importMidi { "Import MIDI file..." };
    juce::Slider metronomeBpm;
    juce::Label tunerNote, tunerCents, results;
    std::vector<double> taps;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool pool { 1 };
    std::vector<float> tunerBuffer;
    float tunerMidi = 0;
};

class SettingsScreen : public Screen
{
public:
    explicit SettingsScreen (Session&);
    juce::String title() const override { return "Settings"; }
    juce::String subtitle() const override { return "Audio device and buffer size, storage, plugins and your plan."; }
    void refresh (bool) override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    juce::Label latency, storage, plugins, plan, uiLabel;
    juce::TextButton projectsFolder { "Projects folder..." }, scanPlugins { "Scan plugins" }, guided { "Guided mode by default" };
    juce::Slider uiScale;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace studio::ui
