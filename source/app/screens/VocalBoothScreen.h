#pragma once

#include "../Screen.h"

namespace studio::ui
{

class VocalBoothScreen : public Screen
{
public:
    explicit VocalBoothScreen (Session&);
    juce::String title() const override { return "Vocal booth"; }
    juce::String subtitle() const override { return "Pick a preset and turn Amount. Open Advanced to adjust any step."; }
    void refresh (bool structural) override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct StepRow;
    void rebuildSteps();
    void apply (bool gestureEnd);
    VocalChainSettings settings() const;

    // Clip tools
    bool selectedClipAudio (juce::AudioBuffer<float>& out, double& sr, const Clip*& clip, const Track*& track);
    void makeHarmonies (int parts);
    void voiceToMidi();
    void alignToLead();
    void openPitchEditor();
    juce::String writeAudio (const juce::AudioBuffer<float>& audio, double sr, const juce::String& name);

    juce::ComboBox trackChoice;
    juce::TextButton enable { "Vocal chain on" }, advanced { "Advanced" };
    juce::OwnedArray<juce::TextButton> presetButtons;
    Knob amount { "Amount", 0, 1, 0.7 };
    juce::OwnedArray<StepRow> steps;
    juce::Viewport stepsView;
    juce::Component stepsHolder;
    juce::Label toolsTitle, info;
    juce::TextButton harmony2 { "2 harmonies" }, harmony3 { "3 harmonies" }, harmony4 { "4 harmonies" }, doubler { "Add doubler" },
        toMidi { "Voice to MIDI" }, align { "Align timing to lead" }, pitchEdit { "Pitch editor..." };
    VocalChainSettings current;
};

} // namespace studio::ui
