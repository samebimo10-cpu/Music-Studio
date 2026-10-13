#pragma once

#include "../Screen.h"

namespace studio::ui
{

class HomeScreen : public Screen, private juce::ListBoxModel
{
public:
    explicit HomeScreen (Session&);
    juce::String title() const override { return "Home"; }
    juce::String subtitle() const override { return "Start a new song, open one, or pick up an idea from your phone."; }
    void refresh (bool) override;
    void resized() override;
    void paint (juce::Graphics&) override;

    /** Opens a project, offering crash recovery if needed. */
    void openWithRecovery (const juce::File& folder);

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    juce::File ideaInbox() const;
    void startFromIdea (const juce::File& audio);

    juce::Label heading, sub, recentLabel, templatesLabel, ideasLabel, learnLabel;
    juce::TextButton newSong { "New song" }, openButton { "Open project..." };
    juce::OwnedArray<juce::TextButton> templateButtons;
    juce::ListBox recentList { "Recent", this };
    juce::StringArray recent;

    juce::ListBox ideaList;
    juce::Array<juce::File> ideas;
    juce::TextButton useIdea { "Start a song from this idea" }, openInbox { "Show inbox folder" };
    struct IdeaModel;
    std::unique_ptr<IdeaModel> ideaModel;

    juce::TextEditor lessons;
    std::unique_ptr<juce::FileChooser> chooser;
};

class NewSongScreen : public Screen
{
public:
    explicit NewSongScreen (Session&);
    juce::String title() const override { return "New song"; }
    juce::String subtitle() const override { return "Pick a style, tempo, key and mood. You'll get a full starting beat as editable parts."; }
    void refresh (bool) override {}
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void styleChanged();
    void create (bool withBeat);

    juce::Label heading, sub, nameLabel, styleLabel, tempoLabel, keyLabel, moodLabel, lengthLabel, styleInfo;
    juce::TextEditor name;
    juce::ComboBox style, key;
    juce::Slider tempo, length;
    juce::TextButton joyful { "Joyful" }, reflective { "Reflective" }, intense { "Intense" };
    juce::TextButton generate { "Generate starting beat" }, blank { "Start empty" };
    juce::String mood = "joyful";
};

} // namespace studio::ui
