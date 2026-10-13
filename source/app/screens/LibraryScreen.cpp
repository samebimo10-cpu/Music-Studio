#include "LibraryScreen.h"
#include "core/instruments/InstrumentLibrary.h"
#include "core/instruments/SamplePack.h"
#include "core/library/LibraryIndex.h"

namespace studio::ui
{

namespace
{
    /** Plays preview buffers through the open audio device, alongside the engine. */
    class PreviewPlayer
    {
    public:
        explicit PreviewPlayer (juce::AudioDeviceManager& dm) : devices (dm) { devices.addAudioCallback (&player); player.setSource (&mixer); }
        ~PreviewPlayer() { player.setSource (nullptr); devices.removeAudioCallback (&player); mixer.removeAllInputs(); }

        void play (juce::AudioBuffer<float> audio, double rate)
        {
            mixer.removeAllInputs();
            auto* mem = new juce::MemoryAudioSource (audio, true, false);
            auto* resampler = new juce::ResamplingAudioSource (mem, true, 2);
            const double deviceRate = devices.getCurrentAudioDevice() != nullptr ? devices.getCurrentAudioDevice()->getCurrentSampleRate() : rate;
            resampler->setResamplingRatio (rate / deviceRate);
            mixer.addInputSource (resampler, true);
        }
        void stop() { mixer.removeAllInputs(); }

    private:
        juce::AudioDeviceManager& devices;
        juce::AudioSourcePlayer player;
        juce::MixerAudioSource mixer;
    };

    std::unique_ptr<PreviewPlayer> previewPlayer;
    PreviewPlayer& previewFor (Session& s)
    {
        if (previewPlayer == nullptr) previewPlayer = std::make_unique<PreviewPlayer> (s.engine().getDeviceManager());
        return *previewPlayer;
    }
}

//==============================================================================
struct LibraryScreen::InstrumentList : public juce::Component, private juce::ListBoxModel
{
    explicit InstrumentList (Session& s) : session (s)
    {
        list.setModel (this);
        list.setRowHeight (40);
        addAndMakeVisible (list);
        family.addItem ("All families", 1);
        for (auto& f : instruments::families()) family.addItem (f, family.getNumItems() + 1);
        family.setSelectedId (1);
        family.onChange = [this] { filter(); };
        addAndMakeVisible (family);
        add.onClick = [this] { addToSong(); };
        audition.onClick = [this] { previewSelected(); };
        addAndMakeVisible (add);
        addAndMakeVisible (audition);
        styleCaption (note);
        note.setText ("The core install ships synthesised starter instruments. Recorded multi-sampled packs (several velocity layers and "
                      "round-robins) download on demand and stream from disk - see Sound packs.", juce::dontSendNotification);
        addAndMakeVisible (note);
        filter();
    }

    void filter()
    {
        items.clear();
        packItems.clear();
        for (auto& i : instruments::catalogue())
            if (family.getSelectedId() == 1 || i.family == family.getText()) items.push_back (&i);
        for (auto& p : SamplePackManager::getInstance().scanInstalled())
            for (auto& id : p.instrumentIds)
            {
                packItems.push_back ({ "sampler:" + p.id + "/" + id, id + " (" + p.name + ")", "Sound pack", "Streams from disk", TrackRole::Unknown, false, false });
            }
        for (auto& pi : packItems) if (family.getSelectedId() == 1) items.push_back (&pi);
        list.updateContent();
        list.repaint();
    }

    int getNumRows() override { return (int) items.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) items.size()) return;
        if (selected) g.fillAll (colours::accent.withAlpha (0.3f));
        auto& i = *items[(size_t) row];
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (i.name, 12, 2, w / 2, h / 2, juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (i.family + " - " + i.description, 12, h / 2, w - 24, h / 2 - 2, juce::Justification::centredLeft);
    }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { previewSelected(); }

    void previewSelected()
    {
        const int r = list.getSelectedRow();
        if (r < 0) return;
        auto& info = *items[(size_t) r];
        auto inst = instruments::create (info.id);
        const double sr = 44100;
        inst->prepare (sr, 512);
        juce::AudioBuffer<float> b (2, (int) (2.5 * sr));
        b.clear();
        // A short phrase: a chord or a drum groove.
        const bool drums = info.isDrumKit;
        const std::vector<std::pair<double, int>> events = drums
            ? std::vector<std::pair<double, int>> { { 0.0, 36 }, { 0.25, 42 }, { 0.5, 38 }, { 0.75, 42 }, { 1.0, 36 }, { 1.125, 36 }, { 1.25, 42 }, { 1.5, 38 }, { 1.75, 46 } }
            : std::vector<std::pair<double, int>> { { 0.0, 48 }, { 0.0, 60 }, { 0.0, 64 }, { 0.0, 67 }, { 0.5, 72 }, { 0.75, 71 }, { 1.0, 67 } };
        int pos = 0;
        size_t e = 0;
        while (pos < b.getNumSamples())
        {
            const int n = juce::jmin (256, b.getNumSamples() - pos);
            while (e < events.size() && events[e].first * sr <= pos) { inst->noteOn (events[e].second, 0.8f); ++e; }
            if (pos >= (int) (1.6 * sr) && pos < (int) (1.6 * sr) + 256) inst->allNotesOff (false);
            inst->render (b, pos, n);
            pos += n;
        }
        previewFor (session).play (b, sr);
    }

    void addToSong()
    {
        const int r = list.getSelectedRow();
        if (r < 0 || ! session.hasProject()) return;
        auto info = *items[(size_t) r];
        juce::String id;
        session.perform ("Add instrument", [&] (Project& p)
        {
            auto& t = p.addTrack (TrackType::Midi, info.name, info.role);
            t.instrument.id = info.id;
            id = t.id;
        });
        session.select (id);
        session.status (info.name + " added. Play it with the on-screen keyboard in the Beat lab or a MIDI keyboard.");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto bar = r.removeFromTop (36);
        family.setBounds (bar.removeFromLeft (220).reduced (2));
        audition.setBounds (bar.removeFromLeft (110).reduced (2));
        add.setBounds (bar.removeFromLeft (140).reduced (2));
        note.setBounds (r.removeFromBottom (40));
        list.setBounds (r);
    }

    Session& session;
    juce::ListBox list;
    juce::ComboBox family;
    juce::TextButton add { "Add to song" }, audition { "Preview" };
    juce::Label note;
    std::vector<const InstrumentInfo*> items;
    std::vector<InstrumentInfo> packItems;
};

//==============================================================================
struct LibraryScreen::LoopList : public juce::Component, private juce::ListBoxModel
{
    explicit LoopList (Session& s) : session (s)
    {
        list.setModel (this);
        list.setRowHeight (34);
        addAndMakeVisible (list);
        text.setTextToShowWhenEmpty ("Search loops (shaker, piano, log drum...)", colours::textDim);
        text.onTextChange = [this] { search(); };
        addAndMakeVisible (text);
        style.addItem ("Any style", 1);
        style.onChange = [this] { search(); };
        addAndMakeVisible (style);
        matchTempo.setButtonText ("Near song tempo");
        matchTempo.onClick = [this] { search(); };
        addAndMakeVisible (matchTempo);
        audition.onClick = [this] { if (auto r = list.getSelectedRow(); r >= 0) preview (results[(size_t) r]); };
        add.onClick = [this] { addToSong(); };
        scan.onClick = [this] { scanFolder(); };
        starter.setTooltip ("Renders royalty-free loops from the beat generator for every style (owned content, safe for commercial release)");
        starter.onClick = [this] { makeStarter(); };
        for (auto* b : { &audition, &add, &scan, &starter }) addAndMakeVisible (b);
        styleCaption (info);
        addAndMakeVisible (info);
    }

    void refreshStyles()
    {
        const auto current = style.getText();
        style.clear (juce::dontSendNotification);
        style.addItem ("Any style", 1);
        for (auto& s : session.library().styles()) style.addItem (s, style.getNumItems() + 1);
        style.setText (current.isNotEmpty() ? current : juce::String ("Any style"), juce::dontSendNotification);
    }

    void search()
    {
        LoopQuery q;
        q.text = text.getText();
        if (style.getSelectedId() > 1) q.style = style.getText();
        if (matchTempo.getToggleState() && session.hasProject())
        {
            q.bpmMin = session.project().bpm * 0.85;
            q.bpmMax = session.project().bpm * 1.15;
        }
        if (session.hasProject()) q.key = session.project().key.toString();
        results = session.library().search (q);
        list.updateContent();
        list.repaint();
        info.setText (juce::String ((int) results.size()) + " of " + juce::String (session.library().count()) + " loops. Added loops are stretched to "
                      + juce::String (session.project().bpm, 0) + " BPM and moved to " + session.project().key.toString() + ".", juce::dontSendNotification);
    }

    int getNumRows() override { return (int) results.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) results.size()) return;
        if (selected) g.fillAll (colours::accent.withAlpha (0.3f));
        auto& l = results[(size_t) row];
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (l.name, 12, 0, w / 2, h, juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (l.style + "  " + l.instrument + "  " + (l.bpm > 0 ? juce::String (l.bpm, 0) + " BPM" : juce::String()) + "  " + l.key,
                    w / 2, 0, w / 2 - 12, h, juce::Justification::centredRight);
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { if (row >= 0) preview (results[(size_t) row]); }

    void preview (const LoopInfo& l)
    {
        juce::AudioBuffer<float> b;
        double rate = 0;
        if (ClipCache::readFile (l.file, b, rate)) previewFor (session).play (b, rate);
    }

    void addToSong()
    {
        const int r = list.getSelectedRow();
        if (r < 0 || ! session.hasProject()) return;
        auto loop = results[(size_t) r];
        const double at = std::floor (session.engine().getPositionBeats() / session.project().beatsPerBar()) * session.project().beatsPerBar();
        auto selected = session.selectedTrackId;
        auto* t = session.project().findTrack (selected);
        if (t == nullptr || t->type != TrackType::Audio) selected.clear();
        juce::Result res = juce::Result::ok();
        session.perform ("Add loop", [&] (Project& p) { res = library::addLoopToProject (p, session.getFolder(), loop, at, selected); });
        session.status (res.wasOk() ? "Loop added at the playhead, matched to the song." : res.getErrorMessage());
    }

    void scanFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a folder of loops to the library");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.isDirectory()) return;
            const int n = session.library().scanFolder (f, "user");
            refreshStyles();
            search();
            session.status (juce::String (n) + " loops indexed (tempo and key read from names or detected).");
        });
    }

    void makeStarter()
    {
        auto out = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Studio").getChildFile ("Starter loops");
        starter.setEnabled (false);
        info.setText ("Rendering starter loops for every style...", juce::dontSendNotification);
        juce::Thread::launch ([this, out]
        {
            library::generateStarterLoops (out);
            juce::MessageManager::callAsync ([this, out]
            {
                session.library().scanFolder (out, "starter");
                starter.setEnabled (true);
                refreshStyles();
                search();
            });
        });
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto bar = r.removeFromTop (36);
        text.setBounds (bar.removeFromLeft (260).reduced (2));
        style.setBounds (bar.removeFromLeft (180).reduced (2));
        matchTempo.setBounds (bar.removeFromLeft (150).reduced (2));
        audition.setBounds (bar.removeFromLeft (90).reduced (2));
        add.setBounds (bar.removeFromLeft (120).reduced (2));
        starter.setBounds (bar.removeFromRight (170).reduced (2));
        scan.setBounds (bar.removeFromRight (150).reduced (2));
        info.setBounds (r.removeFromBottom (26));
        list.setBounds (r);
    }

    Session& session;
    juce::ListBox list;
    juce::TextEditor text;
    juce::ComboBox style;
    juce::ToggleButton matchTempo;
    juce::TextButton audition { "Preview" }, add { "Add to song" }, scan { "Add loop folder..." }, starter { "Make starter loops" };
    juce::Label info;
    std::vector<LoopInfo> results;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
namespace
{
    struct PacksTab : public juce::Component, private juce::ListBoxModel
    {
        explicit PacksTab (Session& s) : session (s)
        {
            list.setModel (this);
            list.setRowHeight (44);
            addAndMakeVisible (list);
            install.setTooltip ("Install a pack from a .zip or a folder (e.g. from a USB drive) - no internet needed");
            install.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Choose a pack (.zip or folder)", juce::File(), "*.zip");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                                      [this] (const juce::FileChooser& fc)
                {
                    auto r = SamplePackManager::getInstance().installFromFile (fc.getResult());
                    session.status (r.wasOk() ? "Pack installed." : r.getErrorMessage());
                    reload();
                });
            };
            download.setTooltip ("Downloads resume after drops. Large packs are best downloaded on Wi-Fi.");
            download.onClick = [this]
            {
                auto* w = new juce::AlertWindow ("Download a pack", "Paste the pack's download link (from your purchase or the pack catalogue).", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("url", "https://");
                w->addTextEditor ("id", "", "Pack name");
                w->addButton ("Download", 1);
                w->addButton ("Cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
                {
                    if (r != 1) return;
                    auto id = juce::File::createLegalFileName (w->getTextEditorContents ("id").trim());
                    if (id.isEmpty()) id = "pack";
                    current = SamplePackManager::getInstance().startDownload (id, juce::URL (w->getTextEditorContents ("url")),
                        [this] (double p) { status.setText ("Downloading... " + juce::String ((int) (p * 100)) + "%", juce::dontSendNotification); },
                        [this] (juce::Result res) { status.setText (res.wasOk() ? "Pack installed." : res.getErrorMessage(), juce::dontSendNotification); reload(); });
                }), true);
            };
            moveFolder.setTooltip ("Keep packs on an external drive");
            moveFolder.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Choose where sound packs live");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (! f.isDirectory()) return;
                    SamplePackManager::getInstance().setPacksFolder (f);
                    session.settings().props().setValue ("packsFolder", f.getFullPathName());
                    reload();
                });
            };
            remove.onClick = [this]
            {
                const int r = list.getSelectedRow();
                if (r >= 0 && r < (int) packs.size()) { SamplePackManager::getInstance().remove (packs[(size_t) r].id); reload(); }
            };
            for (auto* b : { &install, &download, &moveFolder, &remove }) addAndMakeVisible (b);
            styleCaption (status);
            addAndMakeVisible (status);
            reload();
        }

        void reload()
        {
            packs = SamplePackManager::getInstance().scanInstalled();
            list.updateContent();
            list.repaint();
            status.setText ("Packs folder: " + SamplePackManager::getInstance().getPacksFolder().getFullPathName() + "   |   RAM used by sample heads: "
                            + juce::File::descriptionOfSizeInBytes (SamplePackManager::getInstance().preloadBytes()), juce::dontSendNotification);
        }

        int getNumRows() override { return (int) packs.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (row < 0 || row >= (int) packs.size()) return;
            if (selected) g.fillAll (colours::accent.withAlpha (0.3f));
            auto& p = packs[(size_t) row];
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (p.name + "  v" + p.version, 12, 2, w - 24, h / 2, juce::Justification::centredLeft);
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String (p.instrumentIds.size()) + " instruments, " + juce::File::descriptionOfSizeInBytes (p.sizeBytes) + "  -  " + p.description,
                        12, h / 2, w - 24, h / 2 - 2, juce::Justification::centredLeft);
        }

        void paint (juce::Graphics& g) override
        {
            if (packs.empty())
            {
                g.setColour (colours::textDim);
                g.drawFittedText ("No sound packs installed yet. The synthesised starter instruments are always available offline.\n\n"
                                  "Pack format: a folder with pack.json listing instruments and their samples (root note, key and velocity range, "
                                  "round-robin group). See docs/SOUND_PACKS.md.", getLocalBounds().reduced (40), juce::Justification::centred, 6);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            auto bar = r.removeFromTop (36);
            for (auto* b : { &install, &download, &moveFolder, &remove }) b->setBounds (bar.removeFromLeft (170).reduced (2));
            status.setBounds (r.removeFromBottom (26));
            list.setBounds (r);
            list.setVisible (! packs.empty());
        }

        Session& session;
        juce::ListBox list;
        juce::TextButton install { "Install from file..." }, download { "Download..." }, moveFolder { "Packs folder..." }, remove { "Remove" };
        juce::Label status;
        std::vector<SamplePackInfo> packs;
        std::shared_ptr<SamplePackManager::Download> current;
        std::unique_ptr<juce::FileChooser> chooser;
    };
}

LibraryScreen::LibraryScreen (Session& s) : Screen (s)
{
    instrumentsTab = std::make_unique<InstrumentList> (s);
    loopsTab = std::make_unique<LoopList> (s);
    packsTab = std::make_unique<PacksTab> (s);
    tabs.addTab ("Instruments", colours::panel, instrumentsTab.get(), false);
    tabs.addTab ("Loops", colours::panel, loopsTab.get(), false);
    tabs.addTab ("Sound packs", colours::panel, packsTab.get(), false);
    tabs.setTabBarDepth (36);
    addAndMakeVisible (tabs);
}

LibraryScreen::~LibraryScreen()
{
    tabs.clearTabs();
    previewPlayer.reset();
}

void LibraryScreen::refresh (bool)
{
    loopsTab->refreshStyles();
    loopsTab->search();
}

void LibraryScreen::search() { loopsTab->search(); }
void LibraryScreen::preview (const juce::File&) {}
void LibraryScreen::paint (juce::Graphics& g) { g.fillAll (colours::background); }
void LibraryScreen::resized() { tabs.setBounds (getLocalBounds().reduced (8)); }

} // namespace studio::ui
