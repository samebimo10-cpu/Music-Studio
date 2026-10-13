#include "Exporter.h"
#include "../engine/OfflineRenderer.h"
#include "../mastering/MasteringChain.h"
#include "../engine/ClipCache.h"
#include "../midi/MidiTools.h"
#include "../project/ProjectStore.h"
#include "../model/TempoMap.h"
#include "../dsp/DspCommon.h"
#include "../dsp/TimeStretch.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace studio::exporting
{

juce::String baseName (const Project& p)
{
    auto title = p.release.title.isNotEmpty() ? p.release.title : p.name;
    auto artist = p.release.primaryArtist;
    auto n = artist.isNotEmpty() ? artist + " - " + title : title;
    return juce::File::createLegalFileName (n);
}

juce::File findTool (const juce::String& name)
{
#if JUCE_WINDOWS
    const juce::String exe = name + ".exe";
    const char sep = ';';
#else
    const juce::String exe = name;
    const char sep = ':';
#endif
    // Bundled next to the app first, then PATH.
    auto appDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (auto candidate : { appDir.getChildFile (exe), appDir.getChildFile ("tools").getChildFile (exe) })
        if (candidate.existsAsFile()) return candidate;

    auto path = juce::SystemStats::getEnvironmentVariable ("PATH", {});
    for (auto& dir : juce::StringArray::fromTokens (path, juce::String::charToString (sep), ""))
    {
        auto f = juce::File (dir).getChildFile (exe);
        if (f.existsAsFile()) return f;
    }
#if JUCE_MAC
    for (auto d : { "/opt/homebrew/bin", "/usr/local/bin" })
        if (juce::File (d).getChildFile (exe).existsAsFile()) return juce::File (d).getChildFile (exe);
#endif
    return {};
}

namespace
{
    juce::Result runTool (const juce::StringArray& args, int timeoutMs = 10 * 60 * 1000)
    {
        juce::ChildProcess proc;
        if (! proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            return juce::Result::fail ("Could not start " + args[0]);
        auto output = proc.readAllProcessOutput();
        proc.waitForProcessToFinish (timeoutMs);
        if (proc.getExitCode() != 0)
            return juce::Result::fail (juce::File (args[0]).getFileName() + " failed: " + output.getLastCharacters (400));
        return juce::Result::ok();
    }

    juce::StringPairArray tagsFor (const Project& p)
    {
        juce::StringPairArray t;
        t.set ("title", p.release.title.isNotEmpty() ? p.release.title : p.name);
        t.set ("artist", p.release.primaryArtist);
        t.set ("genre", p.release.genre);
        if (p.release.releaseDate.length() >= 4) t.set ("date", p.release.releaseDate.substring (0, 4));
        if (p.release.isrc.isNotEmpty()) t.set ("ISRC", p.release.isrc);
        if (p.release.copyrightLine.isNotEmpty()) t.set ("copyright", p.release.copyrightLine);
        return t;
    }

    juce::StringPairArray wavMetadata (const Project& p)
    {
        // BWF/INFO chunks: title, artist, ISRC for distributors that read them.
        juce::StringPairArray m;
        m.set (juce::WavAudioFormat::riffInfoTitle, p.release.title.isNotEmpty() ? p.release.title : p.name);
        m.set (juce::WavAudioFormat::riffInfoArtist, p.release.primaryArtist);
        m.set (juce::WavAudioFormat::riffInfoGenre, p.release.genre);
        if (p.release.copyrightLine.isNotEmpty()) m.set (juce::WavAudioFormat::riffInfoCopyright, p.release.copyrightLine);
        if (p.release.isrc.isNotEmpty()) m.set (juce::WavAudioFormat::internationalStandardRecordingCode, p.release.isrc);
        m.set (juce::WavAudioFormat::bwavOriginator, "Studio");
        return m;
    }

    bool routesTo (const Project& p, const Track& t, const juce::String& busId)
    {
        int guard = 0;
        for (auto out = t.outputBusId; out.isNotEmpty() && guard++ < 32;)
        {
            if (out == busId) return true;
            auto* b = p.findTrack (out);
            out = b != nullptr ? b->outputBusId : juce::String();
        }
        return false;
    }

    juce::String trackBus (const Project& p, const Track& t)
    {
        // Stem grouping: the top-level bus a track ends up in, or "Master" if none.
        juce::String top;
        int guard = 0;
        for (auto out = t.outputBusId; out.isNotEmpty() && guard++ < 32;)
        {
            top = out;
            auto* b = p.findTrack (out);
            out = b != nullptr ? b->outputBusId : juce::String();
        }
        return top;
    }
}

juce::Result encodeMp3 (const juce::File& wav, const juce::File& mp3, const juce::StringPairArray& tags)
{
    mp3.deleteFile();
    if (auto ffmpeg = findTool ("ffmpeg"); ffmpeg.existsAsFile())
    {
        juce::StringArray args { ffmpeg.getFullPathName(), "-y", "-loglevel", "error", "-i", wav.getFullPathName(),
                                 "-codec:a", "libmp3lame", "-b:a", "320k" };
        for (auto& k : tags.getAllKeys()) { args.add ("-metadata"); args.add (k + "=" + tags[k]); }
        args.add (mp3.getFullPathName());
        return runTool (args);
    }
    if (auto lame = findTool ("lame"); lame.existsAsFile())
    {
        juce::StringArray args { lame.getFullPathName(), "--silent", "-b", "320", "--cbr", "-q", "0" };
        if (tags["title"].isNotEmpty())  { args.add ("--tt"); args.add (tags["title"]); }
        if (tags["artist"].isNotEmpty()) { args.add ("--ta"); args.add (tags["artist"]); }
        args.add (wav.getFullPathName());
        args.add (mp3.getFullPathName());
        return runTool (args);
    }
    return juce::Result::fail ("MP3 export needs ffmpeg or lame. Install one (Settings -> Tools shows how), then export again.");
}

juce::Result encodeAac (const juce::File& wav, const juce::File& m4a, const juce::StringPairArray& tags)
{
    m4a.deleteFile();
    if (auto ffmpeg = findTool ("ffmpeg"); ffmpeg.existsAsFile())
    {
        juce::StringArray args { ffmpeg.getFullPathName(), "-y", "-loglevel", "error", "-i", wav.getFullPathName(), "-c:a", "aac", "-b:a", "256k" };
        for (auto& k : tags.getAllKeys()) { args.add ("-metadata"); args.add (k + "=" + tags[k]); }
        args.add (m4a.getFullPathName());
        return runTool (args);
    }
#if JUCE_MAC
    if (juce::File ("/usr/bin/afconvert").existsAsFile())
        return runTool ({ "/usr/bin/afconvert", "-f", "m4af", "-d", "aac", "-b", "256000", wav.getFullPathName(), m4a.getFullPathName() });
#endif
    return juce::Result::fail ("AAC export needs ffmpeg.");
}

//==============================================================================
juce::Result renderLyricVideo (const Project& p, const juce::File& folder, const juce::File& audioFile, const juce::File& mp4, int w, int h)
{
    auto ffmpeg = findTool ("ffmpeg");
    if (! ffmpeg.existsAsFile())
        return juce::Result::fail ("Lyric video export needs ffmpeg.");

    // Lines with times: synced lyrics, or the plain lyrics spread over the song.
    std::vector<SyncedLyricLine> lines = p.release.syncedLyrics;
    double audioSeconds = 0;
    {
        juce::AudioBuffer<float> tmp;
        double rate = 0;
        if (ClipCache::readFile (audioFile, tmp, rate)) audioSeconds = tmp.getNumSamples() / rate;
    }
    if (lines.empty())
    {
        auto plain = juce::StringArray::fromLines (p.release.lyrics);
        plain.removeEmptyStrings();
        for (int i = 0; i < plain.size(); ++i)
            lines.push_back ({ audioSeconds * i / juce::jmax (1, plain.size()), plain[i] });
    }
    if (lines.empty())
        return juce::Result::fail ("Add lyrics (and sync them) before exporting a lyric video.");

    juce::Image artwork;
    if (p.release.artworkFile.isNotEmpty())
        artwork = juce::ImageFileFormat::loadFrom (store::resolve (folder, p.release.artworkFile));

    auto temp = juce::File::createTempFile ("lyricvideo");
    temp.createDirectory();
    juce::String concat;

    auto drawFrame = [&] (const juce::String& current, const juce::String& next)
    {
        juce::Image img (juce::Image::RGB, w, h, true, juce::SoftwareImageType());
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff101018));
        if (artwork.isValid())
        {
            // Artwork fills the frame, darkened so the text stays readable.
            g.setOpacity (0.45f);
            g.drawImage (artwork, juce::Rectangle<float> (0, 0, (float) w, (float) h), juce::RectanglePlacement::fillDestination);
            g.setOpacity (1.0f);
            const int side = h / 3;
            g.drawImage (artwork, juce::Rectangle<float> ((float) (w - side) / 2.0f, h * 0.08f, (float) side, (float) side),
                         juce::RectanglePlacement::centred);
        }
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions ((float) h * 0.065f, juce::Font::bold));
        g.drawFittedText (current, juce::Rectangle<int> (w / 10, (int) (h * 0.55), w * 8 / 10, (int) (h * 0.2)), juce::Justification::centred, 2);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions ((float) h * 0.04f));
        g.drawFittedText (next, juce::Rectangle<int> (w / 10, (int) (h * 0.76), w * 8 / 10, (int) (h * 0.1)), juce::Justification::centred, 1);
        g.setFont (juce::FontOptions ((float) h * 0.03f));
        g.drawText ((p.release.primaryArtist.isNotEmpty() ? p.release.primaryArtist + " - " : juce::String()) + (p.release.title.isNotEmpty() ? p.release.title : p.name),
                    juce::Rectangle<int> (0, (int) (h * 0.92), w, (int) (h * 0.06)), juce::Justification::centred);
        return img;
    };

    juce::PNGImageFormat png;
    auto addFrame = [&] (int index, const juce::String& cur, const juce::String& nxt, double duration)
    {
        auto file = temp.getChildFile (juce::String::formatted ("frame_%05d.png", index));
        juce::FileOutputStream out (file);
        png.writeImageToStream (drawFrame (cur, nxt), out);
        concat << "file '" << file.getFullPathName().replace ("'", "'\\''") << "'\nduration " << juce::String (juce::jmax (0.04, duration), 3) << "\n";
        return file;
    };

    juce::File last;
    int index = 0;
    if (lines.front().timeSeconds > 0.05)
        last = addFrame (index++, {}, lines.front().text, lines.front().timeSeconds);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const double end = i + 1 < lines.size() ? lines[i + 1].timeSeconds : juce::jmax (lines[i].timeSeconds + 4.0, audioSeconds);
        last = addFrame (index++, lines[i].text, i + 1 < lines.size() ? lines[i + 1].text : juce::String(), end - lines[i].timeSeconds);
    }
    concat << "file '" << last.getFullPathName().replace ("'", "'\\''") << "'\n"; // concat demuxer needs the last file repeated
    auto list = temp.getChildFile ("frames.txt");
    list.replaceWithText (concat);

    mp4.deleteFile();
    auto r = runTool ({ ffmpeg.getFullPathName(), "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", list.getFullPathName(),
                        "-i", audioFile.getFullPathName(), "-vf", "fps=30", "-pix_fmt", "yuv420p", "-c:v", "libx264", "-preset", "veryfast",
                        "-tune", "stillimage", "-c:a", "aac", "-b:a", "256k", "-shortest", "-movflags", "+faststart", mp4.getFullPathName() });
    temp.deleteRecursively();
    return r;
}

//==============================================================================
Result exportAll (const Project& p, const juce::File& folder, const juce::File& out, const Options& o,
                  std::function<bool (float, const juce::String&)> progress)
{
    Result res;
    out.createDirectory();
    const auto base = baseName (p);
    const double sr = o.sampleRate;
    auto report = [&] (float f, const juce::String& label) { return ! progress || progress (f, label); };

    // Every master-chain render uses the same settings, fitted so the master lands on
    // the loudness target (see step 1).
    Project pm = p;
    auto render = [&] (std::function<bool (const Track&)> filter, bool master, bool click = false)
    {
        RenderRequest req;
        req.sampleRate = sr;
        req.includeMastering = master && o.applyMastering;
        req.trackFilter = filter;
        req.projectFolder = folder;
        req.includeClick = click;
        req.clickOnLeftOnly = click;
        return OfflineRenderer::render (pm, req);
    };

    auto isVocal = [&] (const Track& t)
    {
        if (isVocalRole (t.role)) return true;
        // A bus is "vocal" when only vocal tracks feed it.
        if (t.type == TrackType::Bus)
        {
            bool any = false, allVocal = true;
            for (auto& s : p.tracks)
                if (s.type != TrackType::Bus && routesTo (p, s, t.id)) { any = true; allVocal &= isVocalRole (s.role); }
            return any && allVocal;
        }
        return false;
    };

    // 1. Master
    if (! report (0.02f, "Rendering the mix")) return res;
    juce::AudioBuffer<float> master;
    if (o.applyMastering && p.mastering.enabled)
    {
        auto mix = render ({}, false);
        if (mix.getNumSamples() == 0) { res.errors.add ("Nothing to export: the song is empty."); return res; }
        if (! report (0.12f, "Mastering to " + juce::String (p.mastering.targetLufs, 1) + " LUFS")) return res;
        pm.mastering = mastering::fitLoudness (mix, sr, p.mastering);
        master = mastering::render (mix, sr, pm.mastering);
    }
    else
        master = render ({}, false);
    if (master.getNumSamples() == 0) { res.errors.add ("Nothing to export: the song is empty."); return res; }

    // Trim the rendered tail to the last audible sample plus a short fade room.
    {
        int last = master.getNumSamples() - 1;
        while (last > 0 && master.getMagnitude (0, last, 1) < 1.0e-4f && master.getMagnitude (1, last, 1) < 1.0e-4f) --last;
        master.setSize (2, juce::jmin (master.getNumSamples(), last + (int) (0.5 * sr)), true);
    }
    res.masterLoudness = LoudnessMeter::measure (master, sr);

    auto masterWav = out.getChildFile (base + ".wav");
    if (o.masterWav || o.mp3 || o.aac || o.lyricVideo)
    {
        if (writeAudioFile (masterWav, master, sr, 24, wavMetadata (p))) res.files.add (masterWav);
        else res.errors.add ("Could not write " + masterWav.getFileName());
    }

    if (o.flac)
    {
        if (! report (0.25f, "Writing FLAC")) return res;
        auto f = out.getChildFile (base + ".flac");
        if (writeAudioFile (f, master, sr, 24)) res.files.add (f);
        else res.errors.add ("Could not write FLAC.");
    }
    if (o.mp3)
    {
        if (! report (0.3f, "Encoding MP3")) return res;
        auto f = out.getChildFile (base + ".mp3");
        auto r = encodeMp3 (masterWav, f, tagsFor (p));
        if (r.wasOk()) res.files.add (f); else res.warnings.add (r.getErrorMessage());
    }
    if (o.aac)
    {
        if (! report (0.35f, "Encoding AAC")) return res;
        auto f = out.getChildFile (base + ".m4a");
        auto r = encodeAac (masterWav, f, tagsFor (p));
        if (r.wasOk()) res.files.add (f); else res.warnings.add (r.getErrorMessage());
    }

    // 2. Instrumental and a cappella (same master chain, so they line up with the master).
    if (o.instrumental)
    {
        if (! report (0.4f, "Rendering the instrumental")) return res;
        auto buf = render ([&] (const Track& t) { return ! isVocal (t); }, true);
        buf.setSize (2, master.getNumSamples(), true);
        auto f = out.getChildFile (base + " (Instrumental).wav");
        if (writeAudioFile (f, buf, sr, 24)) res.files.add (f);
    }
    if (o.acappella)
    {
        if (! report (0.5f, "Rendering the a cappella")) return res;
        bool anyVocal = false;
        for (auto& t : p.tracks) anyVocal |= isVocalRole (t.role);
        if (anyVocal)
        {
            auto buf = render ([&] (const Track& t) { return isVocal (t) || (t.type == TrackType::Bus && ! t.name.containsIgnoreCase ("drum")); }, false);
            buf.setSize (2, master.getNumSamples(), true);
            auto f = out.getChildFile (base + " (A cappella).wav");
            if (writeAudioFile (f, buf, sr, 24)) res.files.add (f);
        }
        else
            res.warnings.add ("No vocal tracks, so no a cappella was exported.");
    }

    // 3. Stems: one WAV per top-level bus (tracks routed straight to the master form their own stems).
    if (o.stems)
    {
        std::map<juce::String, juce::StringArray> groups; // stem name -> track ids
        for (auto& t : p.tracks)
        {
            if (t.type == TrackType::Bus) continue;
            const auto bus = trackBus (p, t);
            const auto* b = bus.isNotEmpty() ? p.findTrack (bus) : nullptr;
            groups[b != nullptr ? b->name : t.name].add (t.id);
        }
        auto stemFolder = out.getChildFile ("Stems");
        stemFolder.createDirectory();
        int k = 0;
        for (auto& [name, ids] : groups)
        {
            if (! report (0.55f + 0.2f * (float) k++ / (float) groups.size(), "Stem: " + name)) return res;
            auto buf = render ([&] (const Track& t)
            {
                if (t.type == TrackType::Bus)
                {
                    for (auto& id : ids)
                        if (auto* s = p.findTrack (id); s != nullptr && routesTo (p, *s, t.id)) return true;
                    return false;
                }
                return ids.contains (t.id);
            }, false);
            buf.setSize (2, master.getNumSamples(), true);
            auto f = stemFolder.getChildFile (base + " - " + juce::File::createLegalFileName (name) + ".wav");
            if (writeAudioFile (f, buf, sr, 24)) res.files.add (f);
        }
    }

    // 4. Performance track: click and cues on the left, music on the right.
    if (o.performanceTrack)
    {
        if (! report (0.8f, "Rendering the performance track")) return res;
        auto buf = render ([&] (const Track& t) { return t.role != TrackRole::LeadVocal; }, true, true);

        // Section cues: a rising two-note tone one bar before each section, plus a cue sheet.
        TempoMap tm (p);
        juce::String sheet;
        sheet << (p.release.title.isNotEmpty() ? p.release.title : p.name) << " - performance track cues\n"
              << "Left: click and cues. Right: music.\n\n";
        for (auto& s : p.sections)
        {
            const double cueBeat = juce::jmax (0.0, s.startBeat - p.beatsPerBar());
            const auto at = tm.beatsToSamples (cueBeat, sr);
            const int len = (int) (0.12 * sr);
            const double f1 = s.type == SectionType::Chorus ? 880 : s.type == SectionType::Bridge ? 660 : 740;
            for (int note = 0; note < 2; ++note)
                for (int i = 0; i < len; ++i)
                {
                    const auto idx = at + note * (juce::int64) (0.18 * sr) + i;
                    if (idx >= buf.getNumSamples()) break;
                    const float env = std::sin (juce::MathConstants<float>::pi * (float) i / (float) len);
                    buf.addSample (0, (int) idx, 0.4f * env * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f1 * (note ? 1.25 : 1.0) * i / sr));
                }
            const double secs = tm.beatsToSeconds (s.startBeat);
            sheet << juce::String::formatted ("%d:%05.2f  ", (int) secs / 60, std::fmod (secs, 60.0)) << toString (s.type)
                  << (s.name.isNotEmpty() && s.name != toString (s.type) ? " (" + s.name + ")" : juce::String()) << "\n";
        }
        auto f = out.getChildFile (base + " (Performance track).wav");
        if (writeAudioFile (f, buf, sr, 24)) res.files.add (f);
        auto cues = out.getChildFile (base + " (Performance track cues).txt");
        cues.replaceWithText (sheet);
        res.files.add (cues);
    }

    // 5. MIDI
    if (o.midi)
    {
        auto f = out.getChildFile (base + ".mid");
        if (midi::exportMidiFile (p, f)) res.files.add (f);
        else res.warnings.add ("No MIDI parts to export.");
    }

    // 6. Lyric video
    if (o.lyricVideo)
    {
        if (! report (0.9f, "Rendering the lyric video")) return res;
        auto f = out.getChildFile (base + " (Lyric video).mp4");
        auto r = renderLyricVideo (p, folder, masterWav, f);
        if (r.wasOk()) res.files.add (f); else res.warnings.add (r.getErrorMessage());
    }

    if (! o.masterWav && (o.mp3 || o.aac || o.lyricVideo))
        masterWav.deleteFile();

    report (1.0f, "Done");
    return res;
}

//==============================================================================
void matchAlbumLoudness (std::vector<AlbumTrack>& tracks, float target, float ceiling)
{
    for (auto& t : tracks)
    {
        juce::AudioBuffer<float> audio;
        double rate = 0;
        if (! ClipCache::readFile (t.masterFile, audio, rate)) continue;
        t.loudness = LoudnessMeter::measure (audio, rate);
        float trim = target - t.loudness.integratedLufs;
        // Never push peaks past the ceiling; quiet ballads may stay a little under target.
        trim = juce::jmin (trim, ceiling - t.loudness.truePeakDb);
        // Within half a LU of the target is "matched" - leave intentional dynamics alone.
        if (std::abs (target - t.loudness.integratedLufs) < 0.5f) trim = 0;
        t.trimDb = trim;
    }
}

Result writeAlbum (const std::vector<AlbumTrack>& tracks, const juce::File& out, double sr)
{
    Result res;
    out.createDirectory();
    juce::AudioBuffer<float> album (2, 0);
    juce::String cue = "FILE \"Album.wav\" WAVE\n";
    juce::int64 pos = 0;
    std::vector<juce::AudioBuffer<float>> pieces;

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        auto& t = tracks[i];
        juce::AudioBuffer<float> audio;
        double rate = 0;
        if (! ClipCache::readFile (t.masterFile, audio, rate)) { res.errors.add ("Can't read " + t.masterFile.getFileName()); continue; }
        auto resampled = rate != sr ? dsp::timestretch::resample (audio, rate, sr) : audio;
        resampled.applyGain (dsp::dbToGain (t.trimDb));
        const int gap = (int) (t.gapAfterSeconds * sr);
        juce::AudioBuffer<float> piece (2, resampled.getNumSamples() + (i + 1 < tracks.size() ? gap : 0));
        piece.clear();
        for (int ch = 0; ch < 2; ++ch)
            piece.copyFrom (ch, 0, resampled, juce::jmin (ch, resampled.getNumChannels() - 1), 0, resampled.getNumSamples());

        auto f = out.getChildFile (juce::String::formatted ("%02d - ", (int) i + 1) + juce::File::createLegalFileName (t.title) + ".wav");
        if (writeAudioFile (f, piece, sr, 24)) res.files.add (f);

        const auto frames = pos * 75 / (juce::int64) sr; // CUE frames: 75 per second
        cue << juce::String::formatted ("  TRACK %02d AUDIO\n    TITLE \"%s\"\n    INDEX 01 %02d:%02d:%02d\n", (int) i + 1,
                                        t.title.toRawUTF8(), (int) (frames / 75 / 60), (int) (frames / 75 % 60), (int) (frames % 75));
        pos += piece.getNumSamples();
        pieces.push_back (std::move (piece));
    }

    album.setSize (2, (int) pos);
    int at = 0;
    for (auto& pc : pieces)
    {
        for (int ch = 0; ch < 2; ++ch) album.copyFrom (ch, at, pc, ch, 0, pc.getNumSamples());
        at += pc.getNumSamples();
    }
    auto albumFile = out.getChildFile ("Album.wav");
    if (writeAudioFile (albumFile, album, sr, 24)) res.files.add (albumFile);
    auto cueFile = out.getChildFile ("Album.cue");
    cueFile.replaceWithText (cue);
    res.files.add (cueFile);
    res.masterLoudness = LoudnessMeter::measure (album, sr);
    return res;
}

} // namespace studio::exporting
