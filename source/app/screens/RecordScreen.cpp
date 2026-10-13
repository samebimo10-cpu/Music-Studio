#include "RecordScreen.h"
#include "core/analysis/AudioAnalysis.h"
#include "core/arrange/Arrangement.h"
#include "core/lyrics/LyricsTools.h"
#include "core/vocal/VocalTools.h"
#include "core/dsp/SpectralEffects.h"
#include "core/vocal/VocalChain.h"

namespace studio::ui
{

/** Take lanes: one row per take; drag across a take to use that phrase in the comp. */
class RecordScreen::TakeLanes : public juce::Component
{
public:
    explicit TakeLanes (Session& s) : session (s) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panel);
        auto* t = track();
        if (t == nullptr || t->takes.empty())
        {
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (14.0f));
            g.drawText ("Takes appear here after you record. Drag across a take to pick that phrase for the comp; "
                        "the app joins phrases with crossfades.", getLocalBounds().reduced (12), juce::Justification::centred, true);
            return;
        }
        computeRange (*t);
        const int rowH = rowHeight (*t);
        // Comp row
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Comp", 4, 0, 60, rowH, juce::Justification::centredLeft);
        for (auto& seg : t->comp)
        {
            const int idx = takeIndex (*t, seg.takeId);
            g.setColour (takeColour (idx));
            g.fillRect (juce::Rectangle<int> (beatToX (seg.startBeat), 4, beatToX (seg.endBeat) - beatToX (seg.startBeat), rowH - 8));
        }
        for (int i = 0; i < (int) t->takes.size(); ++i)
        {
            auto& tk = t->takes[(size_t) i];
            const int y = (i + 1) * rowH;
            g.setColour (colours::line);
            g.drawHorizontalLine (y, 0, (float) getWidth());
            g.setColour (colours::textDim);
            juce::String label = "Take " + juce::String (i + 1);
            for (int s = 0; s < tk.rating; ++s) label << "*";
            g.drawText (label, 4, y, 60, rowH, juce::Justification::centredLeft);
            auto r = juce::Rectangle<int> (beatToX (tk.startBeat), y + 4, beatToX (tk.startBeat + tk.lengthBeats) - beatToX (tk.startBeat), rowH - 8);
            g.setColour (takeColour (i).withAlpha (0.45f));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            // Highlight where this take is used in the comp.
            for (auto& seg : t->comp)
                if (seg.takeId == tk.id)
                {
                    g.setColour (takeColour (i));
                    g.fillRect (juce::Rectangle<int> (beatToX (seg.startBeat), y + 4, beatToX (seg.endBeat) - beatToX (seg.startBeat), rowH - 8));
                }
        }
        if (dragging)
        {
            g.setColour (colours::text.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<int>::leftTopRightBottom (std::min (dragX0, dragX1), dragRow * rowH, std::max (dragX0, dragX1), (dragRow + 1) * rowH));
        }
        const int px = beatToX (session.engine().getPositionBeats());
        g.setColour (colours::text);
        g.drawVerticalLine (px, 0, (float) getHeight());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* t = track();
        if (t == nullptr || t->takes.empty()) return;
        dragRow = e.y / rowHeight (*t);
        if (dragRow < 1 || dragRow > (int) t->takes.size()) { session.engine().setPositionBeats (xToBeat (e.x)); return; }
        if (e.mods.isPopupMenu())
        {
            const auto takeId = t->takes[(size_t) dragRow - 1].id, trackId = t->id;
            juce::PopupMenu m;
            for (int s = 0; s <= 5; ++s) m.addItem (10 + s, s == 0 ? juce::String ("No rating") : juce::String (s) + " star" + (s > 1 ? "s" : ""));
            m.addItem (1, "Use this whole take");
            m.addItem (2, "Delete take");
            m.showMenuAsync ({}, [this, takeId, trackId] (int r)
            {
                if (r >= 10) session.perform ("Rate take", [=] (Project& p) { for (auto& tk : p.findTrack (trackId)->takes) if (tk.id == takeId) tk.rating = r - 10; }, false);
                if (r == 1) session.perform ("Comp: whole take", [=] (Project& p)
                {
                    auto* tr = p.findTrack (trackId);
                    for (auto& tk : tr->takes) if (tk.id == takeId) vocal::setCompSegment (*tr, takeId, tk.startBeat, tk.startBeat + tk.lengthBeats);
                    vocal::buildCompClips (p, *tr);
                });
                if (r == 2) session.perform ("Delete take", [=] (Project& p)
                {
                    auto* tr = p.findTrack (trackId);
                    tr->takes.erase (std::remove_if (tr->takes.begin(), tr->takes.end(), [&] (auto& tk) { return tk.id == takeId; }), tr->takes.end());
                    tr->comp.erase (std::remove_if (tr->comp.begin(), tr->comp.end(), [&] (auto& c) { return c.takeId == takeId; }), tr->comp.end());
                    vocal::buildCompClips (p, *tr);
                });
            });
            return;
        }
        dragging = true;
        dragX0 = dragX1 = e.x;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) { dragX1 = e.x; repaint(); } }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        auto* t = track();
        if (t == nullptr) return;
        const double bpb = 0.25;
        double a = std::round (xToBeat (std::min (dragX0, dragX1)) / bpb) * bpb;
        double b = std::round (xToBeat (std::max (dragX0, dragX1)) / bpb) * bpb;
        if (b - a < 0.25) { repaint(); return; }
        const auto takeId = t->takes[(size_t) dragRow - 1].id, trackId = t->id;
        session.perform ("Comp phrase", [=] (Project& p)
        {
            auto* tr = p.findTrack (trackId);
            vocal::setCompSegment (*tr, takeId, a, b);
            vocal::buildCompClips (p, *tr);
        });
    }

private:
    const Track* track() const
    {
        auto* t = session.project().findTrack (session.selectedTrackId);
        return t != nullptr && t->type == TrackType::Audio ? t : nullptr;
    }
    int rowHeight (const Track& t) const { return juce::jlimit (24, 60, getHeight() / ((int) t.takes.size() + 1)); }
    static int takeIndex (const Track& t, const juce::String& id)
    {
        for (int i = 0; i < (int) t.takes.size(); ++i) if (t.takes[(size_t) i].id == id) return i;
        return 0;
    }
    static juce::Colour takeColour (int i)
    {
        static const juce::uint32 c[] { 0xff4f8cff, 0xffffb547, 0xffb07cff, 0xff45c8ff, 0xffe86bd1, 0xff9bd45c };
        return juce::Colour (c[i % 6]);
    }
    void computeRange (const Track& t)
    {
        lo = 1.0e9; hi = 0;
        for (auto& tk : t.takes) { lo = std::min (lo, tk.startBeat); hi = std::max (hi, tk.startBeat + tk.lengthBeats); }
        if (hi <= lo) hi = lo + 4;
    }
    int beatToX (double b) const { return 70 + (int) ((b - lo) / (hi - lo) * (getWidth() - 80)); }
    double xToBeat (int x) const { return lo + (x - 70) / (double) (getWidth() - 80) * (hi - lo); }

    Session& session;
    double lo = 0, hi = 4;
    bool dragging = false;
    int dragRow = 0, dragX0 = 0, dragX1 = 0;
};

//==============================================================================
RecordScreen::RecordScreen (Session& s) : Screen (s)
{
    takes = std::make_unique<TakeLanes> (s);
    addAndMakeVisible (*takes);


    levelAdvice.setFont (juce::FontOptions (16.0f));
    levelAdvice.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (levelAdvice);
    styleCaption (armedInfo);
    addAndMakeVisible (armedInfo);

    inputMeter.setShowScale (true);
    inputMeter.source = [this]() -> std::array<float, 3>
    {
        auto* t = session.project().findTrack (session.selectedTrackId);
        const int ch = t != nullptr ? t->inputChannel : 0;
        const float a = session.engine().getInputPeak (ch);
        const float b = t != nullptr && t->inputChannels > 1 ? session.engine().getInputPeak (ch + 1) : a;
        peakDb = juce::jmax (peakDb - 0.5f, juce::Decibels::gainToDecibels (juce::jmax (a, b), -100.0f));
        return { a, b, 0.0f };
    };
    addAndMakeVisible (inputMeter);

    recordButton.setColour (juce::TextButton::buttonColourId, colours::record);
    recordButton.setTooltip ("Start/stop recording on armed tracks (R)");
    recordButton.onClick = [this] { session.toggleRecord(); };
    addAndMakeVisible (recordButton);

    wizard.setTooltip ("Detects your interface or mic and helps you set the gain");
    wizard.onClick = [this] { runInputWizard(); };
    roomCheck.setTooltip ("Records 10 seconds of silence, measures background noise and suggests fixes");
    roomCheck.onClick = [this] { runRoomCheck(); };
    addAndMakeVisible (wizard);
    addAndMakeVisible (roomCheck);

    auto label = [this] (juce::Label& l, const juce::String& t) { l.setText (t, juce::dontSendNotification); styleCaption (l); addAndMakeVisible (l); };
    label (countInLabel, "Count-in");
    label (preRollLabel, "Pre-roll");
    label (monitorLabel, "Monitor reverb (headphones only)");
    label (trackLabel, "Track");
    label (inputLabel, "Input");

    countIn.addItemList ({ "Off", "1 bar", "2 bars" }, 1);
    countIn.setSelectedId (2);
    countIn.onChange = [this] { session.recordSettings.countInBars = countIn.getSelectedId() - 1; };
    preRoll.addItemList ({ "Off", "1 bar", "2 bars", "4 bars" }, 1);
    preRoll.setSelectedId (2);
    preRoll.onChange = [this] { static const int v[] { 0, 1, 2, 4 }; session.recordSettings.preRollBars = v[preRoll.getSelectedId() - 1]; };
    session.recordSettings.countInBars = 1;
    addAndMakeVisible (countIn);
    addAndMakeVisible (preRoll);

    makeToggle (punch, "Punch in/out", false);
    punch.setTooltip ("Re-record just one line: set the range by dragging in the Arrange ruler (it sets the loop), then record");
    punch.onClick = [this] { session.recordSettings.punch = punch.getToggleState(); };
    makeToggle (loopRecord, "Loop record", false);
    loopRecord.setTooltip ("Records every pass of the loop as a new take");
    loopRecord.onClick = [this] { session.recordSettings.loopRecord = loopRecord.getToggleState(); };
    makeToggle (metronome, "Click", false);
    metronome.onClick = [this] { session.engine().setMetronome (metronome.getToggleState()); };
    for (auto* b : { &punch, &loopRecord, &metronome }) addAndMakeVisible (b);

    monitorReverb.setSliderStyle (juce::Slider::LinearHorizontal);
    monitorReverb.setRange (0, 1);
    monitorReverb.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    monitorReverb.onValueChange = [this] { session.engine().setMonitorReverb ((float) monitorReverb.getValue()); };
    addAndMakeVisible (monitorReverb);

    trackChoice.onChange = [this]
    {
        const int i = trackChoice.getSelectedId() - 1;
        auto& tr = session.project().tracks;
        if (i >= 0 && i < (int) tr.size()) session.select (tr[(size_t) i].id);
    };
    addAndMakeVisible (trackChoice);
    inputChoice.onChange = [this]
    {
        const auto id = session.selectedTrackId;
        const int sel = inputChoice.getSelectedId() - 1;
        session.perform ("Track input", [=] (Project& p)
        {
            if (auto* t = p.findTrack (id)) { t->inputChannel = sel / 2; t->inputChannels = sel % 2 == 0 ? 1 : 2; }
        });
    };
    addAndMakeVisible (inputChoice);

    lyricsTitle.setText ("Lyrics", juce::dontSendNotification);
    styleHeading (lyricsTitle, 15);
    addAndMakeVisible (lyricsTitle);
    lyrics.setMultiLine (true);
    lyrics.setReadOnly (true);
    lyrics.setFont (juce::FontOptions (20.0f));
    addAndMakeVisible (lyrics);

    startTimerHz (10);
}

void RecordScreen::refresh (bool)
{
    const auto& p = session.project();
    // Default to an armed (or any) audio track.
    if (auto* sel = p.findTrack (session.selectedTrackId); sel == nullptr || sel->type != TrackType::Audio)
    {
        for (auto& t : p.tracks) if (t.type == TrackType::Audio && t.armed) { session.selectedTrackId = t.id; break; }
        if (auto* s2 = p.findTrack (session.selectedTrackId); s2 == nullptr || s2->type != TrackType::Audio)
            for (auto& t : p.tracks) if (t.type == TrackType::Audio) { session.selectedTrackId = t.id; break; }
    }
    trackChoice.clear (juce::dontSendNotification);
    int armed = 0;
    for (int i = 0; i < (int) p.tracks.size(); ++i)
    {
        auto& t = p.tracks[(size_t) i];
        if (t.type == TrackType::Audio) trackChoice.addItem (t.name + (t.armed ? "  (armed)" : ""), i + 1);
        if (t.armed) ++armed;
        if (t.id == session.selectedTrackId) trackChoice.setSelectedId (i + 1, juce::dontSendNotification);
    }
    armedInfo.setText (armed == 0 ? "No track armed. Arm one with R on its header in Arrange, or pick a track here and press Arm."
                                  : juce::String (armed) + " track(s) armed.", juce::dontSendNotification);

    inputChoice.clear (juce::dontSendNotification);
    juce::StringArray names;
    if (auto* dev = session.engine().getDeviceManager().getCurrentAudioDevice()) names = dev->getInputChannelNames();
    if (names.isEmpty()) names.add ("Input 1");
    for (int i = 0; i < names.size(); ++i)
    {
        inputChoice.addItem (names[i] + " (mono)", i * 2 + 1);
        if (i + 1 < names.size()) inputChoice.addItem (names[i] + " + " + names[i + 1] + " (stereo)", i * 2 + 2);
    }
    if (auto* t = p.findTrack (session.selectedTrackId))
        inputChoice.setSelectedId (t->inputChannel * 2 + (t->inputChannels > 1 ? 2 : 1), juce::dontSendNotification);

    // Lyrics on screen: the lyric sheet, or the release lyrics.
    auto text = ! p.lyricSheet.empty() ? lyrics::sectionsToText (p.lyricSheet) : p.release.lyrics;
    lyrics.setText (text.isNotEmpty() ? text : juce::String ("Write lyrics in the Lyrics pad and they will show here while you record."), false);
    takes->repaint();
}

void RecordScreen::timerCallback()
{
    recordButton.setButtonText (session.engine().isCountingIn() ? "Count-in..." : session.engine().isRecording() ? "Stop recording" : "Record");
    if (--adviceHold <= 0)
    {
        auto a = analysis::adviseInputLevel (peakDb, rmsDb);
        levelAdvice.setText (peakDb < -90 ? juce::String ("No input signal. Check the cable, the interface gain and the input choice.") : a.message,
                             juce::dontSendNotification);
        levelAdvice.setColour (juce::Label::textColourId, a.clipping ? colours::error : peakDb < -90 || a.tooQuiet ? colours::warning : colours::good);
        adviceHold = 5;
    }
    if (session.engine().isPlaying()) takes->repaint();
}

void RecordScreen::runInputWizard()
{
    auto& dm = session.engine().getDeviceManager();
    auto* dev = dm.getCurrentAudioDevice();
    juce::String msg;
    if (dev == nullptr)
        msg = "No audio device is open. Open Settings and choose your interface or USB mic.";
    else
    {
        msg << "Device: " << dev->getName() << " (" << dev->getTypeName() << ")\n"
            << "Inputs: " << dev->getInputChannelNames().joinIntoString (", ") << "\n"
            << "Buffer: " << dev->getCurrentBufferSizeSamples() << " samples at " << (int) dev->getCurrentSampleRate() << " Hz - round trip about "
            << juce::String (session.engine().getRoundTripLatencyMs(), 1) << " ms\n\n"
            << "1. Sing the loudest line of the song now and watch the meter.\n"
            << "2. Turn the gain knob on your interface until the loudest line peaks around -10 dB (yellow), never orange.\n"
            << "3. Stay a hand's width from the mic. Use a pop filter for 'p' and 'b' sounds.\n";
        if (session.engine().getRoundTripLatencyMs() > 12)
            msg << "\nLatency is above 10 ms. Lower the buffer size to 128 in Settings (ASIO on Windows) for comfortable monitoring.";
    }
    tell ("Input setup", msg);
    // Make sure the selected track is armed and monitoring so the meter moves.
    const auto id = session.selectedTrackId;
    if (auto* t = session.project().findTrack (id); t != nullptr && t->type == TrackType::Audio && ! t->armed)
        session.perform ("Arm track", [id] (Project& p) { if (auto* tt = p.findTrack (id)) { tt->armed = true; tt->monitorInput = true; } });
}

void RecordScreen::runRoomCheck()
{
    auto* t = session.project().findTrack (session.selectedTrackId);
    const int ch = t != nullptr ? t->inputChannel : 0;
    roomCheck.setEnabled (false);
    roomCheck.setButtonText ("Stay quiet... 10 s");
    session.engine().captureInput (10.0, ch, 1, [this] (juce::AudioBuffer<float> audio)
    {
        roomCheck.setEnabled (true);
        roomCheck.setButtonText ("Room check (10 s)");
        auto r = analysis::analyseRoom (audio, session.engine().getSampleRate());
        juce::String msg;
        msg << "Rating: " << r.rating << "\n"
            << juce::String::formatted ("Noise floor: %.1f dBFS", (double) r.noiseFloorDb) << "\n"
            << (r.humFrequency > 0 ? "Hum at " + juce::String (r.humFrequency) + " Hz\n" : juce::String()) << "\n";
        for (auto& sug : r.suggestions) msg << "- " << sug << "\n";
        msg << "\nThe noise profile was handed to the vocal chain's noise removal on the selected track.";
        tell ("Room check", msg);
        // Give the measured profile to the selected track's live denoiser.
        if (auto* g = session.engine().getGraph())
            if (auto* vc = g->findVocalChain (session.selectedTrackId))
                if (auto* nr = dynamic_cast<dsp::NoiseReductionEffect*> (vc->effectForStep ("denoise")))
                {
                    nr->setNoiseProfile (r.noiseProfile);
                    if (r.humFrequency > 0) nr->setParam ("hum", r.humFrequency == 50 ? 1.0f : 2.0f);
                }
    });
}

void RecordScreen::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void RecordScreen::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto left = r.removeFromLeft (150);
    inputMeter.setBounds (left.removeFromTop (juce::jmin (420, left.getHeight() - 120)).reduced (20, 0));
    left.removeFromTop (10);
    recordButton.setBounds (left.removeFromTop (60));
    r.removeFromLeft (16);

    auto top = r.removeFromTop (96);
    levelAdvice.setBounds (top.removeFromTop (26));
    armedInfo.setBounds (top.removeFromTop (20));
    auto row = top.removeFromTop (40);
    trackLabel.setBounds (row.removeFromLeft (40));
    trackChoice.setBounds (row.removeFromLeft (220).reduced (2, 4));
    row.removeFromLeft (10);
    inputLabel.setBounds (row.removeFromLeft (40));
    inputChoice.setBounds (row.removeFromLeft (220).reduced (2, 4));
    row.removeFromLeft (10);
    wizard.setBounds (row.removeFromLeft (120).reduced (2, 4));
    roomCheck.setBounds (row.removeFromLeft (150).reduced (2, 4));

    auto controls = r.removeFromTop (56);
    auto col = [&] (juce::Label& l, juce::Component& c, int w)
    {
        auto a = controls.removeFromLeft (w);
        l.setBounds (a.removeFromTop (18));
        c.setBounds (a.reduced (2, 2));
        controls.removeFromLeft (8);
    };
    col (countInLabel, countIn, 110);
    col (preRollLabel, preRoll, 110);
    auto toggles = controls.removeFromLeft (330).withTrimmedTop (18);
    punch.setBounds (toggles.removeFromLeft (120).reduced (2));
    loopRecord.setBounds (toggles.removeFromLeft (120).reduced (2));
    metronome.setBounds (toggles.removeFromLeft (80).reduced (2));
    col (monitorLabel, monitorReverb, 240);

    r.removeFromTop (10);
    auto lyricArea = r.removeFromRight (juce::jmin (380, r.getWidth() / 3));
    lyricsTitle.setBounds (lyricArea.removeFromTop (22));
    lyrics.setBounds (lyricArea);
    r.removeFromRight (12);
    takes->setBounds (r);
}

} // namespace studio::ui
