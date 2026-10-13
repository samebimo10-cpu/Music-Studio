#include "ReleaseScreen.h"
#include "core/engine/OfflineRenderer.h"
#include "core/exporting/Exporter.h"
#include "core/lyrics/LyricsTools.h"
#include "core/release/Release.h"

namespace studio::ui
{

namespace
{
    struct Page : public juce::Component
    {
        explicit Page (Session& s) : session (s) {}
        virtual void load() = 0;
        void paint (juce::Graphics& g) override { g.fillAll (colours::background); }
        Session& session;
        void edit (const juce::String& what, std::function<void (ReleaseMetadata&)> fn)
        {
            session.perform (what, [fn] (Project& p) { fn (p.release); }, false);
        }
    };

    //==========================================================================
    struct DetailsPage : public Page
    {
        struct Field { juce::String label; juce::Label caption; juce::TextEditor editor; };

        explicit DetailsPage (Session& s) : Page (s)
        {
            auto add = [this] (const juce::String& label, const juce::String& hint)
            {
                auto* f = fields.add (new Field());
                f->label = label;
                f->caption.setText (label, juce::dontSendNotification);
                styleCaption (f->caption);
                f->editor.setTextToShowWhenEmpty (hint, colours::textDim);
                f->editor.onFocusLost = [this] { save(); };
                f->editor.onReturnKey = [this] { save(); };
                addAndMakeVisible (f->caption);
                addAndMakeVisible (f->editor);
            };
            add ("Song title", "Without \"feat.\" or \"official audio\"");
            add ("Primary artist", "Exactly as on your other releases");
            add ("Featured artists", "Comma separated");
            add ("Songwriters", "Full legal names, comma separated");
            add ("Composers", "Comma separated");
            add ("Producers", "Comma separated");
            add ("Subgenre", "e.g. Praise & Worship, Afro gospel");
            add ("Release date", "YYYY-MM-DD (2-4 weeks ahead is best)");
            add ("Original release date", "Only if released before");
            add ("ISRC", "Leave empty: the distributor assigns one");
            add ("UPC", "Leave empty: the distributor assigns one");
            add ("Label", "Optional");
            add ("Copyright line", "e.g. 2026 Your Name");

            for (auto* c : { &type, &genre, &language }) { addAndMakeVisible (c); c->onChange = [this] { save(); }; }
            type.addItemList ({ "single", "ep", "album" }, 1);
            genre.addItemList (release::genres(), 1);
            language.addItemList (release::languages(), 1);
            for (auto* l : { &typeL, &genreL, &languageL }) { styleCaption (*l); addAndMakeVisible (l); }
            typeL.setText ("Release type", juce::dontSendNotification);
            genreL.setText ("Genre", juce::dontSendNotification);
            languageL.setText ("Language", juce::dontSendNotification);
            explicitBox.setButtonText ("Explicit lyrics");
            explicitBox.onClick = [this] { save(); };
            addAndMakeVisible (explicitBox);
        }

        void load() override
        {
            const auto& m = session.project().release;
            const juce::String values[] { m.title, m.primaryArtist, m.featuredArtists.joinIntoString (", "), m.songwriters.joinIntoString (", "),
                                          m.composers.joinIntoString (", "), m.producers.joinIntoString (", "), m.subgenre, m.releaseDate,
                                          m.originalReleaseDate, m.isrc, m.upc, m.label, m.copyrightLine };
            for (int i = 0; i < fields.size(); ++i)
                if (! fields[i]->editor.hasKeyboardFocus (true)) fields[i]->editor.setText (values[i], false);
            type.setText (m.releaseType, juce::dontSendNotification);
            genre.setText (m.genre, juce::dontSendNotification);
            language.setText (m.language, juce::dontSendNotification);
            explicitBox.setToggleState (m.explicitContent, juce::dontSendNotification);
        }

        void save()
        {
            auto list = [] (const juce::String& s) { auto a = juce::StringArray::fromTokens (s, ",", ""); a.trim(); a.removeEmptyStrings(); return a; };
            juce::StringArray v;
            for (auto* f : fields) v.add (f->editor.getText());
            const auto t = type.getText(), g = genre.getText(), l = language.getText();
            const bool ex = explicitBox.getToggleState();
            edit ("Release details", [=] (ReleaseMetadata& m)
            {
                m.title = v[0]; m.primaryArtist = v[1]; m.featuredArtists = list (v[2]); m.songwriters = list (v[3]); m.composers = list (v[4]);
                m.producers = list (v[5]); m.subgenre = v[6]; m.releaseDate = v[7].trim(); m.originalReleaseDate = v[8].trim();
                m.isrc = v[9].trim(); m.upc = v[10].trim(); m.label = v[11]; m.copyrightLine = v[12];
                if (t.isNotEmpty()) m.releaseType = t;
                if (g.isNotEmpty()) m.genre = g;
                if (l.isNotEmpty()) m.language = l;
                m.explicitContent = ex;
            });
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16);
            auto col1 = r.removeFromLeft (r.getWidth() / 2).reduced (0, 0).withTrimmedRight (12);
            auto col2 = r;
            auto place = [] (juce::Rectangle<int>& col, juce::Label& l, juce::Component& c)
            {
                l.setBounds (col.removeFromTop (18));
                c.setBounds (col.removeFromTop (30));
                col.removeFromTop (8);
            };
            for (int i = 0; i < fields.size(); ++i) place (i < 7 ? col1 : col2, fields[i]->caption, fields[i]->editor);
            place (col2, typeL, type);
            place (col2, genreL, genre);
            place (col2, languageL, language);
            explicitBox.setBounds (col2.removeFromTop (30));
        }

        juce::OwnedArray<Field> fields;
        juce::ComboBox type, genre, language;
        juce::Label typeL, genreL, languageL;
        juce::ToggleButton explicitBox;
    };

    //==========================================================================
    struct SplitsPage : public Page, private juce::TableListBoxModel
    {
        explicit SplitsPage (Session& s) : Page (s)
        {
            table.setModel (this);
            table.getHeader().addColumn ("Name", 1, 260);
            table.getHeader().addColumn ("Role", 2, 180);
            table.getHeader().addColumn ("Share %", 3, 100);
            table.getHeader().addColumn ("Email (for payouts)", 4, 260);
            table.setRowHeight (30);
            addAndMakeVisible (table);
            addButton.onClick = [this] { edit ("Add split", [] (ReleaseMetadata& m) { m.splits.push_back ({ "", "songwriter", 0, "" }); }); };
            removeButton.onClick = [this]
            {
                const int r = table.getSelectedRow();
                if (r >= 0) edit ("Remove split", [r] (ReleaseMetadata& m) { if (r < (int) m.splits.size()) m.splits.erase (m.splits.begin() + r); });
            };
            evenButton.onClick = [this] { edit ("Even splits", [] (ReleaseMetadata& m) { for (auto& s : m.splits) s.percent = 100.0 / juce::jmax (1, (int) m.splits.size()); }); };
            for (auto* b : { &addButton, &removeButton, &evenButton }) addAndMakeVisible (b);
            styleCaption (total);
            addAndMakeVisible (total);
        }

        void load() override
        {
            table.updateContent();
            table.repaint();
            double sum = 0;
            for (auto& s : session.project().release.splits) sum += s.percent;
            total.setText (juce::String::formatted ("Total: %.2f%%  ", sum) + (std::abs (sum - 100) < 0.01 ? "(good)" : "- must be 100%")
                           + "   Shares go to the distributor where it supports split payouts.", juce::dontSendNotification);
        }

        int getNumRows() override { return (int) session.project().release.splits.size(); }
        void paintRowBackground (juce::Graphics& g, int, int, int, bool selected) override { g.fillAll (selected ? colours::accent.withAlpha (0.25f) : colours::panel); }
        void paintCell (juce::Graphics&, int, int, int, int, bool) override {}

        juce::Component* refreshComponentForCell (int row, int col, bool, juce::Component* existing) override
        {
            auto* ed = dynamic_cast<juce::TextEditor*> (existing);
            if (ed == nullptr) { delete existing; ed = new juce::TextEditor(); }
            const auto& splits = session.project().release.splits;
            if (row >= (int) splits.size()) return ed;
            const auto& s = splits[(size_t) row];
            ed->setText (col == 1 ? s.name : col == 2 ? s.role : col == 3 ? juce::String (s.percent, 2) : s.email, false);
            ed->onFocusLost = ed->onReturnKey = [this, row, col, ed]
            {
                const auto text = ed->getText();
                edit ("Edit split", [=] (ReleaseMetadata& m)
                {
                    if (row >= (int) m.splits.size()) return;
                    auto& sp = m.splits[(size_t) row];
                    if (col == 1) sp.name = text; else if (col == 2) sp.role = text; else if (col == 3) sp.percent = text.getDoubleValue(); else sp.email = text;
                });
            };
            return ed;
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16);
            auto bar = r.removeFromTop (36);
            addButton.setBounds (bar.removeFromLeft (120).reduced (2));
            removeButton.setBounds (bar.removeFromLeft (120).reduced (2));
            evenButton.setBounds (bar.removeFromLeft (120).reduced (2));
            total.setBounds (r.removeFromBottom (24));
            table.setBounds (r);
        }

        juce::TableListBox table;
        juce::TextButton addButton { "Add person" }, removeButton { "Remove" }, evenButton { "Split evenly" };
        juce::Label total;
    };

    //==========================================================================
    struct LyricsSyncPage : public Page, private juce::Timer
    {
        explicit LyricsSyncPage (Session& s) : Page (s)
        {
            editor.setMultiLine (true);
            editor.setReturnKeyStartsNewLine (true);
            editor.setFont (juce::FontOptions (16.0f));
            editor.onFocusLost = [this] { const auto t = editor.getText(); edit ("Lyrics", [t] (ReleaseMetadata& m) { m.lyrics = t; }); };
            addAndMakeVisible (editor);
            fromPad.onClick = [this] { session.perform ("Lyrics from pad", [] (Project& p) { p.release.lyrics = lyrics::plainLyrics (p.lyricSheet); }, false); };
            tap.setColour (juce::TextButton::buttonColourId, colours::accent);
            tap.setTooltip ("Play the song and press this (or Space while focused) at the start of each line");
            tap.onClick = [this] { tapLine(); };
            startSync.onClick = [this]
            {
                lines = juce::StringArray::fromLines (session.project().release.lyrics);
                lines.removeEmptyStrings();
                synced.clear();
                next = 0;
                session.engine().setPositionBeats (0);
                session.engine().play();
                status.setText ("Press Tap at the start of: \"" + lines[0] + "\"", juce::dontSendNotification);
            };
            rough.onClick = [this]
            {
                session.perform ("Rough lyric sync", [] (Project& p) { p.release.syncedLyrics = lyrics::roughSync (p); }, false);
                status.setText ("Lines spread across the matching song sections. Fine-tune with Tap sync.", juce::dontSendNotification);
            };
            for (auto* b : { &fromPad, &startSync, &tap, &rough }) addAndMakeVisible (b);
            styleCaption (status);
            addAndMakeVisible (status);
            preview.setMultiLine (true);
            preview.setReadOnly (true);
            preview.setFont (juce::FontOptions (13.0f));
            addAndMakeVisible (preview);
            startTimerHz (4);
        }

        void tapLine()
        {
            if (next >= lines.size()) return;
            synced.push_back ({ session.engine().getPositionSeconds(), lines[next] });
            ++next;
            if (next >= lines.size())
            {
                auto s = synced;
                edit ("Sync lyrics", [s] (ReleaseMetadata& m) { m.syncedLyrics = s; });
                session.engine().stop();
                status.setText ("All lines synced.", juce::dontSendNotification);
            }
            else status.setText ("Next: \"" + lines[next] + "\"", juce::dontSendNotification);
        }

        void timerCallback() override
        {
            // Highlight the current synced line in the preview while playing.
            const auto& sl = session.project().release.syncedLyrics;
            const double t = session.engine().getPositionSeconds();
            juce::String text;
            for (auto& l : sl)
                text << (t >= l.timeSeconds ? "> " : "  ") << formatTime (l.timeSeconds) << "  " << l.text << "\n";
            if (text != preview.getText()) preview.setText (text, false);
        }

        void load() override
        {
            if (! editor.hasKeyboardFocus (true)) editor.setText (session.project().release.lyrics, false);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16);
            auto bar = r.removeFromTop (36);
            fromPad.setBounds (bar.removeFromLeft (170).reduced (2));
            rough.setBounds (bar.removeFromLeft (150).reduced (2));
            startSync.setBounds (bar.removeFromLeft (150).reduced (2));
            tap.setBounds (bar.removeFromLeft (120).reduced (2));
            status.setBounds (r.removeFromTop (24));
            preview.setBounds (r.removeFromRight (r.getWidth() / 2).withTrimmedLeft (8));
            editor.setBounds (r);
        }

        juce::TextEditor editor, preview;
        juce::TextButton fromPad { "Copy from Lyrics pad" }, startSync { "Start tap sync" }, tap { "Tap" }, rough { "Rough sync" };
        juce::Label status;
        juce::StringArray lines;
        std::vector<SyncedLyricLine> synced;
        int next = 0;
    };

    //==========================================================================
    struct ArtworkPage : public Page
    {
        explicit ArtworkPage (Session& s) : Page (s)
        {
            choose.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Choose cover art (3000x3000 JPG or PNG)", juce::File(), "*.jpg;*.jpeg;*.png");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (! f.existsAsFile()) return;
                    auto dest = store::releaseFolder (session.getFolder()).getChildFile ("cover" + f.getFileExtension().toLowerCase());
                    f.copyFileTo (dest);
                    const auto rel = store::relativePath (session.getFolder(), dest);
                    edit ("Cover art", [rel] (ReleaseMetadata& m) { m.artworkFile = rel; });
                });
            };
            make.setTooltip ("Make a 3000x3000 cover from a template");
            make.onClick = [this]
            {
                const auto& m = session.project().release;
                static const juce::Colour palette[] { juce::Colour (0xff3355aa), juce::Colour (0xff8a3b2b), juce::Colour (0xff2b7a6b), juce::Colour (0xff5a2b8a) };
                auto img = release::makeArtwork (templateBox.getText(), m.title.isNotEmpty() ? m.title : session.project().name, m.primaryArtist,
                                                  palette[(int) colourIndex++ % 4], photo);
                auto dest = store::releaseFolder (session.getFolder()).getChildFile ("cover.jpg");
                release::saveJpeg (img, dest);
                const auto rel = store::relativePath (session.getFolder(), dest);
                edit ("Make cover art", [rel] (ReleaseMetadata& mm) { mm.artworkFile = rel; });
                load();
            };
            photoButton.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Choose a photo for the Photo template", juce::File(), "*.jpg;*.jpeg;*.png");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                {
                    photo = juce::ImageFileFormat::loadFrom (fc.getResult());
                    templateBox.setText ("Photo");
                });
            };
            templateBox.addItemList (release::artTemplates(), 1);
            templateBox.setSelectedId (1);
            for (auto* c : { (juce::Component*) &choose, (juce::Component*) &make, (juce::Component*) &templateBox, (juce::Component*) &photoButton,
                             (juce::Component*) &checks })
                addAndMakeVisible (c);
        }

        void load() override
        {
            const auto& m = session.project().release;
            auto file = m.artworkFile.isNotEmpty() ? store::resolve (session.getFolder(), m.artworkFile) : juce::File();
            image = file.existsAsFile() ? juce::ImageFileFormat::loadFrom (file) : juce::Image();
            std::vector<IssueList::Item> items;
            auto rep = release::checkArtwork (file, m);
            for (auto& c : rep.checks)
                items.push_back ({ c.level == release::Check::Level::Pass ? 0 : c.level == release::Check::Level::Warning ? 1 : 2, c.area, c.message, nullptr, {} });
            checks.setItems (items);
            resized();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            Page::paint (g);
            auto area = getLocalBounds().reduced (16).withTrimmedTop (50).removeFromLeft (juce::jmin (420, getWidth() / 2));
            area = area.withHeight (area.getWidth());
            paintPanel (g, area.toFloat());
            if (image.isValid()) g.drawImage (image, area.reduced (6).toFloat(), juce::RectanglePlacement::centred);
            else
            {
                g.setColour (colours::textDim);
                g.drawText ("No cover art yet", area, juce::Justification::centred);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16);
            auto bar = r.removeFromTop (36);
            choose.setBounds (bar.removeFromLeft (170).reduced (2));
            bar.removeFromLeft (16);
            templateBox.setBounds (bar.removeFromLeft (130).reduced (2));
            photoButton.setBounds (bar.removeFromLeft (110).reduced (2));
            make.setBounds (bar.removeFromLeft (150).reduced (2));
            r.removeFromTop (14);
            r.removeFromLeft (juce::jmin (420, getWidth() / 2) + 16);
            checks.setBounds (r.withHeight (checks.preferredHeight()));
        }

        juce::TextButton choose { "Choose image..." }, make { "Make cover" }, photoButton { "Photo..." };
        juce::ComboBox templateBox;
        IssueList checks;
        juce::Image image, photo;
        unsigned colourIndex = 0;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    //==========================================================================
    struct ExportPage : public Page
    {
        explicit ExportPage (Session& s) : Page (s)
        {
            auto box = [this] (juce::ToggleButton& b, const juce::String& t, bool on) { b.setButtonText (t); b.setToggleState (on, juce::dontSendNotification); addAndMakeVisible (b); };
            box (wav, "Master WAV 24-bit (for the distributor)", true);
            box (flac, "FLAC (lossless archive)", true);
            box (mp3, "MP3 320 kbps (WhatsApp, demos)", true);
            box (aac, "AAC", false);
            box (instrumental, "Instrumental", true);
            box (acappella, "A cappella", true);
            box (stems, "Stems (one WAV per bus)", false);
            box (performance, "Performance track (click + cues)", false);
            box (midiBox, "MIDI", true);
            box (video, "Lyric video (MP4)", false);
            rate.addItemList ({ "44.1 kHz", "48 kHz" }, 1);
            rate.setSelectedId (1);
            addAndMakeVisible (rate);
            check.onClick = [this] { runChecks(); };
            exportAll.setColour (juce::TextButton::buttonColourId, colours::accent);
            exportAll.onClick = [this] { runExport (false); };
            package.setTooltip ("Option 1 distribution: master WAV, artwork and a metadata sheet laid out for manual upload to DistroKid, TuneCore, Amuse, CD Baby...");
            package.onClick = [this] { runExport (true); };
            reveal.onClick = [this] { store::exportsFolder (session.getFolder()).createDirectory(); store::exportsFolder (session.getFolder()).startAsProcess(); };
            for (auto* b : { &check, &exportAll, &package, &reveal }) addAndMakeVisible (b);
            styleCaption (progressLabel);
            addAndMakeVisible (progressLabel);
            view.setViewedComponent (&list, false);
            addAndMakeVisible (view);
            auto tools = juce::String ("Encoders: ") + (exporting::findTool ("ffmpeg").existsAsFile() ? "ffmpeg found" : "ffmpeg not found")
                       + (exporting::findTool ("lame").existsAsFile() ? ", lame found" : "")
                       + ". MP3, AAC and lyric video need ffmpeg (or lame for MP3) installed or placed next to the app.";
            toolInfo.setText (tools, juce::dontSendNotification);
            styleCaption (toolInfo);
            addAndMakeVisible (toolInfo);
        }

        void load() override {}

        void showChecks (const std::vector<release::Check>& checks, const juce::StringArray& extra = {})
        {
            std::vector<IssueList::Item> items;
            for (auto& e : extra) items.push_back ({ 0, "Exported", e, nullptr, {} });
            for (auto& c : checks)
                items.push_back ({ c.level == release::Check::Level::Pass ? 0 : c.level == release::Check::Level::Warning ? 1 : 2, c.area, c.message, nullptr, {} });
            list.setItems (items);
            list.setSize (view.getWidth() - 12, list.preferredHeight());
        }

        void runChecks()
        {
            progressLabel.setText ("Rendering the master and checking everything...", juce::dontSendNotification);
            auto snapshot = session.project();
            auto folder = session.getFolder();
            pool.addJob ([this, snapshot, folder]
            {
                RenderRequest req;
                req.sampleRate = 44100;
                req.projectFolder = folder;
                auto master = OfflineRenderer::render (snapshot, req);
                auto checks = release::preReleaseChecklist (snapshot, folder, master, req.sampleRate);
                juce::MessageManager::callAsync ([this, checks]
                {
                    showChecks (checks);
                    progressLabel.setText (release::hasErrors (checks) ? "Fix the items marked x before releasing." : "Ready to release.", juce::dontSendNotification);
                });
            });
        }

        void runExport (bool asPackage)
        {
            auto snapshot = session.project();
            auto folder = session.getFolder();
            exporting::Options o;
            o.masterWav = wav.getToggleState(); o.flac = flac.getToggleState(); o.mp3 = mp3.getToggleState(); o.aac = aac.getToggleState();
            o.instrumental = instrumental.getToggleState(); o.acappella = acappella.getToggleState(); o.stems = stems.getToggleState();
            o.performanceTrack = performance.getToggleState(); o.midi = midiBox.getToggleState(); o.lyricVideo = video.getToggleState();
            o.sampleRate = rate.getSelectedId() == 2 ? 48000 : 44100;
            exportAll.setEnabled (false);
            package.setEnabled (false);
            pool.addJob ([this, snapshot, folder, o, asPackage]
            {
                auto progress = [this] (float f, const juce::String& label)
                {
                    juce::MessageManager::callAsync ([this, f, label] { progressLabel.setText (juce::String ((int) (f * 100)) + "%  " + label, juce::dontSendNotification); });
                    return true;
                };
                juce::StringArray files;
                std::vector<release::Check> checks;
                if (asPackage)
                {
                    auto r = release::writeExportPackage (snapshot, folder, store::exportsFolder (folder), progress);
                    files.add (r.folder.getFullPathName());
                    files.add (r.zip.getFullPathName());
                    checks = r.checks;
                    for (auto& e : r.errors) checks.push_back ({ release::Check::Level::Error, "Export", e });
                }
                else
                {
                    auto r = exporting::exportAll (snapshot, folder, store::exportsFolder (folder), o, progress);
                    for (auto& f : r.files) files.add (f.getFileName());
                    for (auto& w : r.warnings) checks.push_back ({ release::Check::Level::Warning, "Export", w });
                    for (auto& e : r.errors) checks.push_back ({ release::Check::Level::Error, "Export", e });
                    checks.push_back ({ release::Check::Level::Pass, "Loudness",
                                        juce::String::formatted ("Master: %.1f LUFS, %.2f dBTP", (double) r.masterLoudness.integratedLufs, (double) r.masterLoudness.truePeakDb) });
                }
                juce::MessageManager::callAsync ([this, files, checks, asPackage]
                {
                    exportAll.setEnabled (true);
                    package.setEnabled (true);
                    showChecks (checks, files);
                    progressLabel.setText (asPackage ? "Upload package ready (folder and zip in release/exports)." : "Export finished (release/exports).",
                                           juce::dontSendNotification);
                });
            });
        }

        ~ExportPage() override { pool.removeAllJobs (true, 60000); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16);
            auto left = r.removeFromLeft (360);
            for (auto* b : { &wav, &flac, &mp3, &aac, &instrumental, &acappella, &stems, &performance, &midiBox, &video })
                b->setBounds (left.removeFromTop (28));
            rate.setBounds (left.removeFromTop (32).withWidth (140).reduced (0, 2));
            left.removeFromTop (8);
            toolInfo.setBounds (left.removeFromTop (60));
            r.removeFromLeft (16);
            auto bar = r.removeFromTop (40);
            check.setBounds (bar.removeFromLeft (170).reduced (2));
            exportAll.setBounds (bar.removeFromLeft (150).reduced (2));
            package.setBounds (bar.removeFromLeft (200).reduced (2));
            reveal.setBounds (bar.removeFromLeft (150).reduced (2));
            progressLabel.setBounds (r.removeFromTop (26));
            view.setBounds (r);
            list.setSize (r.getWidth() - 12, list.preferredHeight());
        }

        juce::ToggleButton wav, flac, mp3, aac, instrumental, acappella, stems, performance, midiBox, video;
        juce::ComboBox rate;
        juce::TextButton check { "Run checklist" }, exportAll { "Export files" }, package { "Export upload package" }, reveal { "Show exports" };
        juce::Label progressLabel, toolInfo;
        IssueList list;
        juce::Viewport view;
        juce::ThreadPool pool { 1 };
    };

    //==========================================================================
    struct AfterReleasePage : public Page
    {
        explicit AfterReleasePage (Session& s) : Page (s)
        {
            links.setMultiLine (true);
            links.setReturnKeyStartsNewLine (true);
            links.setTextToShowWhenEmpty ("One per line:  Store | link\nSpotify | https://open.spotify.com/...\nBoomplay | https://...", colours::textDim);
            addAndMakeVisible (links);
            pageButton.onClick = [this] { makePage (false); };
            preSaveButton.onClick = [this] { makePage (true); };
            importCsv.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Distributor earnings report (CSV)", juce::File(), "*.csv;*.tsv;*.txt");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                {
                    auto rows = release::parseEarningsCsv (fc.getResult());
                    std::map<juce::String, std::pair<juce::int64, double>> byStore;
                    juce::int64 streams = 0; double money = 0;
                    for (auto& r : rows) { byStore[r.store].first += r.streams; byStore[r.store].second += r.earnings; streams += r.streams; money += r.earnings; }
                    juce::String t;
                    t << "Total: " << streams << " streams, " << juce::String (money, 2) << " earned\n\n";
                    for (auto& [store, v] : byStore) t << store << ": " << v.first << " streams, " << juce::String (v.second, 2) << "\n";
                    earnings.setText (t, false);
                });
            };
            for (auto* b : { &pageButton, &preSaveButton, &importCsv }) addAndMakeVisible (b);
            earnings.setMultiLine (true);
            earnings.setReadOnly (true);
            addAndMakeVisible (earnings);
            reminders.setMultiLine (true);
            reminders.setReadOnly (true);
            addAndMakeVisible (reminders);
            styleCaption (partnerNote);
            partnerNote.setText ("In-app submission to stores needs a distribution partner agreement (build phase 4). Until then, use the upload package "
                                 "with your distributor, then paste the store links here.", juce::dontSendNotification);
            addAndMakeVisible (partnerNote);
        }

        void makePage (bool preSave)
        {
            std::vector<release::StoreLink> l;
            for (auto& line : juce::StringArray::fromLines (links.getText()))
                if (line.contains ("|")) l.push_back ({ line.upToFirstOccurrenceOf ("|", false, false).trim(), line.fromFirstOccurrenceOf ("|", false, false).trim() });
            auto out = store::releaseFolder (session.getFolder()).getChildFile (preSave ? "presave-page" : "smart-link-page");
            auto r = release::writeSmartLinkPage (session.project(), session.getFolder(), l, out, preSave);
            if (r.wasOk()) { session.status ("Page written to " + out.getFullPathName() + " - upload the folder to any web host."); out.getChildFile ("index.html").startAsProcess(); }
        }

        void load() override
        {
            juce::String t;
            for (auto& r : release::afterReleaseReminders (session.project())) t << "- " << r << "\n\n";
            reminders.setText (t, false);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16);
            partnerNote.setBounds (r.removeFromTop (40));
            auto left = r.removeFromLeft (r.getWidth() / 2).withTrimmedRight (10);
            auto bar = left.removeFromTop (36);
            pageButton.setBounds (bar.removeFromLeft (170).reduced (2));
            preSaveButton.setBounds (bar.removeFromLeft (170).reduced (2));
            links.setBounds (left.removeFromTop (left.getHeight() / 2));
            left.removeFromTop (8);
            importCsv.setBounds (left.removeFromTop (34).withWidth (220).reduced (2));
            earnings.setBounds (left);
            reminders.setBounds (r);
        }

        juce::TextEditor links, earnings, reminders;
        juce::TextButton pageButton { "Make smart-link page" }, preSaveButton { "Make pre-save page" }, importCsv { "Import earnings report..." };
        juce::Label partnerNote;
        std::unique_ptr<juce::FileChooser> chooser;
    };
}

//==============================================================================
ReleaseScreen::ReleaseScreen (Session& s) : Screen (s)
{
    auto add = [this] (const juce::String& name, juce::Component* c) { pages.add (c); tabs.addTab (name, colours::panel, c, false); };
    add ("Details", new DetailsPage (s));
    add ("Splits", new SplitsPage (s));
    add ("Lyrics & sync", new LyricsSyncPage (s));
    add ("Artwork", new ArtworkPage (s));
    add ("Checklist & export", new ExportPage (s));
    add ("After release", new AfterReleasePage (s));
    tabs.setTabBarDepth (36);
    addAndMakeVisible (tabs);
}

ReleaseScreen::~ReleaseScreen()
{
    tabs.clearTabs();
}

void ReleaseScreen::refresh (bool)
{
    for (auto* p : pages)
        if (auto* page = dynamic_cast<Page*> (p)) page->load();
}

void ReleaseScreen::paint (juce::Graphics& g) { g.fillAll (colours::background); }
void ReleaseScreen::resized() { tabs.setBounds (getLocalBounds().reduced (8)); }

} // namespace studio::ui
