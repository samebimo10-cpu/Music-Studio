#pragma once

#include "../Screen.h"

namespace studio::ui
{

class MasterScreen : public Screen, private juce::Timer
{
public:
    explicit MasterScreen (Session&);
    ~MasterScreen() override;
    juce::String title() const override { return "Master"; }
    juce::String subtitle() const override { return "Pick a preset or press AI master. Streaming targets: about -14 LUFS, peaks at or below -1 dBTP."; }
    void refresh (bool structural) override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void runAiMaster();
    void albumMastering();
    void rebuildParams();

    juce::OwnedArray<juce::TextButton> presets;
    juce::TextButton ai { "AI master" }, ab { "A/B: hearing master" }, resetMeter { "Reset meter" }, album { "Album mastering..." };
    juce::Slider target, ceiling;
    juce::Label targetL, ceilingL, notes, loudnessTitle;
    std::unique_ptr<ParamPanel> params;
    juce::Viewport paramView;
    LevelMeter meter;
    float lastBeforeLufs = 0, lastAfterLufs = 0;
    juce::String meterText;
    std::vector<std::pair<juce::String, float>> platformRows;
    juce::ThreadPool worker { 1 };
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace studio::ui
