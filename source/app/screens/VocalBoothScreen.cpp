#include "VocalBoothScreen.h"
#include "core/vocal/VocalChain.h"
#include "core/vocal/VocalTools.h"
#include "core/model/TempoMap.h"
#include "core/mixing/MixTools.h"
#include "core/engine/OfflineRenderer.h"

namespace studio::ui
{

/** One chain step: bypass toggle, description and (in advanced view) its parameters. */
struct VocalBoothScreen::StepRow : public juce::Component
{
    StepRow (VocalBoothScreen& o, juce::String stepId, juce::String name, juce::String desc) : owner (o), id (std::move (stepId))
    {
        on.setButtonText (name);
        on.setClickingTogglesState (true);
        on.onClick = [this] { owner.current.bypass[id] = ! on.getToggleState(); owner.apply (true); };
        addAndMakeVisible (on);
        description.setText (desc, juce::dontSendNotification);
        styleCaption (description);
        addAndMakeVisible (description);
    }

    void buildParams (bool show)
    {
        params.clear();
        if (! show) return;
        VocalChain reference; // parameter descriptions for this step
        auto* step = reference.findStep (id);
        if (step == nullptr) return;
        for (auto* fx : step->effects)
        {
            const auto prefix = fx->getType() == "compressor" ? juce::String ("comp") : fx->getType() == "pitchcorrect" ? juce::String ("pitch")
                              : fx->getType() == "denoise" ? juce::String ("denoise") : fx->getType();
            std::vector<dsp::ParamInfo> infos;
            for (auto& pi : fx->getParams())
                if (! (prefix == "pitch" && (pi.id == "tonic" || pi.id == "scale")) && ! ((prefix == "reverb" || prefix == "delay") && pi.id == "mix"))
                    infos.push_back (pi);
            auto defaults = VocalChain::presetValues (owner.current.preset);
            auto* panel = params.add (new ParamPanel (infos,
                [this, prefix, defaults] (const juce::String& p) -> float
                {
                    auto key = prefix + "." + p;
                    if (auto it = owner.current.params.find (key); it != owner.current.params.end()) return it->second;
                    if (auto it = defaults.find (key); it != defaults.end()) return it->second;
                    VocalChain ref;
                    return ref.effectForStep (id) != nullptr ? ref.effectForStep (id)->getParam (p) : 0.0f;
                },
                [this, prefix] (const juce::String& p, float v, bool end) { owner.current.params[prefix + "." + p] = v; owner.apply (end); }, 8));
            addAndMakeVisible (panel);
        }
    }

    int preferredHeight (int width) const
    {
        int h = 40;
        for (auto* p : params) h += p->preferredHeight (width - 20) + 4;
        return h;
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto top = r.removeFromTop (30);
        on.setBounds (top.removeFromLeft (200));
        top.removeFromLeft (10);
        description.setBounds (top);
        for (auto* p : params)
        {
            r.removeFromTop (4);
            p->setBounds (r.removeFromTop (p->preferredHeight (r.getWidth())));
        }
    }

    void paint (juce::Graphics& g) override { paintPanel (g, getLocalBounds().toFloat().reduced (1)); }

    VocalBoothScreen& owner;
    juce::String id;
    juce::TextButton on;
    juce::Label description;
    juce::OwnedArray<ParamPanel> params;
};

//==============================================================================
/** Pitch note editor: detected notes vs targets; drag a note to choose its target. */
class PitchEditorComponent : public juce::Component
{
public:
    PitchEditorComponent (vocal::PitchAnalysis a, double seconds, std::function<void (const vocal::PitchAnalysis&)> onApply)
        : analysis (std::move (a)), length (seconds), apply (std::move (onApply))
    {
        applyButton.onClick = [this] { if (apply) apply (analysis); if (auto* d = findParentComponentOfClass<juce::DialogWindow>()) d->exitModalState (1); };
        addAndMakeVisible (applyButton);
        setSize (1000, 520);
        for (auto& f : analysis.frames)
            if (f.frequency > 0) { lo = std::min (lo, (float) theory::hzToMidi (f.frequency)); hi = std::max (hi, (float) theory::hzToMidi (f.frequency)); }
        if (hi < lo) { lo = 48; hi = 72; }
        lo = std::floor (lo) - 3; hi = std::ceil (hi) + 3;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panel);
        auto area = getLocalBounds().withTrimmedBottom (50).withTrimmedLeft (40);
        for (int n = (int) lo; n <= (int) hi; ++n)
        {
            const float y = yFor ((float) n, area);
            const bool black = juce::MidiMessage::isMidiNoteBlack (n);
            g.setColour (black ? colours::background : colours::panelRaised);
            g.fillRect ((float) area.getX(), y - rowH (area) / 2, (float) area.getWidth(), rowH (area));
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (10.0f));
            if (n % 12 == 0 || ! black) g.drawText (theory::noteName (n), 0, (int) (y - 6), 38, 12, juce::Justification::centredRight);
        }
        // Detected pitch curve
        juce::Path curve;
        bool started = false;
        for (auto& f : analysis.frames)
        {
            if (f.frequency <= 0 || f.confidence < 0.6f) { started = false; continue; }
            const float x = xFor (f.timeSeconds, area), y = yFor ((float) theory::hzToMidi (f.frequency), area);
            if (! started) { curve.startNewSubPath (x, y); started = true; } else curve.lineTo (x, y);
        }
        g.setColour (colours::textDim);
        g.strokePath (curve, juce::PathStrokeType (1.2f));
        // Notes: detected (outline) and target (filled)
        for (size_t i = 0; i < analysis.notes.size(); ++i)
        {
            auto& n = analysis.notes[i];
            const float x0 = xFor (n.startSeconds, area), x1 = xFor (n.endSeconds, area);
            g.setColour (colours::textDim);
            g.drawRect (juce::Rectangle<float> (x0, yFor (n.detectedMidi, area) - rowH (area) / 2, x1 - x0, rowH (area)));
            g.setColour ((n.edited ? colours::accentWarm : colours::accent).withAlpha (0.8f));
            g.fillRoundedRectangle (juce::Rectangle<float> (x0, yFor (n.targetMidi, area) - rowH (area) / 2, x1 - x0, rowH (area)), 3.0f);
        }
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Grey line: what was sung. Blue: the target note (snapped to the key). Drag a note up or down to change it; double-click to reset.",
                    getLocalBounds().removeFromBottom (50).withTrimmedRight (150).reduced (10, 0), juce::Justification::centredLeft, true);
    }

    void resized() override { applyButton.setBounds (getWidth() - 140, getHeight() - 42, 130, 34); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto area = getLocalBounds().withTrimmedBottom (50).withTrimmedLeft (40);
        dragged = -1;
        for (size_t i = 0; i < analysis.notes.size(); ++i)
        {
            auto& n = analysis.notes[i];
            if (e.x >= xFor (n.startSeconds, area) && e.x <= xFor (n.endSeconds, area)) dragged = (int) i;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragged < 0) return;
        auto area = getLocalBounds().withTrimmedBottom (50).withTrimmedLeft (40);
        auto& n = analysis.notes[(size_t) dragged];
        n.targetMidi = std::round (lo + (1.0f - (float) (e.y - area.getY()) / (float) area.getHeight()) * (hi - lo));
        n.edited = true;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (dragged < 0) return;
        auto& n = analysis.notes[(size_t) dragged];
        n.targetMidi = std::round (n.detectedMidi);
        n.edited = true;
        repaint();
    }

private:
    float rowH (juce::Rectangle<int> a) const { return (float) a.getHeight() / (hi - lo + 1); }
    float yFor (float midi, juce::Rectangle<int> a) const { return (float) a.getY() + (1.0f - (midi - lo) / (hi - lo)) * (float) a.getHeight(); }
    float xFor (double t, juce::Rectangle<int> a) const { return (float) a.getX() + (float) (t / juce::jmax (0.01, length)) * (float) a.getWidth(); }

    vocal::PitchAnalysis analysis;
    double length;
    std::function<void (const vocal::PitchAnalysis&)> apply;
    juce::TextButton applyButton { "Apply" };
    float lo = 200, hi = -1;
    int dragged = -1;
};

//==============================================================================
VocalBoothScreen::VocalBoothScreen (Session& s) : Screen (s)
{
    trackChoice.onChange = [this]
    {
        const int i = trackChoice.getSelectedId() - 1;
        auto& tr = session.project().tracks;
        if (i >= 0 && i < (int) tr.size()) session.select (tr[(size_t) i].id);
    };
    addAndMakeVisible (trackChoice);

    makeToggle (enable, "Vocal chain on", false);
    enable.onClick = [this] { current.enabled = enable.getToggleState(); apply (true); };
    addAndMakeVisible (enable);
    makeToggle (advanced, "Advanced", false);
    advanced.onClick = [this] { rebuildSteps(); };
    addAndMakeVisible (advanced);

    for (auto& name : VocalChain::presetNames())
    {
        auto* b = presetButtons.add (new juce::TextButton (name));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (501);
        b->onClick = [this, name]
        {
            current.preset = name;
            current.params.clear(); // a new preset starts clean
            current.enabled = true;
            enable.setToggleState (true, juce::dontSendNotification);
            apply (true);
            rebuildSteps();
        };
        addAndMakeVisible (b);
    }

    amount.slider.setTooltip ("One knob for the whole chain: 0 leaves the voice untouched, 100% is the full preset");
    amount.slider.onValueChange = [this] { current.amount = (float) amount.slider.getValue(); apply (false); };
    amount.slider.onDragEnd = [this] { apply (true); };
    addAndMakeVisible (amount);

    stepsView.setViewedComponent (&stepsHolder, false);
    stepsView.setScrollBarsShown (true, false);
    addAndMakeVisible (stepsView);

    toolsTitle.setText ("Vocal tools (work on the selected clip)", juce::dontSendNotification);
    styleHeading (toolsTitle, 15);
    addAndMakeVisible (toolsTitle);
    styleCaption (info);
    info.setText ("Timing alignment (step 5) tightens backing vocals and doubles to the lead; it works on whole takes, so it lives here.",
                  juce::dontSendNotification);
    addAndMakeVisible (info);
    harmony2.onClick = [this] { makeHarmonies (2); };
    harmony3.onClick = [this] { makeHarmonies (3); };
    harmony4.onClick = [this] { makeHarmonies (4); };
    doubler.onClick = [this]
    {
        const auto id = session.selectedTrackId;
        session.perform ("Add doubler", [id] (Project& p) { if (auto* t = p.findTrack (id)) mixing::setEffect (*t, "doubler", { { "mix", 0.5f } }); });
        session.status ("Doubler added. Adjust it in the Mixer.");
    };
    toMidi.onClick = [this] { voiceToMidi(); };
    align.onClick = [this] { alignToLead(); };
    pitchEdit.onClick = [this] { openPitchEditor(); };
    harmony3.setTooltip ("Creates harmony parts from the lead vocal, in key, on new tracks");
    toMidi.setTooltip ("Hum a melody and turn it into notes for any instrument");
    for (auto* b : { &harmony2, &harmony3, &harmony4, &doubler, &toMidi, &align, &pitchEdit }) addAndMakeVisible (b);
}

VocalChainSettings VocalBoothScreen::settings() const
{
    if (auto* t = session.project().findTrack (session.selectedTrackId)) return t->vocalChain;
    return {};
}

void VocalBoothScreen::apply (bool gestureEnd)
{
    if (session.selectedTrackId.isEmpty()) return;
    session.setVocalChain (session.selectedTrackId, current, gestureEnd);
    for (auto* row : steps) row->on.setToggleState (! current.bypass[row->id], juce::dontSendNotification);
}

void VocalBoothScreen::refresh (bool structural)
{
    const auto& p = session.project();
    if (structural)
    {
        trackChoice.clear (juce::dontSendNotification);
        // Default to the first vocal track when the selection is not an audio track.
        auto* sel = p.findTrack (session.selectedTrackId);
        if (sel == nullptr || sel->type != TrackType::Audio)
            for (auto& t : p.tracks) if (t.type == TrackType::Audio) { session.selectedTrackId = t.id; break; }
        for (int i = 0; i < (int) p.tracks.size(); ++i)
            if (p.tracks[(size_t) i].type == TrackType::Audio)
            {
                trackChoice.addItem (p.tracks[(size_t) i].name, i + 1);
                if (p.tracks[(size_t) i].id == session.selectedTrackId) trackChoice.setSelectedId (i + 1, juce::dontSendNotification);
            }
    }
    current = settings();
    enable.setToggleState (current.enabled, juce::dontSendNotification);
    amount.slider.setValue (current.amount, juce::dontSendNotification);
    for (auto* b : presetButtons) b->setToggleState (b->getButtonText() == current.preset, juce::dontSendNotification);
    if (structural || steps.isEmpty()) rebuildSteps();
    for (auto* row : steps) row->on.setToggleState (! current.bypass[row->id], juce::dontSendNotification);
}

void VocalBoothScreen::rebuildSteps()
{
    steps.clear();
    VocalChain reference;
    for (auto& st : reference.getSteps())
    {
        auto* row = steps.add (new StepRow (*this, st.id, st.name, st.description));
        row->buildParams (advanced.getToggleState());
        row->on.setToggleState (! current.bypass[st.id], juce::dontSendNotification);
        stepsHolder.addAndMakeVisible (row);
    }
    resized();
}

void VocalBoothScreen::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void VocalBoothScreen::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto top = r.removeFromTop (40);
    trackChoice.setBounds (top.removeFromLeft (240).reduced (0, 4));
    top.removeFromLeft (10);
    enable.setBounds (top.removeFromLeft (150).reduced (2));
    advanced.setBounds (top.removeFromRight (110).reduced (2));
    r.removeFromTop (8);

    auto presetRow = r.removeFromTop (100);
    amount.setBounds (presetRow.removeFromRight (110));
    const int bw = presetRow.getWidth() / juce::jmax (1, (presetButtons.size() + 1) / 2);
    auto row1 = presetRow.removeFromTop (48), row2 = presetRow;
    for (int i = 0; i < presetButtons.size(); ++i)
        presetButtons[i]->setBounds ((i < 4 ? row1 : row2).withX ((i < 4 ? row1 : row2).getX() + (i % 4) * bw).withWidth (bw).reduced (3));

    auto tools = r.removeFromBottom (110);
    toolsTitle.setBounds (tools.removeFromTop (24));
    info.setBounds (tools.removeFromBottom (22));
    auto tr = tools.reduced (0, 6);
    const int tw = tr.getWidth() / 7;
    for (auto* b : { &harmony2, &harmony3, &harmony4, &doubler, &toMidi, &align, &pitchEdit }) b->setBounds (tr.removeFromLeft (tw).reduced (3));

    r.removeFromTop (8);
    stepsView.setBounds (r);
    const int w = r.getWidth() - 12;
    int y = 0;
    for (auto* row : steps)
    {
        const int h = row->preferredHeight (w);
        row->setBounds (0, y, w, h);
        y += h + 6;
    }
    stepsHolder.setSize (w, y);
}

//==============================================================================
bool VocalBoothScreen::selectedClipAudio (juce::AudioBuffer<float>& out, double& sr, const Clip*& clip, const Track*& track)
{
    track = session.project().findTrack (session.selectedTrackId);
    clip = nullptr;
    if (track == nullptr) return false;
    for (auto& c : track->clips)
        if (c.id == session.selectedClipId || (session.selectedClipId.isEmpty() && ! c.isMidi)) { clip = &c; break; }
    if (clip == nullptr || clip->isMidi)
    {
        tell ("Select a vocal clip", "Click an audio clip in Arrange (or record one) first.");
        return false;
    }
    juce::AudioBuffer<float> all;
    if (! ClipCache::readFile (store::resolve (session.getFolder(), clip->audioFile), all, sr)) return false;
    TempoMap tm (session.project());
    const double secs = tm.beatsToSeconds (clip->endBeat()) - tm.beatsToSeconds (clip->startBeat);
    const int start = juce::jlimit (0, all.getNumSamples(), (int) (clip->sourceOffsetSeconds * sr));
    const int len = juce::jlimit (0, all.getNumSamples() - start, (int) (secs * sr));
    out.setSize (all.getNumChannels(), juce::jmax (1, len));
    for (int ch = 0; ch < all.getNumChannels(); ++ch) out.copyFrom (ch, 0, all, ch, start, len);
    return len > 0;
}

juce::String VocalBoothScreen::writeAudio (const juce::AudioBuffer<float>& audio, double sr, const juce::String& name)
{
    auto f = store::audioFolder (session.getFolder()).getChildFile (juce::File::createLegalFileName (name) + ".wav").getNonexistentSibling();
    writeAudioFile (f, audio, sr, 32);
    return store::relativePath (session.getFolder(), f);
}

void VocalBoothScreen::makeHarmonies (int parts)
{
    juce::AudioBuffer<float> audio; double sr = 0; const Clip* clip = nullptr; const Track* track = nullptr;
    if (! selectedClipAudio (audio, sr, clip, track)) return;
    const auto key = session.project().keyAt (clip->startBeat);
    auto harmonies = vocal::generateHarmonies (audio, sr, key, vocal::harmonyPreset (parts));
    const Clip base = *clip;
    const auto trackName = track->name;
    std::vector<juce::String> files;
    for (size_t i = 0; i < harmonies.size(); ++i) files.push_back (writeAudio (harmonies[i], sr, trackName + " harmony " + juce::String ((int) i + 1)));
    session.perform ("Harmony generator", [=] (Project& p)
    {
        juce::String busId;
        for (auto& t : p.tracks) if (t.type == TrackType::Bus && t.name.containsIgnoreCase ("backing")) busId = t.id;
        for (size_t i = 0; i < files.size(); ++i)
        {
            auto& t = p.addTrack (TrackType::Audio, trackName + " harmony " + juce::String ((int) i + 1), TrackRole::BackingVocal);
            t.volumeDb = -6;
            t.outputBusId = busId;
            t.vocalChain.enabled = true;
            t.vocalChain.preset = "Choir stack";
            t.vocalChain.amount = 0.5f;
            Clip c = base;
            c.id = makeId ("clip");
            c.audioFile = files[i];
            c.sourceOffsetSeconds = 0;
            c.takeId.clear();
            c.name = "Harmony " + juce::String ((int) i + 1);
            t.clips.push_back (c);
        }
    });
    session.status (juce::String (parts) + " harmony parts added on new tracks (in " + key.toString() + ").");
}

void VocalBoothScreen::voiceToMidi()
{
    juce::AudioBuffer<float> audio; double sr = 0; const Clip* clip = nullptr; const Track* track = nullptr;
    if (! selectedClipAudio (audio, sr, clip, track)) return;
    const auto key = session.project().keyAt (clip->startBeat);
    auto notes = vocal::voiceToMidi (audio, sr, TempoMap (session.project()).bpmAt (clip->startBeat), &key, 0.25);
    if (notes.empty()) { tell ("Voice to MIDI", "No clear notes were found. Hum steadily with clear gaps between notes."); return; }
    const double start = clip->startBeat, len = clip->lengthBeats;
    juce::String newId;
    session.perform ("Voice to MIDI", [&] (Project& p)
    {
        auto& t = p.addTrack (TrackType::Midi, "Melody (from voice)", TrackRole::Lead);
        t.instrument.id = "grand_piano";
        Clip c;
        c.id = makeId ("clip");
        c.isMidi = true;
        c.name = "Hummed melody";
        c.startBeat = start;
        c.lengthBeats = len;
        c.notes = notes;
        t.clips.push_back (c);
        newId = t.id;
    });
    session.select (newId);
    session.status (juce::String ((int) notes.size()) + " notes created. Change the instrument in the Beat lab or Arrange.");
}

void VocalBoothScreen::alignToLead()
{
    juce::AudioBuffer<float> audio; double sr = 0; const Clip* clip = nullptr; const Track* track = nullptr;
    if (! selectedClipAudio (audio, sr, clip, track)) return;
    // Find the lead vocal clip overlapping this one.
    const Track* lead = nullptr;
    for (auto& t : session.project().tracks) if (t.role == TrackRole::LeadVocal && t.id != track->id) { lead = &t; break; }
    if (lead == nullptr) { tell ("Timing alignment", "Mark one track as the lead vocal (its role) to align backing vocals to it."); return; }

    RenderRequest req;
    req.sampleRate = sr;
    req.includeMastering = false;
    req.startBeat = clip->startBeat;
    req.endBeat = clip->endBeat();
    req.tailSeconds = 0;
    req.projectFolder = session.getFolder();
    Project dry = session.project();
    for (auto& t : dry.tracks) { t.vocalChain.enabled = false; t.effects.clear(); t.volumeDb = 0; t.outputBusId.clear(); t.sends.clear(); }
    auto leadAudio = OfflineRenderer::renderTrack (dry, lead->id, req);
    auto aligned = vocal::alignTiming (leadAudio, audio, sr, 0.85f);
    aligned.setSize (aligned.getNumChannels(), audio.getNumSamples(), true, true);
    const auto file = writeAudio (aligned, sr, track->name + " aligned");
    const auto trackId = track->id, clipId = clip->id;
    session.perform ("Align timing", [=] (Project& p)
    {
        if (auto* c = p.findTrack (trackId)->findClip (clipId)) { c->audioFile = file; c->sourceOffsetSeconds = 0; c->takeId.clear(); }
    });
    session.status ("Timing tightened to \"" + lead->name + "\". Undo restores the original.");
}

void VocalBoothScreen::openPitchEditor()
{
    juce::AudioBuffer<float> audio; double sr = 0; const Clip* clip = nullptr; const Track* track = nullptr;
    if (! selectedClipAudio (audio, sr, clip, track)) return;
    auto analysis = vocal::analyseForEditor (audio, sr, session.project().keyAt (clip->startBeat));
    const auto trackId = track->id, clipId = clip->id, name = track->name;
    auto audioCopy = std::make_shared<juce::AudioBuffer<float>> (audio);
    auto* editor = new PitchEditorComponent (analysis, audio.getNumSamples() / sr, [this, audioCopy, sr, trackId, clipId, name] (const vocal::PitchAnalysis& a)
    {
        auto out = vocal::applyPitchEdits (*audioCopy, a, 0.0f);
        const auto file = writeAudio (out, sr, name + " tuned");
        session.perform ("Pitch edit", [=] (Project& p)
        {
            if (auto* c = p.findTrack (trackId)->findClip (clipId)) { c->audioFile = file; c->sourceOffsetSeconds = 0; c->takeId.clear(); }
        });
    });
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (editor);
    o.dialogTitle = "Pitch editor - " + name;
    o.dialogBackgroundColour = colours::panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

} // namespace studio::ui
