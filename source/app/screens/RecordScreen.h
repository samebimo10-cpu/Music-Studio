#pragma once

#include "../Screen.h"

namespace studio::ui
{

/** Record: big meters, take lanes for comping, lyrics on screen, punch and loop controls. */
class RecordScreen : public Screen, private juce::Timer
{
public:
    explicit RecordScreen (Session&);
    juce::String title() const override { return "Record"; }
    juce::String subtitle() const override { return "Arm a track, check your level, then press Record (R). Every take is kept for comping."; }
    void refresh (bool structural) override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void runInputWizard();
    void runRoomCheck();
    void updateTakeLanes();

    class TakeLanes;
    std::unique_ptr<TakeLanes> takes;

    juce::Label heading, levelAdvice, lyricsTitle, armedInfo;
    LevelMeter inputMeter { true };
    juce::TextButton recordButton { "Record" }, wizard { "Input setup" }, roomCheck { "Room check (10 s)" };
    juce::ComboBox countIn, preRoll, trackChoice, inputChoice;
    juce::TextButton punch { "Punch in/out" }, loopRecord { "Loop record" }, metronome { "Click" };
    juce::Slider monitorReverb;
    juce::Label countInLabel, preRollLabel, monitorLabel, trackLabel, inputLabel;
    juce::TextEditor lyrics;
    float peakDb = -100, rmsDb = -100;
    int adviceHold = 0;
};

} // namespace studio::ui
