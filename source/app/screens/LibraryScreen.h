#pragma once

#include "../Screen.h"

namespace studio::ui
{

class LibraryScreen : public Screen
{
public:
    explicit LibraryScreen (Session&);
    ~LibraryScreen() override;
    juce::String title() const override { return "Library"; }
    juce::String subtitle() const override { return "Instruments, loops and sound packs. Loops match the song's tempo and key when added."; }
    void refresh (bool structural) override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct InstrumentList;
    struct LoopList;
    void search();
    void preview (const juce::File& f);

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<InstrumentList> instrumentsTab;
    std::unique_ptr<LoopList> loopsTab;
    std::unique_ptr<juce::Component> packsTab;
};

} // namespace studio::ui
