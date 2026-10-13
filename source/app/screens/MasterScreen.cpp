#include "MasterScreen.h"
#include "core/engine/OfflineRenderer.h"
#include "core/exporting/Exporter.h"
#include "core/mastering/MasteringChain.h"

namespace studio::ui
{

MasterScreen::MasterScreen (Session& s) : Screen (s)
{
    for (auto& name : MasteringChain::presetNames())
    {
        auto* b = presets.add (new juce::TextButton (name));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (600);
        b->onClick = [this, name]
        {
            auto m = session.project().mastering;
            m.preset = name;
            m.params.clear();
            m.enabled = true;
            session.setMastering (m, true);
            rebuildParams();
        };
        addAndMakeVisible (b);
    }
    ai.setColour (juce::TextButton::buttonColourId, colours::accent);
    ai.setTooltip ("Analyses the mix and suggests EQ, compression, width and loudness, then explains what it changed");
    ai.onClick = [this] { runAiMaster(); };
    addAndMakeVisible (ai);

    ab.setClickingTogglesState (true);
    ab.setToggleState (true, juce::dontSendNotification);
    ab.setTooltip ("Switch between the master and the unmastered mix, at matched loudness so you judge tone, not volume");
    ab.onClick = [this]
    {
        auto m = session.project().mastering;
        m.enabled = ab.getToggleState();
        session.setMastering (m, true);
        if (auto* g = session.engine().getGraph())
            g->setMasterVolume (m.enabled ? 0.0f : juce::jlimit (0.0f, 18.0f, lastAfterLufs - lastBeforeLufs));
        ab.setButtonText (m.enabled ? "A/B: hearing master" : "A/B: hearing mix (level-matched)");
    };
    addAndMakeVisible (ab);
    resetMeter.onClick = [this] { if (auto* g = session.engine().getGraph()) g->resetLoudness(); };
    addAndMakeVisible (resetMeter);
    album.setTooltip ("Put several mastered songs in order, match their loudness and set the gaps");
    album.onClick = [this] { albumMastering(); };
    addAndMakeVisible (album);

    auto setup = [this] (juce::Slider& sl, juce::Label& l, const juce::String& text, double lo, double hi, const juce::String& suffix)
    {
        sl.setSliderStyle (juce::Slider::LinearHorizontal);
        sl.setRange (lo, hi, 0.5);
        sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 22);
        sl.setTextValueSuffix (suffix);
        addAndMakeVisible (sl);
        l.setText (text, juce::dontSendNotification);
        styleCaption (l);
        addAndMakeVisible (l);
    };
    setup (target, targetL, "Loudness target", -20, -6, " LUFS");
    setup (ceiling, ceilingL, "Peak ceiling", -3, 0, " dBTP");
    target.onDragEnd = [this] { auto m = session.project().mastering; m.targetLufs = (float) target.getValue(); session.setMastering (m, true); };
    ceiling.onDragEnd = [this] { auto m = session.project().mastering; m.ceilingDbtp = (float) ceiling.getValue(); session.setMastering (m, true); };

    notes.setJustificationType (juce::Justification::topLeft);
    notes.setColour (juce::Label::textColourId, colours::text);
    notes.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (notes);
    loudnessTitle.setText ("Loudness", juce::dontSendNotification);
    styleHeading (loudnessTitle, 15);
    addAndMakeVisible (loudnessTitle);

    meter.source = masterMeterSource (session.engine());
    meter.setShowScale (true);
    addAndMakeVisible (meter);
    addAndMakeVisible (paramView);
    startTimerHz (5);
}

MasterScreen::~MasterScreen() { worker.removeAllJobs (true, 20000); }

void MasterScreen::rebuildParams()
{
    MasteringChain reference;
    std::vector<dsp::ParamInfo> infos;
    for (auto& p : reference.getParams()) if (p.id != "ceiling") infos.push_back (p);
    params = std::make_unique<ParamPanel> (infos,
        [this, infos] (const juce::String& id) -> float
        {
            const auto& m = session.project().mastering;
            if (auto it = m.params.find (id); it != m.params.end()) return it->second;
            auto pv = MasteringChain::presetValues (m.preset);
            if (auto it = pv.find (id); it != pv.end()) return it->second;
            for (auto& i : infos) if (i.id == id) return i.defaultValue;
            return 0.0f;
        },
        [this] (const juce::String& id, float v, bool end)
        {
            auto m = session.project().mastering;
            m.params[id] = v;
            session.setMastering (m, end);
        }, 8);
    paramView.setViewedComponent (params.get(), false);
    resized();
}

void MasterScreen::refresh (bool structural)
{
    const auto& m = session.project().mastering;
    for (auto* b : presets) b->setToggleState (b->getButtonText() == m.preset, juce::dontSendNotification);
    target.setValue (m.targetLufs, juce::dontSendNotification);
    ceiling.setValue (m.ceilingDbtp, juce::dontSendNotification);
    ab.setToggleState (m.enabled, juce::dontSendNotification);
    if (structural || params == nullptr) rebuildParams();
    else params->refreshValues();
}

void MasterScreen::timerCallback()
{
    auto* g = session.engine().getGraph();
    if (g == nullptr) return;
    auto& l = g->masterLoudness();
    const float integrated = l.getIntegratedLufs();
    meterText = juce::String::formatted ("Momentary %s   Short-term %s   Integrated %s   True peak %s",
                                         l.getMomentaryLufs() < -90 ? "-" : juce::String (l.getMomentaryLufs(), 1).toRawUTF8(),
                                         l.getShortTermLufs() < -90 ? "-" : juce::String (l.getShortTermLufs(), 1).toRawUTF8(),
                                         integrated < -90 ? "-" : (juce::String (integrated, 1) + " LUFS").toRawUTF8(),
                                         l.getTruePeakDb() < -90 ? "-" : (juce::String (l.getTruePeakDb(), 1) + " dBTP").toRawUTF8());
    platformRows.clear();
    if (integrated > -90)
        for (auto& t : platformTargets())
            platformRows.push_back ({ t.name, platformAdjustmentDb (t, integrated, l.getTruePeakDb()) });
    repaint();
}

void MasterScreen::runAiMaster()
{
    ai.setEnabled (false);
    notes.setText ("Rendering and analysing the mix...", juce::dontSendNotification);
    auto snapshot = session.project();
    auto folder = session.getFolder();
    worker.addJob ([this, snapshot, folder]
    {
        RenderRequest req;
        req.sampleRate = 44100;
        req.includeMastering = false;
        req.projectFolder = folder;
        auto mix = OfflineRenderer::render (snapshot, req);
        auto sug = mastering::suggest (mix, req.sampleRate, snapshot.style, snapshot.mastering.targetLufs, snapshot.mastering.ceilingDbtp,
                                       snapshot.mastering.preset);
        juce::MessageManager::callAsync ([this, sug]
        {
            ai.setEnabled (true);
            auto settings = sug.settings;
            settings.enabled = true;
            session.setMastering (settings, true);
            lastBeforeLufs = sug.before.integratedLufs;
            lastAfterLufs = sug.after.integratedLufs;
            juce::String text = "AI master (" + settings.preset + " base):\n";
            for (auto& n : sug.notes) text << "- " << n << "\n";
            text << juce::String::formatted ("\nBefore: %.1f LUFS, %.1f dBTP.  After: %.1f LUFS, %.1f dBTP.",
                                             (double) sug.before.integratedLufs, (double) sug.before.truePeakDb,
                                             (double) sug.after.integratedLufs, (double) sug.after.truePeakDb);
            notes.setText (text, juce::dontSendNotification);
            rebuildParams();
        });
    });
}

void MasterScreen::albumMastering()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the mastered songs, in album order (you can reorder by renaming 01-, 02-...)",
                                                   store::exportsFolder (session.getFolder()), "*.wav;*.flac;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
    {
        auto files = fc.getResults();
        if (files.isEmpty()) return;
        files.sort();
        std::vector<exporting::AlbumTrack> tracks;
        for (auto& f : files) tracks.push_back ({ f, f.getFileNameWithoutExtension(), 2.0, 0.0f, {} });
        const float tgt = session.project().mastering.targetLufs, ceil = session.project().mastering.ceilingDbtp;
        auto out = session.settings().projectsFolder().getChildFile ("Album " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d"));
        notes.setText ("Matching loudness across " + juce::String ((int) tracks.size()) + " songs...", juce::dontSendNotification);
        worker.addJob ([this, tracks, tgt, ceil, out]() mutable
        {
            exporting::matchAlbumLoudness (tracks, tgt, ceil);
            auto res = exporting::writeAlbum (tracks, out);
            juce::String text = "Album written to " + out.getFullPathName() + "\n";
            for (auto& t : tracks)
                text << juce::String::formatted ("- %s: %.1f LUFS, trim %+.1f dB\n", t.title.toRawUTF8(), (double) t.loudness.integratedLufs, (double) t.trimDb);
            text << "Gaps: 2 s. A CUE sheet is included for CD replication.";
            juce::MessageManager::callAsync ([this, text] { notes.setText (text, juce::dontSendNotification); });
        });
    });
}

void MasterScreen::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    auto r = getLocalBounds().reduced (16);
    auto right = r.removeFromRight (360);
    paintPanel (g, right.toFloat());
    auto inner = right.reduced (14).withTrimmedTop (30);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (meterText, inner.removeFromTop (44).withTrimmedLeft (60), juce::Justification::topLeft, 3);
    inner.removeFromTop (8);
    g.setColour (colours::textDim);
    g.drawText ("How each platform will play it", inner.removeFromTop (20).withTrimmedLeft (60), juce::Justification::centredLeft);
    for (auto& [name, adj] : platformRows)
    {
        auto row = inner.removeFromTop (22).withTrimmedLeft (60);
        g.setColour (colours::text);
        g.drawText (name, row.removeFromLeft (130), juce::Justification::centredLeft);
        g.setColour (std::abs (adj) < 1.0f ? colours::good : colours::warning);
        g.drawText (std::abs (adj) < 0.05f ? juce::String ("plays as is") : (adj < 0 ? "turned down " : "turned up ") + juce::String (std::abs (adj), 1) + " dB",
                    row, juce::Justification::centredLeft);
    }
    if (platformRows.empty())
    {
        g.setColour (colours::textDim);
        g.drawFittedText ("Play the song (ideally the loudest part) to measure it.", inner.removeFromTop (40).withTrimmedLeft (60), juce::Justification::topLeft, 2);
    }
}

void MasterScreen::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto right = r.removeFromRight (360).reduced (14);
    loudnessTitle.setBounds (right.removeFromTop (24));
    meter.setBounds (right.withWidth (50).withTrimmedBottom (juce::jmax (0, right.getHeight() - 360)));
    resetMeter.setBounds (right.removeFromBottom (32).removeFromLeft (120));
    r.removeFromRight (16);

    auto bar = r.removeFromTop (44);
    for (auto* b : presets) b->setBounds (bar.removeFromLeft (130).reduced (3));
    bar.removeFromLeft (10);
    ai.setBounds (bar.removeFromLeft (130).reduced (3));
    r.removeFromTop (8);
    auto row = r.removeFromTop (34);
    targetL.setBounds (row.removeFromLeft (110));
    target.setBounds (row.removeFromLeft (260));
    row.removeFromLeft (16);
    ceilingL.setBounds (row.removeFromLeft (90));
    ceiling.setBounds (row.removeFromLeft (240));
    r.removeFromTop (8);
    auto row2 = r.removeFromTop (36);
    ab.setBounds (row2.removeFromLeft (260).reduced (2));
    album.setBounds (row2.removeFromLeft (170).reduced (2));
    r.removeFromTop (8);
    notes.setBounds (r.removeFromBottom (160));
    paramView.setBounds (r);
    if (params != nullptr) params->setSize (r.getWidth() - 12, params->preferredHeight (r.getWidth() - 12));
}

} // namespace studio::ui
