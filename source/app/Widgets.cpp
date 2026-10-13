#include "Widgets.h"
#include "core/engine/AudioEngine.h"

namespace studio::ui
{

//==============================================================================
LevelMeter::LevelMeter (bool v) : vertical (v)
{
    startTimerHz (30);
    setTooltip ("Level meter. Blue is safe, yellow is loud, orange means you are close to clipping.");
}

void LevelMeter::timerCallback()
{
    if (! source) return;
    auto v = source();
    for (int ch = 0; ch < 2; ++ch)
    {
        const float db = juce::Decibels::gainToDecibels (v[(size_t) ch], -70.0f);
        levels[ch] = db > levels[ch] ? db : juce::jmax (db, levels[ch] - 1.5f); // fall 45 dB/s
        if (db >= holds[ch]) { holds[ch] = db; holdTimers[ch] = 45; }
        else if (--holdTimers[ch] <= 0) holds[ch] = juce::jmax (-70.0f, holds[ch] - 1.0f);
    }
    gr = v[2];
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (showScale)
    {
        auto scale = vertical ? r.removeFromRight (22.0f) : r.removeFromBottom (12.0f);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (9.0f));
        for (int db : { 0, -6, -12, -24, -48 })
        {
            const float p = juce::jmap ((float) db, -70.0f, 6.0f, 0.0f, 1.0f);
            if (vertical) g.drawText (juce::String (db), scale.withY (r.getBottom() - p * r.getHeight() - 5).withHeight (10), juce::Justification::centredRight);
            else g.drawText (juce::String (db), scale.withX (r.getX() + p * r.getWidth() - 10).withWidth (20), juce::Justification::centred);
        }
    }

    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 3.0f);
    const float gap = 2.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = vertical ? r.withWidth ((r.getWidth() - gap) / 2).withX (r.getX() + ch * (r.getWidth() + gap) / 2)
                            : r.withHeight ((r.getHeight() - gap) / 2).withY (r.getY() + ch * (r.getHeight() + gap) / 2);
        const float p = juce::jlimit (0.0f, 1.0f, juce::jmap (levels[ch], -70.0f, 6.0f, 0.0f, 1.0f));
        auto fill = vertical ? bar.withTop (bar.getBottom() - p * bar.getHeight()) : bar.withWidth (p * bar.getWidth());
        juce::ColourGradient grad (colours::meterLow, vertical ? bar.getX() : bar.getX(), vertical ? bar.getBottom() : bar.getY(),
                                   colours::meterHot, vertical ? bar.getX() : bar.getRight(), vertical ? bar.getY() : bar.getY(), false);
        grad.addColour (juce::jmap (-12.0f, -70.0f, 6.0f, 0.0f, 1.0f), colours::meterLow);
        grad.addColour (juce::jmap (-4.0f, -70.0f, 6.0f, 0.0f, 1.0f), colours::meterMid);
        g.setGradientFill (grad);
        g.fillRect (fill);

        const float hp = juce::jlimit (0.0f, 1.0f, juce::jmap (holds[ch], -70.0f, 6.0f, 0.0f, 1.0f));
        g.setColour (holds[ch] > -0.5f ? colours::meterHot : colours::text);
        if (vertical) g.fillRect (bar.getX(), bar.getBottom() - hp * bar.getHeight(), bar.getWidth(), 2.0f);
        else g.fillRect (bar.getX() + hp * bar.getWidth(), bar.getY(), 2.0f, bar.getHeight());
    }

    if (gr < -0.3f && vertical)
    {
        const float p = juce::jlimit (0.0f, 1.0f, -gr / 24.0f);
        g.setColour (colours::accentWarm.withAlpha (0.8f));
        g.fillRect (r.getRight() - 3.0f, r.getY(), 3.0f, p * r.getHeight());
    }
}

std::function<std::array<float, 3>()> trackMeterSource (AudioEngine& engine, const juce::String& trackId)
{
    return [&engine, trackId]() -> std::array<float, 3>
    {
        if (auto* g = engine.getGraph())
            if (auto* m = g->trackMeter (trackId))
            {
                auto* mm = const_cast<MeterValues*> (m);
                return { mm->peakL.exchange (0.0f), mm->peakR.exchange (0.0f), mm->gainReductionDb.load() };
            }
        return { 0, 0, 0 };
    };
}

std::function<std::array<float, 3>()> masterMeterSource (AudioEngine& engine)
{
    return [&engine]() -> std::array<float, 3>
    {
        if (auto* g = engine.getGraph())
        {
            auto& m = const_cast<MeterValues&> (g->masterMeter());
            return { m.peakL.exchange (0.0f), m.peakR.exchange (0.0f), m.gainReductionDb.load() };
        }
        return { 0, 0, 0 };
    };
}

//==============================================================================
Knob::Knob (const juce::String& text, double min, double max, double value, const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    slider.setRange (min, max);
    slider.setValue (value, juce::dontSendNotification);
    slider.setTextValueSuffix (suffix);
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    styleCaption (label);
    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

//==============================================================================
ParamPanel::ParamPanel (const std::vector<dsp::ParamInfo>& params, std::function<float (const juce::String&)> getter,
                        std::function<void (const juce::String&, float, bool)> setter, int cols)
    : get (std::move (getter)), set (std::move (setter)), columns (cols)
{
    for (auto& p : params)
    {
        auto row = std::make_unique<Row>();
        row->info = p;
        row->label = std::make_unique<juce::Label> ("", p.name);
        row->label->setJustificationType (juce::Justification::centred);
        styleCaption (*row->label);
        addAndMakeVisible (*row->label);

        const auto id = p.id;
        if (! p.choices.isEmpty())
        {
            row->combo = std::make_unique<juce::ComboBox>();
            for (int i = 0; i < p.choices.size(); ++i) row->combo->addItem (p.choices[i], i + 1);
            row->combo->onChange = [this, id, c = row->combo.get()] { set (id, (float) (c->getSelectedId() - 1), true); };
            row->combo->setTooltip (p.help);
            addAndMakeVisible (*row->combo);
        }
        else
        {
            row->slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            auto& s = *row->slider;
            s.setRange (p.minValue, p.maxValue);
            if (p.logarithmic && p.minValue > 0) s.setSkewFactorFromMidPoint (std::sqrt (p.minValue * p.maxValue));
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
            s.setNumDecimalPlacesToDisplay (p.maxValue - p.minValue > 50 ? 0 : 1);
            s.setTextValueSuffix (p.unit.isNotEmpty() && p.unit != "%" ? " " + p.unit : juce::String());
            s.setDoubleClickReturnValue (true, p.defaultValue);
            s.setTooltip (p.help.isNotEmpty() ? p.help : p.name);
            s.onValueChange = [this, id, &s] { set (id, (float) s.getValue(), false); };
            s.onDragEnd = [this, id, &s] { set (id, (float) s.getValue(), true); };
            addAndMakeVisible (s);
        }
        rows.push_back (std::move (row));
    }
    refreshValues();
}

void ParamPanel::refreshValues()
{
    for (auto& r : rows)
    {
        const float v = get (r->info.id);
        if (r->slider) r->slider->setValue (v, juce::dontSendNotification);
        if (r->combo) r->combo->setSelectedId ((int) std::round (v) + 1, juce::dontSendNotification);
    }
}

int ParamPanel::preferredHeight (int width) const
{
    const int cols = juce::jmax (1, juce::jmin (columns, width / 90));
    const int rowsNeeded = ((int) rows.size() + cols - 1) / cols;
    return rowsNeeded * 96;
}

void ParamPanel::resized()
{
    const int cols = juce::jmax (1, juce::jmin (columns, getWidth() / 90));
    const int w = getWidth() / cols;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto cell = juce::Rectangle<int> ((int) (i % (size_t) cols) * w, (int) (i / (size_t) cols) * 96, w, 96).reduced (4);
        rows[i]->label->setBounds (cell.removeFromTop (16));
        if (rows[i]->slider) rows[i]->slider->setBounds (cell);
        if (rows[i]->combo) rows[i]->combo->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 28));
    }
}

//==============================================================================
void Card::paint (juce::Graphics& g)
{
    paintPanel (g, getLocalBounds().toFloat());
    if (title.isNotEmpty())
    {
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (title, getLocalBounds().reduced (12, 8).removeFromTop (20), juce::Justification::centredLeft);
    }
}

//==============================================================================
void IssueList::setItems (std::vector<Item> newItems)
{
    items = std::move (newItems);
    buttons.clear();
    for (auto& it : items)
    {
        auto* b = buttons.add (new juce::TextButton (it.actionLabel.isNotEmpty() ? it.actionLabel : juce::String ("Fix")));
        b->setVisible (it.action != nullptr);
        auto action = it.action;
        b->onClick = [action] { if (action) action(); };
        addChildComponent (b);
    }
    resized();
    repaint();
}

int IssueList::preferredHeight() const { return (int) items.size() * 58 + 4; }

void IssueList::resized()
{
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds (getWidth() - 96, i * 58 + 14, 88, 30);
}

void IssueList::paint (juce::Graphics& g)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        auto r = juce::Rectangle<int> (0, (int) i * 58, getWidth(), 54);
        g.setColour (colours::panelRaised);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        const auto c = items[i].level == 0 ? colours::good : items[i].level == 1 ? colours::warning : colours::error;
        g.setColour (c);
        g.fillRoundedRectangle (r.removeFromLeft (5).toFloat(), 2.0f);
        // Shape as well as colour, for colour-blind users: tick, triangle, cross.
        auto icon = r.removeFromLeft (28).toFloat().withSizeKeepingCentre (14, 14);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (items[i].level == 0 ? juce::String::fromUTF8 ("\xe2\x9c\x93") : items[i].level == 1 ? "!" : juce::String::fromUTF8 ("\xc3\x97"),
                    icon, juce::Justification::centred);
        auto textArea = r.withTrimmedRight (items[i].action ? 104 : 8);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (items[i].title, textArea.removeFromTop (26).withTrimmedTop (6), juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (items[i].detail, textArea.withTrimmedBottom (4), juce::Justification::topLeft, 2);
    }
}

void makeToggle (juce::TextButton& b, const juce::String& text, bool initial)
{
    b.setButtonText (text);
    b.setClickingTogglesState (true);
    b.setToggleState (initial, juce::dontSendNotification);
}

} // namespace studio::ui
