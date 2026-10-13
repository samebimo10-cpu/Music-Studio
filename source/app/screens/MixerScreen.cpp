#include "MixerScreen.h"
#include "core/engine/OfflineRenderer.h"
#include "core/engine/PluginHost.h"
#include "core/mixing/MixTools.h"
#include "core/vocal/VocalChain.h"

namespace studio::ui
{

/** Effect slot button: click edits, right-click opens the slot menu. */
struct SlotButton : public juce::TextButton
{
    using juce::TextButton::TextButton;
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick) onRightClick();
        else juce::TextButton::mouseDown (e);
    }
};

//==============================================================================
struct MixerScreen::Strip : public juce::Component
{
    Strip (Session& s, juce::String trackId, bool isMaster) : session (s), id (std::move (trackId)), master (isMaster)
    {
        name.setJustificationType (juce::Justification::centred);
        name.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (name);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        fader.setRange (-60, 12, 0.1);
        fader.setSkewFactorFromMidPoint (-12);
        fader.setTextValueSuffix (" dB");
        fader.setDoubleClickReturnValue (true, 0);
        addAndMakeVisible (fader);

        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1, 1, 0.01);
        pan.setDoubleClickReturnValue (true, 0);
        pan.setTooltip ("Pan (double-click to centre)");
        addAndMakeVisible (pan);

        for (auto* b : { &mute, &solo }) { b->setClickingTogglesState (true); addAndMakeVisible (b); }
        solo.setColour (juce::TextButton::buttonOnColourId, colours::accentWarm);
        addFx.setTooltip ("Add an effect");
        addFx.onClick = [this] { addEffectMenu(); };
        addAndMakeVisible (addFx);
        addAndMakeVisible (meter);
        meter.setShowScale (master);

        if (master)
        {
            meter.source = masterMeterSource (session.engine());
            fader.onValueChange = [this] { if (auto* g = session.engine().getGraph()) g->setMasterVolume ((float) fader.getValue()); };
            pan.setVisible (false);
            mute.setVisible (false);
            solo.setVisible (false);
            addFx.setButtonText ("Mastering");
            addFx.onClick = [this] { if (onMaster) onMaster(); };
        }
        else
        {
            meter.source = trackMeterSource (session.engine(), id);
            fader.onValueChange = [this] { session.setTrackVolume (id, (float) fader.getValue(), false); };
            fader.onDragEnd = [this] { session.setTrackVolume (id, (float) fader.getValue(), true); };
            pan.onValueChange = [this] { session.setTrackPan (id, (float) pan.getValue(), false); };
            pan.onDragEnd = [this] { session.setTrackPan (id, (float) pan.getValue(), true); };
            mute.onClick = [this] { session.setTrackMute (id, mute.getToggleState()); };
            solo.onClick = [this] { session.setTrackSolo (id, solo.getToggleState()); };
        }
        refresh();
    }

    std::function<void()> onMaster;

    void refresh()
    {
        if (master)
        {
            name.setText ("Master", juce::dontSendNotification);
            slots.clear();
            return;
        }
        auto* t = session.project().findTrack (id);
        if (t == nullptr) return;
        name.setText (t->name, juce::dontSendNotification);
        fader.setValue (t->volumeDb, juce::dontSendNotification);
        pan.setValue (t->pan, juce::dontSendNotification);
        mute.setToggleState (t->mute, juce::dontSendNotification);
        solo.setToggleState (t->solo, juce::dontSendNotification);
        colour = juce::Colour (t->colour);

        slots.clear();
        if (t->vocalChain.enabled)
        {
            auto* b = slots.add (new juce::TextButton ("Vocal: " + t->vocalChain.preset));
            b->setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.4f));
            b->onClick = [this] { session.select (id); if (onVocal) onVocal(); };
            addAndMakeVisible (b);
        }
        for (auto& e : t->effects)
        {
            juce::String label = e.type == "plugin" ? juce::File (e.pluginIdentifier).getFileNameWithoutExtension() : e.type;
            for (auto& [type, display] : dsp::availableEffectTypes()) if (type == e.type) label = display;
            if (e.sidechainTrackId.isNotEmpty()) label << " (SC)";
            auto* b = new SlotButton (label);
            slots.add (b);
            b->setColour (juce::TextButton::buttonColourId, e.bypassed ? colours::background : colours::panelRaised.brighter (0.05f));
            b->setColour (juce::TextButton::textColourOffId, e.bypassed ? colours::textDim : colours::text);
            const auto slotId = e.id;
            b->onClick = [this, slotId, b] { MixerScreen::editEffect (session, id, slotId, *b); };
            b->onRightClick = [this, slotId] { slotMenu (slotId); };
            b->setTooltip ("Click to edit, right-click for bypass/remove/sidechain");
            addAndMakeVisible (b);
        }
        sendKnobs.clear();
        for (int i = 0; i < (int) t->sends.size(); ++i)
        {
            auto& s = t->sends[(size_t) i];
            auto* bus = session.project().findTrack (s.busId);
            auto* k = sendKnobs.add (new Knob (bus != nullptr ? bus->name : juce::String ("Send"), -60, 6, s.levelDb, " dB"));
            k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k->slider.setTooltip ("Send to " + (bus != nullptr ? bus->name : juce::String ("bus")));
            k->slider.onValueChange = [this, i, k]
            {
                if (auto* g = session.engine().getGraph()) g->setSendLevel (id, i, (float) k->slider.getValue());
            };
            k->slider.onDragEnd = [this, i, k]
            {
                const float v = (float) k->slider.getValue();
                const auto tid = id;
                session.perform ("Send level", [=] (Project& p) { if (auto* tt = p.findTrack (tid); tt != nullptr && i < (int) tt->sends.size()) tt->sends[(size_t) i].levelDb = v; }, false);
            };
            addAndMakeVisible (k);
        }
        resized();
        repaint();
    }

    std::function<void()> onVocal;

    void slotMenu (const juce::String& slotId)
    {
        auto* t = session.project().findTrack (id);
        if (t == nullptr) return;
        const EffectSlot* slot = nullptr;
        for (auto& e : t->effects) if (e.id == slotId) slot = &e;
        if (slot == nullptr) return;
        juce::PopupMenu m, sc;
        m.addItem (1, slot->bypassed ? "Turn on" : "Bypass");
        m.addItem (2, "Remove");
        m.addItem (3, "Move up");
        if (slot->type == "compressor")
        {
            sc.addItem (100, "None", true, slot->sidechainTrackId.isEmpty());
            int k = 101;
            for (auto& o : session.project().tracks) if (o.id != id) sc.addItem (k++, o.name, true, o.id == slot->sidechainTrackId);
            m.addSubMenu ("Sidechain from (duck under)", sc);
        }
        m.showMenuAsync ({}, [this, slotId] (int r)
        {
            const auto tid = id;
            std::vector<juce::String> others;
            for (auto& o : session.project().tracks) if (o.id != tid) others.push_back (o.id);
            session.perform ("Effect slot", [=] (Project& p)
            {
                auto* tr = p.findTrack (tid);
                for (size_t i = 0; i < tr->effects.size(); ++i)
                {
                    auto& e = tr->effects[i];
                    if (e.id != slotId) continue;
                    if (r == 1) e.bypassed = ! e.bypassed;
                    if (r == 3 && i > 0) std::swap (tr->effects[i], tr->effects[i - 1]);
                    if (r >= 100)
                    {
                        e.sidechainTrackId = r == 100 ? juce::String() : others[(size_t) (r - 101)];
                        e.params["sidechain"] = r == 100 ? 0.0f : 1.0f;
                    }
                    if (r == 2) { tr->effects.erase (tr->effects.begin() + (long) i); }
                    break;
                }
            });
        });
    }

    void addEffectMenu()
    {
        juce::PopupMenu m, plugins;
        int k = 1;
        for (auto& [type, display] : dsp::availableEffectTypes()) m.addItem (k++, display);
        auto fx = PluginHost::getInstance().effects();
        for (size_t i = 0; i < fx.size(); ++i) plugins.addItem (1000 + (int) i, fx[i].manufacturerName + " - " + fx[i].name);
        if (fx.empty()) plugins.addItem (-1, "No plugins found (Settings -> Scan plugins)", false);
        m.addSubMenu ("Plugins (VST3/AU)", plugins);
        m.showMenuAsync ({}, [this, fx] (int r)
        {
            if (r <= 0) return;
            const auto tid = id;
            EffectSlot slot;
            slot.id = makeId ("fx");
            if (r >= 1000)
            {
                slot.type = "plugin";
                slot.pluginIdentifier = fx[(size_t) (r - 1000)].createIdentifierString();
            }
            else slot.type = dsp::availableEffectTypes()[(size_t) (r - 1)].first;
            if (slot.type == "reverb" || slot.type == "delay") slot.params["mix"] = slot.type == "reverb" ? 0.2f : 0.15f;
            session.perform ("Add effect", [=] (Project& p) { if (auto* t = p.findTrack (tid)) t->effects.push_back (slot); });
        });
    }

    void paint (juce::Graphics& g) override
    {
        paintPanel (g, getLocalBounds().toFloat().reduced (2));
        if (! master)
        {
            g.setColour (colour);
            g.fillRect (getLocalBounds().reduced (2).removeFromTop (4));
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        r.removeFromTop (4);
        name.setBounds (r.removeFromTop (22));
        for (auto* s : slots) s->setBounds (r.removeFromTop (26).reduced (0, 1));
        addFx.setBounds (r.removeFromTop (26).reduced (0, 1));
        auto sends = r.removeFromTop (sendKnobs.isEmpty() ? 0 : 62);
        for (auto* k : sendKnobs) k->setBounds (sends.removeFromLeft (juce::jmax (40, sends.getWidth() / juce::jmax (1, sendKnobs.size()))));
        pan.setBounds (r.removeFromTop (master ? 0 : 44).withSizeKeepingCentre (44, 44));
        auto buttons = r.removeFromBottom (master ? 0 : 28);
        mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2));
        solo.setBounds (buttons.reduced (2));
        auto meterArea = r.removeFromRight (master ? 40 : 16);
        meter.setBounds (meterArea);
        fader.setBounds (r);
    }

    Session& session;
    juce::String id;
    bool master;
    juce::Label name;
    juce::Slider fader, pan;
    juce::TextButton mute { "M" }, solo { "S" }, addFx { "+ Effect" };
    juce::OwnedArray<juce::TextButton> slots;
    juce::OwnedArray<Knob> sendKnobs;
    LevelMeter meter;
    juce::Colour colour;
};

//==============================================================================
void MixerScreen::editEffect (Session& session, const juce::String& trackId, const juce::String& slotId, juce::Component& anchor)
{
    auto* t = session.project().findTrack (trackId);
    if (t == nullptr) return;
    const EffectSlot* slot = nullptr;
    for (auto& e : t->effects) if (e.id == slotId) slot = &e;
    if (slot == nullptr) return;

    // Third-party plugins show their own editor.
    if (slot->type == "plugin")
    {
        if (auto* g = session.engine().getGraph())
            if (auto* fx = dynamic_cast<PluginEffect*> (g->findEffect (trackId, slotId)))
                if (auto* ed = fx->getPlugin().createEditorIfNeeded())
                {
                    juce::DialogWindow::LaunchOptions o;
                    o.content.setNonOwned (ed);
                    o.dialogTitle = fx->getDisplayName();
                    o.useNativeTitleBar = true;
                    o.launchAsync();
                    return;
                }
        return;
    }

    auto reference = dsp::createEffect (slot->type);
    if (reference == nullptr) return;
    auto params = slot->params;
    std::vector<dsp::ParamInfo> infos = reference->getParams();
    auto* panel = new ParamPanel (infos,
        [&session, trackId, slotId, infos] (const juce::String& p) -> float
        {
            if (auto* tr = session.project().findTrack (trackId))
                for (auto& e : tr->effects)
                    if (e.id == slotId)
                        if (auto it = e.params.find (p); it != e.params.end()) return it->second;
            for (auto& i : infos) if (i.id == p) return i.defaultValue;
            return 0.0f;
        },
        [&session, trackId, slotId] (const juce::String& p, float v, bool end) { session.setEffectParam (trackId, slotId, p, v, end); }, 5);
    panel->setSize (480, panel->preferredHeight (480));
    juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (panel), anchor.getScreenBounds(), nullptr);
}

MixerScreen::MixerScreen (Session& s) : Screen (s)
{
    stripView.setViewedComponent (&stripHolder, false);
    stripView.setScrollBarsShown (false, true);
    addAndMakeVisible (stripView);

    autoMix.setColour (juce::TextButton::buttonColourId, colours::accent);
    autoMix.setTooltip ("Sets levels, panning, EQ and reverb sends from each track's role (lead vocal, kick, bass, pad...)");
    autoMix.onClick = [this] { runAutoMix(); };
    assistant.setTooltip ("Finds problems like a buried vocal or clashing bass and kick, with one-click fixes");
    assistant.onClick = [this] { runAssistant(); };
    reference.setTooltip ("Load a released song and compare tone and loudness with your mix");
    reference.onClick = [this] { loadReference(); };
    for (auto* b : { &autoMix, &assistant, &reference }) addAndMakeVisible (b);

    auto names = dsp::ListeningCheckEffect::modeNames();
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = listenButtons.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (909);
        b->setToggleState (i == 0, juce::dontSendNotification);
        b->setTooltip ("Hear the mix as it will sound on: " + names[i] + " (monitoring only, never exported)");
        b->onClick = [this, i] { session.engine().setListeningMode (i); };
        addAndMakeVisible (b);
    }

    assistantTitle.setText ("Mix assistant", juce::dontSendNotification);
    styleHeading (assistantTitle, 15);
    addAndMakeVisible (assistantTitle);
    styleCaption (busyLabel);
    addAndMakeVisible (busyLabel);
    issuesView.setViewedComponent (&issues, false);
    issuesView.setScrollBarsShown (true, false);
    addAndMakeVisible (issuesView);
}

MixerScreen::~MixerScreen() { worker.removeAllJobs (true, 10000); }

void MixerScreen::refresh (bool structural)
{
    if (structural)
    {
        strips.clear();
        // Tracks first, then buses, then master.
        for (int pass = 0; pass < 2; ++pass)
            for (auto& t : session.project().tracks)
                if ((t.type == TrackType::Bus) == (pass == 1))
                {
                    auto* s = strips.add (new Strip (session, t.id, false));
                    s->onVocal = [this] { if (navigate) navigate ("vocal"); };
                    stripHolder.addAndMakeVisible (s);
                }
        auto* m = strips.add (new Strip (session, {}, true));
        m->onMaster = [this] { if (navigate) navigate ("master"); };
        stripHolder.addAndMakeVisible (m);
        resized();
    }
    else
        for (auto* s : strips) s->refresh();
}

void MixerScreen::runAutoMix()
{
    if (busy.exchange (true)) return;
    busyLabel.setText ("Listening to each track...", juce::dontSendNotification);
    auto snapshot = session.project();
    auto folder = session.getFolder();
    worker.addJob ([this, snapshot, folder]
    {
        auto analyses = mixing::analyseTracks (snapshot, folder);
        juce::MessageManager::callAsync ([this, analyses]
        {
            juce::StringArray changes;
            session.perform ("Auto-mix", [&] (Project& p) { changes = mixing::autoMix (p, analyses).changes; });
            busy = false;
            busyLabel.setText ("Auto-mix set " + juce::String (changes.size()) + " things. Adjust to taste, then run the Mix assistant.", juce::dontSendNotification);
        });
    });
}

void MixerScreen::runAssistant()
{
    if (busy.exchange (true)) return;
    busyLabel.setText ("Analysing the mix...", juce::dontSendNotification);
    auto snapshot = session.project();
    auto folder = session.getFolder();
    worker.addJob ([this, snapshot, folder]
    {
        auto analyses = mixing::analyseTracks (snapshot, folder);
        RenderRequest req;
        req.sampleRate = 44100;
        req.includeMastering = false;
        req.projectFolder = folder;
        auto mix = OfflineRenderer::render (snapshot, req);
        auto found = mixing::assistMix (snapshot, mix, req.sampleRate, analyses);
        juce::MessageManager::callAsync ([this, found]
        {
            busy = false;
            std::vector<IssueList::Item> items;
            for (auto& f : found)
            {
                IssueList::Item it;
                it.level = 2;
                it.title = f.title;
                it.detail = f.detail;
                auto fix = f.fix;
                auto title = f.title;
                it.action = [this, fix, title] { session.perform ("Fix: " + title, [fix] (Project& p) { fix (p); }); session.status ("Fixed: " + title + ". Undo if you prefer it before."); };
                items.push_back (it);
            }
            if (items.empty()) items.push_back ({ 0, "No problems found", "Vocal level, low end, tone, headroom and mono compatibility all look fine.", nullptr, {} });
            issues.setItems (items);
            issues.setSize (juce::jmax (100, issuesView.getWidth() - 10), issues.preferredHeight());
            busyLabel.setText ({}, juce::dontSendNotification);
        });
    });
}

void MixerScreen::loadReference()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a released song to compare against", juce::File(), "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.m4a");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.existsAsFile() || busy.exchange (true)) return;
        busyLabel.setText ("Comparing with " + f.getFileNameWithoutExtension() + "...", juce::dontSendNotification);
        auto snapshot = session.project();
        auto folder = session.getFolder();
        worker.addJob ([this, f, snapshot, folder]
        {
            juce::AudioBuffer<float> ref;
            double rate = 0;
            juce::String text;
            if (ClipCache::readFile (f, ref, rate))
            {
                RenderRequest req;
                req.sampleRate = rate;
                req.projectFolder = folder;
                auto mix = OfflineRenderer::render (snapshot, req);
                auto cmp = mixing::compareToReference (mix, ref, rate);
                text = cmp.notes.joinIntoString ("\n");
            }
            else text = "Could not read the reference file.";
            juce::MessageManager::callAsync ([this, text, name = f.getFileNameWithoutExtension()]
            {
                busy = false;
                busyLabel.setText ({}, juce::dontSendNotification);
                std::vector<IssueList::Item> items { { 1, "Compared with \"" + name + "\" (mastered mix vs reference)", text, nullptr, {} } };
                issues.setItems (items);
                issues.setSize (juce::jmax (100, issuesView.getWidth() - 10), issues.preferredHeight() + 40);
            });
        });
    });
}

void MixerScreen::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    auto side = getLocalBounds().reduced (12).removeFromRight (320).withTrimmedTop (44);
    paintPanel (g, side.toFloat());
}

void MixerScreen::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto bar = r.removeFromTop (36);
    autoMix.setBounds (bar.removeFromLeft (110).reduced (2));
    assistant.setBounds (bar.removeFromLeft (130).reduced (2));
    reference.setBounds (bar.removeFromLeft (150).reduced (2));
    bar.removeFromLeft (20);
    for (auto* b : listenButtons) b->setBounds (bar.removeFromLeft (110).reduced (2));
    r.removeFromTop (8);

    auto side = r.removeFromRight (320).reduced (10);
    r.removeFromRight (10);
    assistantTitle.setBounds (side.removeFromTop (24));
    busyLabel.setBounds (side.removeFromTop (40));
    issuesView.setBounds (side);
    issues.setSize (juce::jmax (100, side.getWidth() - 10), issues.preferredHeight());

    stripView.setBounds (r);
    const int w = 118, h = r.getHeight() - 16;
    for (int i = 0; i < strips.size(); ++i) strips[i]->setBounds (i * (w + 4), 0, i == strips.size() - 1 ? w + 30 : w, h);
    stripHolder.setSize (strips.size() * (w + 4) + 40, h);
}

} // namespace studio::ui
