#pragma once

#include "../Screen.h"

namespace studio::ui
{

class TimelineView;

/** The main workspace: sections, tracks, clips, automation and the arrangement assistant. */
class ArrangeScreen : public Screen
{
public:
    explicit ArrangeScreen (Session&);
    ~ArrangeScreen() override;
    juce::String title() const override { return "Arrange"; }
    juce::String subtitle() const override { return "Lay out sections, edit clips and automation. S splits, Ctrl+D duplicates, Delete removes."; }
    void refresh (bool structural) override;
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void addTrackMenu();
    void addSectionMenu();
    void importAudio();
    void tempoKeyDialog();
    void updateAssistant();

    std::unique_ptr<TimelineView> timeline;
    juce::TextButton addTrack { "+ Track" }, importButton { "Import audio" }, addSection { "+ Section" }, split { "Split" },
        duplicate { "Duplicate" }, del { "Delete" }, zoomIn { "+" }, zoomOut { "-" }, snapButton { "Snap" }, tempoKey { "Tempo & key" },
        snapshot { "Snapshot" }, assistantToggle { "Assistant" }, automationToggle { "Automation" };
    juce::ComboBox grid;
    juce::Component assistantPanel;
    IssueList issues;
    juce::Label assistantTitle;
    juce::Viewport issuesView;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace studio::ui
