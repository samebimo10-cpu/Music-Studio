#pragma once

#include "../Screen.h"

namespace studio::ui
{

class MixerScreen : public Screen
{
public:
    explicit MixerScreen (Session&);
    ~MixerScreen() override;
    juce::String title() const override { return "Mixer"; }
    juce::String subtitle() const override { return "Press Auto-mix for a starting balance, then read the Mix assistant."; }
    void refresh (bool structural) override;
    void resized() override;
    void paint (juce::Graphics&) override;

    /** Opens a parameter editor for an effect slot (also used by other screens). */
    static void editEffect (Session& session, const juce::String& trackId, const juce::String& slotId, juce::Component& anchor);

private:
    struct Strip;
    void runAutoMix();
    void runAssistant();
    void loadReference();

    juce::OwnedArray<Strip> strips;
    juce::Component stripHolder;
    juce::Viewport stripView;
    juce::TextButton autoMix { "Auto-mix" }, assistant { "Mix assistant" }, reference { "Reference track..." };
    juce::OwnedArray<juce::TextButton> listenButtons;
    IssueList issues;
    juce::Viewport issuesView;
    juce::Label assistantTitle, busyLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool worker { 1 };
    std::atomic<bool> busy { false };
};

} // namespace studio::ui
