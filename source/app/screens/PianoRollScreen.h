#pragma once

#include "../Screen.h"

namespace studio::ui
{

class NoteGrid;

class PianoRollScreen : public Screen
{
public:
    explicit PianoRollScreen (Session&);
    ~PianoRollScreen() override;
    juce::String title() const override { return "Piano roll"; }
    juce::String subtitle() const override { return "Click to add notes, drag to move or resize, right-click or Delete to remove. Scale notes are highlighted."; }
    void refresh (bool structural) override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    std::unique_ptr<NoteGrid> grid;
    juce::Label clipInfo, chordInfo;
    juce::ComboBox gridSize, clipChoice;
    juce::TextButton quantize { "Quantize" }, humanize { "Humanize" }, legato { "Legato" }, toKey { "Snap to key" },
        transposeUp { "+1" }, transposeDown { "-1" }, octaveUp { "+12" }, octaveDown { "-12" }, detect { "Detect chords" };
    juce::Slider strength;
};

} // namespace studio::ui
