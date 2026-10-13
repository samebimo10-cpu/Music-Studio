#include "Theme.h"

namespace studio::ui
{

StudioLookAndFeel::StudioLookAndFeel()
{
    using namespace colours;
    setColourScheme ({ background, panel, panelRaised, line, text, accent, text, panelRaised, text });

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panelRaised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, line);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, line);
    setColour (juce::Slider::textBoxTextColourId, textDim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, panelRaised);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panelRaised);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::TextEditor::backgroundColourId, panelRaised);
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ScrollBar::thumbColourId, line.brighter (0.2f));
    setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::TableHeaderComponent::backgroundColourId, panelRaised);
    setColour (juce::TableHeaderComponent::textColourId, textDim);
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, accent.withAlpha (0.7f));
}

void StudioLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (down) c = c.darker (0.2f);
    else if (highlighted) c = c.brighter (0.08f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 6.0f);
    if (b.hasKeyboardFocus (true))
    {
        g.setColour (colours::accent);
        g.drawRoundedRectangle (r, 6.0f, 1.5f);
    }
}

void StudioLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float thickness = juce::jmax (3.0f, radius * 0.16f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness, 0, start, end, true);
    g.setColour (colours::line);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness, 0, start, angle, true);
    g.setColour (s.isEnabled() ? s.findColour (juce::Slider::rotarySliderFillColourId) : colours::textDim);
    g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Point<float> tip (centre.x + (radius - thickness * 2.2f) * std::sin (angle), centre.y - (radius - thickness * 2.2f) * std::cos (angle));
    g.setColour (colours::text);
    g.drawLine (juce::Line<float> (centre, tip), 2.0f);
}

void StudioLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                          juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }
    const bool vertical = style == juce::Slider::LinearVertical;
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float trackW = 4.0f;
    auto track = vertical ? r.withSizeKeepingCentre (trackW, r.getHeight()) : r.withSizeKeepingCentre (r.getWidth(), trackW);
    g.setColour (colours::line);
    g.fillRoundedRectangle (track, 2.0f);

    auto filled = vertical ? track.withTop (pos) : track.withRight (pos);
    g.setColour (s.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (filled, 2.0f);

    auto thumb = vertical ? juce::Rectangle<float> (r.getCentreX() - 12.0f, pos - 6.0f, 24.0f, 12.0f)
                          : juce::Rectangle<float> (pos - 6.0f, r.getCentreY() - 10.0f, 12.0f, 20.0f);
    g.setColour (colours::text);
    g.fillRoundedRectangle (thumb, 3.0f);
}

void StudioLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::accent : colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (colours::textDim);
    g.fillPath (arrow);
}

juce::Font StudioLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions (juce::jmin (16.0f, (float) buttonHeight * 0.48f));
}

juce::Font StudioLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void StudioLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll (colours::panelRaised);
    g.setColour (colours::line);
    g.drawRect (0, 0, width, height);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText (text, juce::Rectangle<int> (8, 4, width - 16, height - 8), juce::Justification::centredLeft, 6);
}

void styleHeading (juce::Label& l, float size)
{
    l.setFont (juce::FontOptions (size, juce::Font::bold));
    l.setColour (juce::Label::textColourId, colours::text);
}

void styleCaption (juce::Label& l)
{
    l.setFont (juce::FontOptions (13.0f));
    l.setColour (juce::Label::textColourId, colours::textDim);
}

void paintPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, radius);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

juce::String formatTime (double seconds)
{
    seconds = juce::jmax (0.0, seconds);
    return juce::String::formatted ("%d:%04.1f", (int) seconds / 60, std::fmod (seconds, 60.0));
}

juce::String formatDb (float db)
{
    if (db <= -100.0f) return "-inf";
    return juce::String (db, 1) + " dB";
}

} // namespace studio::ui
