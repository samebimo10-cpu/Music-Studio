// studio-cli: headless access to the Studio engine (batch renders, CI, testing).

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "core/analysis/AudioAnalysis.h"
#include "core/analysis/LoudnessMeter.h"
#include "core/arrange/Arrangement.h"
#include "core/beatgen/BeatGenerator.h"
#include "core/engine/ClipCache.h"
#include "core/engine/OfflineRenderer.h"
#include "core/exporting/Exporter.h"
#include "core/library/LibraryIndex.h"
#include "core/mastering/MasteringChain.h"
#include "core/mixing/MixTools.h"
#include "core/project/ProjectStore.h"
#include "core/release/Release.h"
#include "core/vocal/VocalChain.h"
#include "core/vocal/VocalTools.h"

using namespace studio;

namespace
{
    struct Args
    {
        juce::StringArray positional;
        std::map<juce::String, juce::String> options;

        Args (int argc, char** argv)
        {
            for (int i = 2; i < argc; ++i)
            {
                juce::String a (argv[i]);
                if (a.startsWith ("--"))
                {
                    auto key = a.substring (2);
                    if (key.contains ("=")) { options[key.upToFirstOccurrenceOf ("=", false, false)] = key.fromFirstOccurrenceOf ("=", false, false); continue; }
                    if (i + 1 < argc && ! juce::String (argv[i + 1]).startsWith ("--")) options[key] = argv[++i];
                    else options[key] = "1";
                }
                else positional.add (a);
            }
        }

        juce::String get (const juce::String& k, const juce::String& def = {}) const
        {
            auto it = options.find (k);
            return it != options.end() ? it->second : def;
        }
        bool has (const juce::String& k) const { return options.count (k) > 0; }
        juce::File file (int i) const { return juce::File::getCurrentWorkingDirectory().getChildFile (positional[i]); }
    };

    void print (const juce::String& s) { std::cout << s << std::endl; }
    int fail (const juce::String& s) { std::cerr << "error: " << s << std::endl; return 1; }

    void printLoudness (const LoudnessMeter::Summary& s)
    {
        print (juce::String::formatted ("Integrated: %.2f LUFS", (double) s.integratedLufs));
        print (juce::String::formatted ("True peak:  %.2f dBTP", (double) s.truePeakDb));
        print (juce::String::formatted ("Sample pk:  %.2f dBFS", (double) s.samplePeakDb));
        print (juce::String::formatted ("LRA:        %.1f LU", (double) s.loudnessRange));
        print (juce::String::formatted ("Max short:  %.2f LUFS", (double) s.maxShortTermLufs));
        for (auto& t : platformTargets())
            print (juce::String::formatted ("  %-14s %+.1f dB", t.name.toRawUTF8(), (double) platformAdjustmentDb (t, s.integratedLufs, s.truePeakDb)));
    }

    bool readAudio (const juce::File& f, juce::AudioBuffer<float>& b, double& sr)
    {
        if (! ClipCache::readFile (f, b, sr)) { std::cerr << "error: cannot read " << f.getFullPathName() << std::endl; return false; }
        return true;
    }

    juce::Result loadOrCreate (const juce::File& folder, Project& p)
    {
        if (store::projectFile (folder).existsAsFile())
            return store::load (folder, p);
        p = templates::blank (folder.getFileNameWithoutExtension());
        folder.createDirectory();
        for (auto f : { store::audioFolder (folder), store::takesFolder (folder), store::releaseFolder (folder), store::historyFolder (folder) })
            f.createDirectory();
        return store::save (p, folder);
    }

    void usage()
    {
        print ("studio-cli <command> [args]\n"
               "\n"
               "Projects\n"
               "  new <folder> [--template NAME] [--name SONG]     create a project (templates: Worship single, Praise medley, Podcast, Choir rehearsal track)\n"
               "  beat <folder> [--style ID] [--bpm N] [--key K] [--mood M] [--length SEC] [--seed N]\n"
               "                                                  generate drums, bass, keys, guitar, percussion and pad as MIDI\n"
               "  styles                                           list beat generator styles\n"
               "  info <folder>                                    show tracks, sections and chords\n"
               "  import <folder> <audio> [--at BEAT]              import audio (e.g. a beat) onto a new track\n"
               "  render <folder> <out.wav> [--no-master] [--rate 44100]\n"
               "  automix <folder>                                 analyse tracks and set levels, pans, EQ and sends\n"
               "  assist <folder>                                  mix assistant: problems in plain words\n"
               "  arrange-check <folder>                           arrangement assistant\n"
               "  vamp <folder> [--repeats 3] [--lift]             build a vamp from the first chorus\n"
               "  export <folder> [--out DIR] [--stems] [--performance] [--video] [--rate 48000]\n"
               "  check <folder>                                   pre-release checklist\n"
               "  package <folder> [--out DIR]                      export package for manual distributor upload\n"
               "\n"
               "Audio\n"
               "  loudness <file>                                  LUFS, true peak, platform adjustments\n"
               "  analyse <file>                                   key, tempo, chords, tone\n"
               "  master <in> <out> [--preset Warm] [--target -14] [--ceiling -1] [--ai] [--style ID]\n"
               "  vocal <in> <out> [--preset \"Worship lead\"] [--amount 0.7] [--key \"C major\"]\n"
               "  room <silence.wav>                               room check\n"
               "  harmony <in> <outdir> [--key K] [--parts 3]\n"
               "  separate <in> <outdir>                           vocals, drums, bass, other\n"
               "  compare <mix> <reference>                        reference track comparison\n"
               "  starter-loops <outdir>                           render royalty-free starter loops\n");
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for fonts, images and plugins

    if (argc < 2) { usage(); return 0; }
    const juce::String cmd (argv[1]);
    Args args (argc, argv);

    if (cmd == "help" || cmd == "--help" || cmd == "-h") { usage(); return 0; }

    if (cmd == "styles")
    {
        for (auto& s : beatgen::styles())
            print (juce::String::formatted ("%-22s %-42s %3.0f-%3.0f BPM", s.id.toRawUTF8(), s.name.toRawUTF8(), s.bpmMin, s.bpmMax));
        return 0;
    }

    if (cmd == "new")
    {
        if (args.positional.isEmpty()) return fail ("new <folder>");
        auto parent = args.file (0).getParentDirectory();
        auto p = args.has ("template") ? templates::create (args.get ("template")) : templates::blank();
        p.name = args.get ("name", args.file (0).getFileNameWithoutExtension());
        p.release.title = p.name;
        juce::File folder;
        auto r = store::create (parent, p, folder);
        if (r.failed()) return fail (r.getErrorMessage());
        print ("Created " + folder.getFullPathName());
        return 0;
    }

    if (cmd == "beat")
    {
        if (args.positional.isEmpty()) return fail ("beat <folder>");
        auto folder = args.file (0);
        Project p;
        auto r = loadOrCreate (folder, p);
        if (r.failed()) return fail (r.getErrorMessage());
        beatgen::Settings s;
        s.style = args.get ("style", p.style.isNotEmpty() ? p.style : juce::String ("afrobeats"));
        s.bpm = args.get ("bpm", "0").getDoubleValue();
        s.key = args.get ("key");
        s.mood = args.get ("mood", "joyful");
        s.lengthSeconds = args.get ("length", "180").getDoubleValue();
        s.seed = args.get ("seed", "0").getLargeIntValue();
        beatgen::BeatGenerator::generate (p, s);
        r = store::save (p, folder);
        if (r.failed()) return fail (r.getErrorMessage());
        print (juce::String::formatted ("Generated %s at %.0f BPM in %s: %d sections, %d chords",
                                        s.style.toRawUTF8(), p.bpm, p.key.toString().toRawUTF8(), (int) p.sections.size(), (int) p.chords.size()));
        for (auto& t : p.tracks)
            if (t.generator.part.isNotEmpty())
            {
                size_t notes = 0;
                for (auto& c : t.clips) notes += c.notes.size();
                print ("  " + t.name + ": " + juce::String ((int) t.clips.size()) + " clips, " + juce::String ((int) notes) + " notes");
            }
        return 0;
    }

    if (cmd == "info")
    {
        Project p;
        auto r = store::load (args.file (0), p);
        if (r.failed()) return fail (r.getErrorMessage());
        print (p.name + "  |  " + juce::String (p.bpm, 1) + " BPM  |  " + p.key.toString() + "  |  style: " + p.style + "  |  mood: " + p.mood);
        print ("Sections:");
        for (auto& s : p.sections)
            print (juce::String::formatted ("  %-14s beat %6.1f  %5.1f beats  energy %.2f", arrange::sectionLabel (p, s).toRawUTF8(), s.startBeat, s.lengthBeats, (double) s.energy));
        print ("Tracks:");
        for (auto& t : p.tracks)
            print (juce::String::formatted ("  [%-5s] %-34s vol %+5.1f pan %+4.2f  %s  clips %d  fx %d", toString (t.type).toRawUTF8(), t.name.toRawUTF8(),
                                            (double) t.volumeDb, (double) t.pan, t.instrument.id.toRawUTF8(), (int) t.clips.size(), (int) t.effects.size()));
        juce::StringArray chords;
        for (size_t i = 0; i < p.chords.size() && i < 16; ++i) chords.add (p.chords[i].chord);
        print ("Chords: " + chords.joinIntoString (" "));
        return 0;
    }

    if (cmd == "import")
    {
        Project p;
        auto folder = args.file (0);
        auto r = loadOrCreate (folder, p);
        if (r.failed()) return fail (r.getErrorMessage());
        r = store::importAudio (p, folder, args.file (1), args.get ("at", "0").getDoubleValue());
        if (r.failed()) return fail (r.getErrorMessage());
        store::save (p, folder);
        print ("Imported " + args.file (1).getFileName());
        return 0;
    }

    if (cmd == "render")
    {
        Project p;
        auto folder = args.file (0);
        auto r = store::load (folder, p);
        if (r.failed()) return fail (r.getErrorMessage());
        RenderRequest req;
        req.sampleRate = args.get ("rate", "44100").getDoubleValue();
        req.includeMastering = ! args.has ("no-master");
        req.projectFolder = folder;
        auto start = juce::Time::getMillisecondCounterHiRes();
        auto audio = OfflineRenderer::render (p, req);
        auto ms = juce::Time::getMillisecondCounterHiRes() - start;
        if (! writeAudioFile (args.file (1), audio, req.sampleRate, 24)) return fail ("could not write output");
        const double secs = audio.getNumSamples() / req.sampleRate;
        print (juce::String::formatted ("Rendered %.1f s in %.1f s (%.1fx real time)", secs, ms / 1000.0, secs / (ms / 1000.0)));
        printLoudness (LoudnessMeter::measure (audio, req.sampleRate));
        return 0;
    }

    if (cmd == "automix" || cmd == "assist")
    {
        Project p;
        auto folder = args.file (0);
        auto r = store::load (folder, p);
        if (r.failed()) return fail (r.getErrorMessage());
        auto analyses = mixing::analyseTracks (p, folder);
        if (cmd == "automix")
        {
            auto rep = mixing::autoMix (p, analyses);
            for (auto& c : rep.changes) print ("  " + c);
            store::save (p, folder);
            return 0;
        }
        RenderRequest req;
        req.sampleRate = 44100;
        req.includeMastering = false;
        req.projectFolder = folder;
        auto mix = OfflineRenderer::render (p, req);
        auto issues = mixing::assistMix (p, mix, req.sampleRate, analyses);
        if (issues.empty()) print ("No problems found.");
        for (auto& i : issues) print ("* " + i.title + "\n    " + i.detail);
        if (args.has ("fix"))
        {
            for (auto& i : issues) if (i.fix) i.fix (p);
            store::save (p, folder);
            print ("Applied " + juce::String ((int) issues.size()) + " fixes.");
        }
        return 0;
    }

    if (cmd == "arrange-check")
    {
        Project p;
        auto r = store::load (args.file (0), p);
        if (r.failed()) return fail (r.getErrorMessage());
        for (auto& i : arrange::analyse (p))
            print ((i.severity == arrange::Issue::Severity::Warning ? "! " : "- ") + i.message);
        return 0;
    }

    if (cmd == "vamp")
    {
        Project p;
        auto folder = args.file (0);
        auto r = store::load (folder, p);
        if (r.failed()) return fail (r.getErrorMessage());
        juce::String chorusId;
        for (auto& s : p.sections) if (s.type == SectionType::Chorus) chorusId = s.id; // last chorus
        if (chorusId.isEmpty()) return fail ("no chorus section");
        arrange::VampOptions o;
        o.repeats = args.get ("repeats", "3").getIntValue();
        o.keyLiftOnLast = args.has ("lift");
        auto ids = arrange::buildVamp (p, chorusId, o);
        store::save (p, folder);
        print ("Added " + juce::String ((int) ids.size()) + " vamp sections.");
        return 0;
    }

    if (cmd == "export")
    {
        Project p;
        auto folder = args.file (0);
        auto r = store::load (folder, p);
        if (r.failed()) return fail (r.getErrorMessage());
        exporting::Options o;
        o.sampleRate = args.get ("rate", "44100").getDoubleValue();
        o.stems = args.has ("stems");
        o.performanceTrack = args.has ("performance");
        o.lyricVideo = args.has ("video");
        o.aac = args.has ("aac");
        auto out = args.has ("out") ? juce::File::getCurrentWorkingDirectory().getChildFile (args.get ("out")) : store::exportsFolder (folder);
        auto res = exporting::exportAll (p, folder, out, o, [] (float f, const juce::String& label)
        {
            std::cout << juce::String::formatted ("\r[%3d%%] ", (int) (f * 100)) << label.paddedRight (' ', 40) << std::flush;
            return true;
        });
        std::cout << std::endl;
        for (auto& f : res.files) print ("  " + f.getFullPathName());
        for (auto& w : res.warnings) print ("warning: " + w);
        for (auto& e : res.errors) print ("error: " + e);
        printLoudness (res.masterLoudness);
        return res.errors.isEmpty() ? 0 : 1;
    }

    if (cmd == "check" || cmd == "package")
    {
        Project p;
        auto folder = args.file (0);
        auto r = store::load (folder, p);
        if (r.failed()) return fail (r.getErrorMessage());
        std::vector<release::Check> checks;
        if (cmd == "package")
        {
            auto out = args.has ("out") ? juce::File::getCurrentWorkingDirectory().getChildFile (args.get ("out")) : store::exportsFolder (folder);
            auto pr = release::writeExportPackage (p, folder, out);
            checks = pr.checks;
            print ("Package: " + pr.folder.getFullPathName());
            print ("Zip:     " + pr.zip.getFullPathName());
        }
        else
        {
            RenderRequest req;
            req.sampleRate = 44100;
            req.projectFolder = folder;
            auto master = OfflineRenderer::render (p, req);
            checks = release::preReleaseChecklist (p, folder, master, req.sampleRate);
        }
        for (auto& c : checks)
            print (juce::String (c.level == release::Check::Level::Pass ? "[ok]   " : c.level == release::Check::Level::Warning ? "[warn] " : "[FIX]  ")
                   + c.area + ": " + c.message);
        return release::hasErrors (checks) ? 2 : 0;
    }

    if (cmd == "loudness")
    {
        juce::AudioBuffer<float> b; double sr = 0;
        if (! readAudio (args.file (0), b, sr)) return 1;
        printLoudness (LoudnessMeter::measure (b, sr));
        return 0;
    }

    if (cmd == "analyse" || cmd == "analyze")
    {
        juce::AudioBuffer<float> b; double sr = 0;
        if (! readAudio (args.file (0), b, sr)) return 1;
        auto k = analysis::detectKey (b, sr);
        auto t = analysis::detectTempo (b, sr);
        print (juce::String::formatted ("Key:   %s (confidence %.2f)", k.key.toString().toRawUTF8(), (double) k.confidence));
        print (juce::String::formatted ("Tempo: %.1f BPM (confidence %.2f)", t.bpm, (double) t.confidence));
        auto prof = analysis::analyseSpectrum (b, sr);
        juce::String bands;
        for (int i = 0; i < analysis::SpectrumProfile::numBands; ++i)
            bands << juce::String (analysis::SpectrumProfile::bandCentres()[(size_t) i], 0) << "Hz " << juce::String (prof.bandDb[(size_t) i], 1) << "  ";
        print ("Bands: " + bands);
        print (juce::String::formatted ("RMS %.1f dBFS, crest %.1f dB, centroid %.0f Hz, stereo correlation %.2f",
                                        (double) prof.rmsDb, (double) prof.crestDb, (double) prof.centroidHz, (double) prof.stereoCorrelation));
        juce::StringArray chords;
        for (auto& c : analysis::detectChords (b, sr, 60.0 / t.bpm * 2))
            if (chords.size() < 24) chords.add (c.chord.name());
        print ("Chords: " + chords.joinIntoString (" "));
        return 0;
    }

    if (cmd == "master")
    {
        juce::AudioBuffer<float> b; double sr = 0;
        if (! readAudio (args.file (0), b, sr)) return 1;
        const float target = args.get ("target", "-14").getFloatValue();
        const float ceiling = args.get ("ceiling", "-1").getFloatValue();
        MasteringSettings s;
        LoudnessMeter::Summary after;
        if (args.has ("ai"))
        {
            auto sug = mastering::suggest (b, sr, args.get ("style"), target, ceiling, args.get ("preset", "Warm"));
            for (auto& n : sug.notes) print ("  " + n);
            s = sug.settings;
            after = sug.after;
        }
        else
        {
            s.preset = args.get ("preset", "Warm");
            s.targetLufs = target;
            s.ceilingDbtp = ceiling;
            s = mastering::fitLoudness (b, sr, s, &after);
        }
        auto out = mastering::render (b, sr, s);
        if (! writeAudioFile (args.file (1), out, sr, 24)) return fail ("could not write output");
        printLoudness (LoudnessMeter::measure (out, sr));
        return 0;
    }

    if (cmd == "vocal")
    {
        juce::AudioBuffer<float> b; double sr = 0;
        if (! readAudio (args.file (0), b, sr)) return 1;
        VocalChain chain;
        chain.prepare (sr, 1024, 2);
        VocalChainSettings vs;
        vs.enabled = true;
        vs.preset = args.get ("preset", "Worship lead");
        vs.amount = args.get ("amount", "0.7").getFloatValue();
        chain.applySettings (vs, Key::fromString (args.get ("key", "C major")));
        const int lat = chain.getLatencySamples();
        juce::AudioBuffer<float> work (2, b.getNumSamples() + lat);
        work.clear();
        for (int ch = 0; ch < 2; ++ch) work.copyFrom (ch, 0, b, juce::jmin (ch, b.getNumChannels() - 1), 0, b.getNumSamples());
        for (int pos = 0; pos < work.getNumSamples(); pos += 1024)
        {
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, pos, juce::jmin (1024, work.getNumSamples() - pos));
            chain.process (view, nullptr);
        }
        juce::AudioBuffer<float> out (2, b.getNumSamples());
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, work, ch, lat, b.getNumSamples());
        if (! writeAudioFile (args.file (1), out, sr, 24)) return fail ("could not write output");
        print ("Processed with \"" + vs.preset + "\" (latency " + juce::String (lat) + " samples compensated)");
        return 0;
    }

    if (cmd == "room")
    {
        juce::AudioBuffer<float> b; double sr = 0;
        if (! readAudio (args.file (0), b, sr)) return 1;
        auto r = analysis::analyseRoom (b, sr);
        print (juce::String::formatted ("Room: %s (noise floor %.1f dBFS, hum %s, hiss %.1f dB, rumble %.1f dB)", r.rating.toRawUTF8(),
                                        (double) r.noiseFloorDb, r.humFrequency > 0 ? (juce::String (r.humFrequency) + " Hz").toRawUTF8() : "none",
                                        (double) r.hissDb, (double) r.rumbleDb));
        for (auto& s : r.suggestions) print ("  - " + s);
        return 0;
    }

    if (cmd == "harmony")
    {
        juce::AudioBuffer<float> b; double sr = 0;
        if (! readAudio (args.file (0), b, sr)) return 1;
        auto key = Key::fromString (args.get ("key", "C major"));
        auto parts = vocal::generateHarmonies (b, sr, key, vocal::harmonyPreset (args.get ("parts", "3").getIntValue()));
        auto out = args.file (1);
        out.createDirectory();
        for (size_t i = 0; i < parts.size(); ++i)
            writeAudioFile (out.getChildFile ("harmony_" + juce::String ((int) i + 1) + ".wav"), parts[i], sr, 24);
        print ("Wrote " + juce::String ((int) parts.size()) + " harmony parts to " + out.getFullPathName());
        return 0;
    }

    if (cmd == "separate")
    {
        juce::AudioBuffer<float> b; double sr = 0;
        if (! readAudio (args.file (0), b, sr)) return 1;
        auto stems = vocal::separateStems (b, sr);
        auto out = args.file (1);
        out.createDirectory();
        writeAudioFile (out.getChildFile ("vocals.wav"), stems.vocals, sr, 24);
        writeAudioFile (out.getChildFile ("drums.wav"), stems.drums, sr, 24);
        writeAudioFile (out.getChildFile ("bass.wav"), stems.bass, sr, 24);
        writeAudioFile (out.getChildFile ("other.wav"), stems.other, sr, 24);
        print ("Wrote vocals, drums, bass and other to " + out.getFullPathName());
        return 0;
    }

    if (cmd == "compare")
    {
        juce::AudioBuffer<float> a, b; double sa = 0, sb = 0;
        if (! readAudio (args.file (0), a, sa) || ! readAudio (args.file (1), b, sb)) return 1;
        auto c = mixing::compareToReference (a, b, sa);
        for (auto& n : c.notes) print ("- " + n);
        return 0;
    }

    if (cmd == "starter-loops")
    {
        auto out = args.file (0);
        const int n = library::generateStarterLoops (out);
        print ("Wrote " + juce::String (n) + " starter loops to " + out.getFullPathName());
        LibraryIndex index;
        if (index.open (LibraryIndex::defaultLocation()).wasOk())
            print ("Indexed " + juce::String (index.scanFolder (out, "starter")) + " loops into " + LibraryIndex::defaultLocation().getFullPathName());
        return 0;
    }

    usage();
    return fail ("unknown command: " + cmd);
}
