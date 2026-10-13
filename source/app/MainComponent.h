#pragma once

#include "Session.h"
#include "Screen.h"

namespace studio::ui
{

/** Sidebar navigation, transport bar, guided-mode steps and the active screen. */
class MainComponent : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit MainComponent (Session&);
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void show (const juce::String& screenId);
    juce::String currentScreen() const { return current; }
    juce::StringArray screenIds() const;
    Screen* screen (const juce::String& id);

    /** Saves before quitting; returns false to cancel. */
    void askToSaveThen (std::function<void()> then);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateTransport();

    Session& session;
    struct Entry { juce::String id, label, shortcut; std::unique_ptr<Screen> screen; std::unique_ptr<juce::TextButton> button; };
    std::vector<Entry> entries;
    juce::String current;

    // Transport
    juce::TextButton play { "Play" }, stop { "Stop" }, record { "Rec" }, loop { "Loop" }, click { "Click" }, home { "|<" };
    juce::TextButton undo { "Undo" }, redo { "Redo" }, save { "Save" }, guidedToggle { "Guided" };
    juce::Label position, songInfo, cpu, status, titleLabel, subtitleLabel;
    LevelMeter masterMeter { false };

    // Guided mode
    juce::OwnedArray<juce::TextButton> steps;
    static const std::vector<std::pair<juce::String, juce::String>>& guidedSteps();
    bool guided = true;
    int statusHold = 0;
};

} // namespace studio::ui
