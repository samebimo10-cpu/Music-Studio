#pragma once

#include "AppCommon.h"

namespace studio::ui
{

/** Dark theme for long sessions. Meter colours are colour-blind safe (blue -> yellow -> orange, never red vs green). */
namespace colours
{
    const juce::Colour background   { 0xff121318 };
    const juce::Colour panel        { 0xff1a1c23 };
    const juce::Colour panelRaised  { 0xff22252e };
    const juce::Colour line         { 0xff2e323d };
    const juce::Colour text         { 0xffe8eaf0 };
    const juce::Colour textDim      { 0xff9aa0ae };
    const juce::Colour accent       { 0xff4f8cff };
    const juce::Colour accentWarm   { 0xffffb547 };
    const juce::Colour record       { 0xffff4d5e };
    const juce::Colour meterLow     { 0xff3fa7ff };
    const juce::Colour meterMid     { 0xffffd23f };
    const juce::Colour meterHot     { 0xffff6b35 };
    const juce::Colour good         { 0xff3fa7ff };
    const juce::Colour warning      { 0xffffb547 };
    const juce::Colour error        { 0xffff6b35 };
}

class StudioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StudioLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};

/** Small helpers so screens look consistent. */
void styleHeading (juce::Label& l, float size = 22.0f);
void styleCaption (juce::Label& l);
void paintPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 8.0f);
juce::String formatTime (double seconds);
juce::String formatDb (float db);

} // namespace studio::ui
