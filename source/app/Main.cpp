#include "MainComponent.h"
#include "core/beatgen/BeatGenerator.h"
#include "core/instruments/SamplePack.h"

namespace studio
{

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (Session& s) : juce::DocumentWindow ("Studio", ui::colours::background, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        content = new ui::MainComponent (s);
        setContentOwned (content, false);
        setResizable (true, true);
        setResizeLimits (1100, 700, 8000, 6000);
        centreWithSize (1440, 900);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        content->askToSaveThen ([] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
    }

    ui::MainComponent* content = nullptr;
};

class StudioApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Studio"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        session = std::make_unique<Session>();
        juce::Desktop::getInstance().setGlobalScaleFactor (session->settings().uiScale());

        auto packs = session->settings().props().getValue ("packsFolder");
        if (packs.isNotEmpty()) SamplePackManager::getInstance().setPacksFolder (juce::File (packs));

        window = std::make_unique<MainWindow> (*session);
        tooltips = std::make_unique<juce::TooltipWindow> (nullptr, 600);

        auto args = juce::StringArray::fromTokens (commandLine, true);
        args.trim();
        args.removeEmptyStrings();
        for (auto& a : args) a = a.unquoted();

        // studio --screenshot <dir>: renders every screen with a demo song (used for docs and UI checks).
        if (const int i = args.indexOf ("--screenshot"); i >= 0)
        {
            screenshots (juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1]));
            return;
        }
        // studio <project folder>: open it.
        if (args.size() == 1 && juce::File::isAbsolutePath (args[0]) && juce::File (args[0]).isDirectory())
            session->open (juce::File (args[0]));
    }

    void screenshots (const juce::File& dir)
    {
        dir.createDirectory();
        auto p = templates::create ("Worship single");
        p.name = "Grace Overflow";
        p.release.title = "Grace Overflow";
        p.release.primaryArtist = "Ada Okafor";
        p.release.songwriters.add ("Ada Okafor");
        p.release.releaseDate = "2026-12-04";
        p.release.lyrics = "Your grace is like a river\nRunning through the land\nI lift my hands to praise You\nHolding to Your hand";
        p.lyricSheet = { { "Verse 1", { { "Your grace is like a river", "D   A/C#", {} }, { "Running through the land", "Bm   G", {} } } },
                         { "Chorus", { { "I lift my hands to praise You", "G   D", {} }, { "Holding to Your hand", "A", {} } } } };
        beatgen::Settings bs;
        bs.style = "nigerian_praise";
        bs.seed = 7;
        bs.lengthSeconds = 150;
        beatgen::BeatGenerator::generate (p, bs);
        auto tmp = juce::File::createTempFile ("studio_screens");
        tmp.createDirectory();
        session->settings().setProjectsFolder (tmp);
        session->newProject (p);

        window->setSize (1440, 900);
        shotIds = window->content->screenIds();
        shotDir = dir;
        shotTemp = tmp;
        juce::Timer::callAfterDelay (600, [this] { nextShot (0); });
    }

    /** One screen per step: show it, let it paint, snapshot, move on (no nested message loops). */
    void nextShot (int index)
    {
        auto* main = window->content;
        if (index >= shotIds.size())
        {
            shotTemp.deleteRecursively();
            quit();
            return;
        }
        main->show (shotIds[index]);
        if (auto* s = main->screen (shotIds[index])) s->refresh (true);
        juce::Timer::callAfterDelay (350, [this, index, main]
        {
            auto img = main->createComponentSnapshot (main->getLocalBounds(), true, 1.0f);
            auto file = shotDir.getChildFile (shotIds[index] + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (img, out);
            nextShot (index + 1);
        });
    }

    void shutdown() override
    {
        window = nullptr;
        tooltips = nullptr;
        session = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    ui::StudioLookAndFeel lookAndFeel;
    std::unique_ptr<Session> session;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    juce::StringArray shotIds;
    juce::File shotDir, shotTemp;
};

} // namespace studio

START_JUCE_APPLICATION (studio::StudioApplication)
