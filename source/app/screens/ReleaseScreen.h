#pragma once

#include "../Screen.h"

namespace studio::ui
{

class ReleaseScreen : public Screen
{
public:
    explicit ReleaseScreen (Session&);
    ~ReleaseScreen() override;
    juce::String title() const override { return "Release"; }
    juce::String subtitle() const override { return "Fill in the details, add cover art, sync lyrics, run the checklist, then export."; }
    void refresh (bool structural) override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::OwnedArray<juce::Component> pages;
};

} // namespace studio::ui
