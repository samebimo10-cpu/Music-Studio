#pragma once

#include "../Screen.h"

namespace studio::ui
{

class DrumGrid;

class BeatLabScreen : public Screen, private juce::MidiKeyboardState::Listener
{
public:
    explicit BeatLabScreen (Session&);
    ~BeatLabScreen() override;
    juce::String title() const override { return "Beat lab"; }
    juce::String subtitle() const override { return "Generate a groove, then reshape each part. Everything stays editable MIDI."; }
    void refresh (bool structural) override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct PartRow;
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void generate();
    void playPad (int index, bool down);
    void recordPadChord (int index);

    juce::ComboBox style, key, mood;
    juce::Slider tempo, length;
    juce::Label styleL, keyL, moodL, tempoL, lengthL, partsTitle, padsTitle, gridTitle;
    juce::TextButton generateButton { "Generate" }, newChords { "New chords" };
    juce::OwnedArray<PartRow> parts;
    juce::Component partsHolder;
    juce::Viewport partsView;
    std::unique_ptr<DrumGrid> grid;
    juce::OwnedArray<juce::TextButton> pads;
    juce::ComboBox padMode;
    juce::TextButton padSevenths { "7ths" }, padRecord { "Write to clip" };
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    std::vector<int> padNotesDown;
};

} // namespace studio::ui
