#pragma once

#include "Theme.h"
#include "core/dsp/Effect.h"
#include "core/engine/MixGraph.h"

namespace studio { class AudioEngine; }

namespace studio::ui
{

/** Peak/RMS meter with peak hold. Reads values through a callback so it survives graph rebuilds. */
class LevelMeter : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    LevelMeter (bool vertical = true);
    /** Returns {peakL, peakR, gainReductionDb}; called 30 times a second. */
    std::function<std::array<float, 3>()> source;
    void paint (juce::Graphics&) override;
    void setShowScale (bool s) { showScale = s; }

private:
    void timerCallback() override;
    bool vertical, showScale = false;
    float levels[2] { -70.0f, -70.0f }, holds[2] { -70.0f, -70.0f }, gr = 0;
    int holdTimers[2] {};
};

/** Reads a track or master meter from the engine's current graph. */
std::function<std::array<float, 3>()> trackMeterSource (studio::AudioEngine& engine, const juce::String& trackId);
std::function<std::array<float, 3>()> masterMeterSource (studio::AudioEngine& engine);

/** A labelled rotary knob. */
class Knob : public juce::Component
{
public:
    Knob (const juce::String& label, double min, double max, double value, const juce::String& suffix = {});
    juce::Slider slider;
    juce::Label label;
    void resized() override;
};

/** Generic editor for any dsp::Effect's parameters: knobs or choice boxes, with tooltips. */
class ParamPanel : public juce::Component
{
public:
    ParamPanel (const std::vector<dsp::ParamInfo>& params, std::function<float (const juce::String&)> getter,
                std::function<void (const juce::String&, float, bool)> setter, int columns = 6);
    void resized() override;
    void refreshValues();
    int preferredHeight (int width) const;

private:
    struct Row
    {
        dsp::ParamInfo info;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::Label> label;
    };
    std::vector<std::unique_ptr<Row>> rows;
    std::function<float (const juce::String&)> get;
    std::function<void (const juce::String&, float, bool)> set;
    int columns;
};

/** A titled card that hosts a content component. */
class Card : public juce::Component
{
public:
    explicit Card (const juce::String& title = {}) : title (title) {}
    void paint (juce::Graphics& g) override;
    juce::Rectangle<int> content() const { return getLocalBounds().reduced (12).withTrimmedTop (title.isNotEmpty() ? 26 : 0); }
    juce::String title;
};

/** Simple list of checks/issues with coloured markers (used by release and mix assistant screens). */
class IssueList : public juce::Component
{
public:
    struct Item
    {
        int level = 0; // 0 ok, 1 warning, 2 error/fix
        juce::String title, detail;
        std::function<void()> action;
        juce::String actionLabel;
    };
    void setItems (std::vector<Item> items);
    void paint (juce::Graphics&) override;
    void resized() override;
    int preferredHeight() const;

private:
    std::vector<Item> items;
    juce::OwnedArray<juce::TextButton> buttons;
};

/** Makes a TextButton that toggles a bool. */
void makeToggle (juce::TextButton& b, const juce::String& text, bool initial);

} // namespace studio::ui
