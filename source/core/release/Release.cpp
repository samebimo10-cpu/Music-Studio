#include "Release.h"
#include "../analysis/AudioAnalysis.h"
#include "../exporting/Exporter.h"
#include "../engine/ClipCache.h"
#include "../project/ProjectStore.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace studio::release
{

namespace
{
    Check pass (const juce::String& area, const juce::String& msg)  { return { Check::Level::Pass, area, msg }; }
    Check warn (const juce::String& area, const juce::String& msg)  { return { Check::Level::Warning, area, msg }; }
    Check error (const juce::String& area, const juce::String& msg) { return { Check::Level::Error, area, msg }; }

    bool looksLikeUrl (const juce::String& s)
    {
        auto l = s.toLowerCase();
        return l.contains ("http") || l.contains ("www.") || l.contains (".com") || l.contains (".ng") || l.contains ("@")
            || l.contains (".org") || l.contains (".net");
    }

    bool looksLikePrice (const juce::String& s)
    {
        auto l = s.toLowerCase();
        return l.containsAnyOf (juce::String::fromUTF8 ("$\xe2\x82\xa6\xc2\xa3\xe2\x82\xac")) || l.contains (" naira") || l.contains ("free download");
    }

    bool isAllCaps (const juce::String& s)
    {
        int letters = 0, upper = 0;
        for (auto c : s)
            if (juce::CharacterFunctions::isLetter (c)) { ++letters; if (juce::CharacterFunctions::isUpperCase (c)) ++upper; }
        return letters > 4 && upper == letters;
    }

    void checkText (std::vector<Check>& out, const juce::String& field, const juce::String& v, bool required)
    {
        if (v.trim().isEmpty())
        {
            if (required) out.push_back (error ("Metadata", field + " is missing."));
            return;
        }
        if (v != v.trim()) out.push_back (warn ("Metadata", field + " has spaces at the start or end."));
        if (v.contains ("  ")) out.push_back (warn ("Metadata", field + " has double spaces."));
        if (isAllCaps (v)) out.push_back (warn ("Metadata", field + " is in ALL CAPS. Stores usually reject this unless it is the official styling."));
        if (looksLikeUrl (v)) out.push_back (error ("Metadata", field + " contains a web address or handle. Stores do not allow this."));
    }
}

bool hasErrors (const std::vector<Check>& checks)
{
    for (auto& c : checks) if (c.level == Check::Level::Error) return true;
    return false;
}

//==============================================================================
juce::String normaliseIsrc (const juce::String& isrc)
{
    return isrc.removeCharacters ("- ").toUpperCase();
}

bool isValidIsrc (const juce::String& isrcIn)
{
    auto s = normaliseIsrc (isrcIn);
    if (s.length() != 12) return false;
    for (int i = 0; i < 2; ++i) if (! juce::CharacterFunctions::isLetter (s[i])) return false;
    for (int i = 2; i < 5; ++i) if (! juce::CharacterFunctions::isLetterOrDigit (s[i])) return false;
    for (int i = 5; i < 12; ++i) if (! juce::CharacterFunctions::isDigit (s[i])) return false;
    return true;
}

bool isValidUpc (const juce::String& upcIn)
{
    auto s = upcIn.removeCharacters ("- ");
    if (s.length() != 12 && s.length() != 13) return false;
    if (! s.containsOnly ("0123456789")) return false;
    // GS1 check digit: weights 3,1,3,1... from the right (excluding the check digit).
    int sum = 0;
    const int n = s.length();
    for (int i = n - 2, k = 0; i >= 0; --i, ++k)
        sum += (s[i] - '0') * (k % 2 == 0 ? 3 : 1);
    const int check = (10 - sum % 10) % 10;
    return check == s[n - 1] - '0';
}

std::vector<Check> validateSplits (const std::vector<Split>& splits)
{
    std::vector<Check> out;
    if (splits.empty())
    {
        out.push_back (warn ("Splits", "No splits entered. If you wrote the song with others, agree and enter the percentages before release."));
        return out;
    }
    double total = 0;
    juce::StringArray names;
    for (auto& s : splits)
    {
        total += s.percent;
        if (s.name.trim().isEmpty()) out.push_back (error ("Splits", "A split has no name."));
        if (s.percent <= 0) out.push_back (error ("Splits", s.name + " has a zero or negative share."));
        if (names.contains (s.name.trim(), true)) out.push_back (warn ("Splits", s.name + " is listed twice."));
        names.add (s.name.trim());
    }
    if (std::abs (total - 100.0) > 0.01)
        out.push_back (error ("Splits", juce::String::formatted ("Splits add up to %.2f%%. They must total 100%%.", total)));
    else
        out.push_back (pass ("Splits", "Splits total 100%."));
    return out;
}

std::vector<Check> validateMetadata (const ReleaseMetadata& m)
{
    std::vector<Check> out;
    checkText (out, "Song title", m.title, true);
    checkText (out, "Primary artist", m.primaryArtist, true);
    for (auto& f : m.featuredArtists) checkText (out, "Featured artist", f, false);
    if (m.songwriters.isEmpty()) out.push_back (error ("Metadata", "Add at least one songwriter (needed for publishing royalties)."));
    for (auto& s : m.songwriters)
    {
        checkText (out, "Songwriter", s, false);
        if (s.trim().indexOfChar (' ') < 0)
            out.push_back (warn ("Metadata", "Songwriter \"" + s + "\" should be a full legal name (first and last), not a stage name."));
    }
    if (m.genre.isEmpty()) out.push_back (error ("Metadata", "Choose a genre."));
    if (m.language.isEmpty()) out.push_back (error ("Metadata", "Choose the lyrics language."));

    // Featured artists belong in their own field, not in the title.
    auto t = m.title.toLowerCase();
    if (t.contains ("feat.") || t.contains ("ft.") || t.contains ("featuring"))
        out.push_back (error ("Metadata", "Put featured artists in the Featured artist field, not in the title."));
    if (t.contains ("official") || t.contains ("audio") || t.contains ("new single") || t.contains ("prod."))
        out.push_back (warn ("Metadata", "The title contains words stores reject (\"official\", \"audio\", \"new single\", \"prod.\")."));
    if (looksLikePrice (m.title)) out.push_back (error ("Metadata", "The title mentions a price or download."));

    // Artist name consistency: the primary artist should not also appear as featured.
    for (auto& f : m.featuredArtists)
        if (f.trim().equalsIgnoreCase (m.primaryArtist.trim()))
            out.push_back (error ("Metadata", "The primary artist is also listed as featured."));
        else if (f.trim().removeCharacters (" .").equalsIgnoreCase (m.primaryArtist.trim().removeCharacters (" .")))
            out.push_back (warn ("Metadata", "\"" + f + "\" and \"" + m.primaryArtist + "\" look like the same artist spelled differently."));

    // Dates.
    auto checkDate = [&] (const juce::String& d, const juce::String& field, bool required)
    {
        if (d.isEmpty()) { if (required) out.push_back (error ("Metadata", field + " is missing.")); return; }
        auto parts = juce::StringArray::fromTokens (d, "-", "");
        if (parts.size() != 3 || parts[0].length() != 4)
            out.push_back (error ("Metadata", field + " must be YYYY-MM-DD."));
    };
    checkDate (m.releaseDate, "Release date", true);
    checkDate (m.originalReleaseDate, "Original release date", false);
    if (m.releaseDate.isNotEmpty())
    {
        auto parts = juce::StringArray::fromTokens (m.releaseDate, "-", "");
        if (parts.size() == 3)
        {
            juce::Time rd (parts[0].getIntValue(), parts[1].getIntValue() - 1, parts[2].getIntValue(), 0, 0);
            const auto days = (rd - juce::Time::getCurrentTime()).inDays();
            if (days < 14)
                out.push_back (warn ("Metadata", "Release date is less than 2 weeks away. Most distributors need 2-4 weeks for store delivery and playlist pitching."));
        }
    }

    // Codes.
    if (m.isrc.isNotEmpty() && ! isValidIsrc (m.isrc))
        out.push_back (error ("Codes", "ISRC \"" + m.isrc + "\" is not valid. Format: CC-XXX-YY-NNNNN."));
    else if (m.isrc.isEmpty())
        out.push_back (pass ("Codes", "No ISRC entered: the distributor will assign one."));
    if (m.upc.isNotEmpty() && ! isValidUpc (m.upc))
        out.push_back (error ("Codes", "UPC \"" + m.upc + "\" is not valid (12 or 13 digits with a correct check digit)."));
    else if (m.upc.isEmpty())
        out.push_back (pass ("Codes", "No UPC entered: the distributor will assign one."));

    if (m.copyrightLine.isEmpty())
        out.push_back (warn ("Metadata", "Add a copyright line (e.g. \"2026 " + (m.primaryArtist.isNotEmpty() ? m.primaryArtist : juce::String ("Artist Name")) + "\")."));

    auto splitChecks = validateSplits (m.splits);
    out.insert (out.end(), splitChecks.begin(), splitChecks.end());

    if (! hasErrors (out))
        out.push_back (pass ("Metadata", "Metadata looks complete."));
    return out;
}

//==============================================================================
ArtworkReport checkArtwork (const juce::File& file, const ReleaseMetadata& m)
{
    ArtworkReport r;
    if (! file.existsAsFile())
    {
        r.checks.push_back (error ("Artwork", "No cover art. Add a 3000x3000 JPG or PNG, or make one with the art maker."));
        return r;
    }
    auto ext = file.getFileExtension().toLowerCase();
    if (ext != ".jpg" && ext != ".jpeg" && ext != ".png")
        r.checks.push_back (error ("Artwork", "Cover art must be JPG or PNG."));
    if (file.getSize() > 36 * 1024 * 1024)
        r.checks.push_back (warn ("Artwork", "The image file is very large (over 36 MB); some distributors reject it."));

    auto img = juce::ImageFileFormat::loadFrom (file);
    if (! img.isValid())
    {
        r.checks.push_back (error ("Artwork", "The image could not be read."));
        return r;
    }
    r.width = img.getWidth();
    r.height = img.getHeight();
    if (r.width != r.height)
        r.checks.push_back (error ("Artwork", juce::String::formatted ("Cover art must be square (it is %d x %d).", r.width, r.height)));
    if (r.width < 3000 || r.height < 3000)
        r.checks.push_back (error ("Artwork", juce::String::formatted ("Cover art must be at least 3000 x 3000 px (it is %d x %d).", r.width, r.height)));
    else
        r.checks.push_back (pass ("Artwork", "Size is 3000 x 3000 or larger."));

    // Work on a 1000 px copy for speed.
    auto small = img.rescaled (1000, 1000, juce::Graphics::mediumResamplingQuality);
    const juce::Image::BitmapData bd (small, juce::Image::BitmapData::readOnly);
    auto lum = [&] (int x, int y) { auto c = bd.getPixelColour (x, y); return 0.299f * c.getFloatRed() + 0.587f * c.getFloatGreen() + 0.114f * c.getFloatBlue(); };

    // Sharpness: variance of the Laplacian. Upscaled or out-of-focus images score low.
    double sum = 0, sumSq = 0;
    int n = 0;
    for (int y = 1; y < 999; y += 2)
        for (int x = 1; x < 999; x += 2)
        {
            const float l = 4 * lum (x, y) - lum (x - 1, y) - lum (x + 1, y) - lum (x, y - 1) - lum (x, y + 1);
            sum += l; sumSq += (double) l * l; ++n;
        }
    r.sharpness = n > 0 ? (sumSq / n - (sum / n) * (sum / n)) * 10000.0 : 0;
    if (r.sharpness < 15.0)
        r.checks.push_back (warn ("Artwork", "The image looks blurry or upscaled. Use the original high-resolution file."));
    else
        r.checks.push_back (pass ("Artwork", "The image is sharp."));

    // Borders: a uniform strip along all four edges.
    auto edgeUniform = [&] (bool horizontal, int fixed)
    {
        float mn = 1, mx = 0;
        for (int k = 0; k < 1000; k += 4)
        {
            const float l = horizontal ? lum (k, fixed) : lum (fixed, k);
            mn = std::min (mn, l); mx = std::max (mx, l);
        }
        return mx - mn < 0.03f;
    };
    auto stripWidth = [&] (bool horizontal, bool fromStart)
    {
        int w = 0;
        for (int i = 0; i < 80; ++i)
        {
            const int fixed = fromStart ? i : 999 - i;
            if (! edgeUniform (horizontal, fixed)) break;
            ++w;
        }
        return w;
    };
    const int top = stripWidth (true, true), bottom = stripWidth (true, false), left = stripWidth (false, true), right = stripWidth (false, false);
    r.hasBorder = top >= 8 && bottom >= 8 && left >= 8 && right >= 8;
    if (r.hasBorder)
        r.checks.push_back (error ("Artwork", "The cover has a border. Stores reject artwork with borders; extend the image to the edges."));

    // Text: the image itself needs a person (or the future OCR model) to confirm; metadata and file name are checked here.
    if (looksLikeUrl (file.getFileName()) || looksLikePrice (file.getFileName()))
        r.checks.push_back (warn ("Artwork", "The file name mentions a website or price. Make sure the image doesn't show one."));
    r.checks.push_back (warn ("Artwork", "Confirm the cover shows no web addresses, social handles, prices, \"new single\" or store logos."));
    juce::ignoreUnused (m);
    return r;
}

juce::StringArray artTemplates() { return { "Gradient", "Photo", "Minimal", "Bold" }; }

juce::Image makeArtwork (const juce::String& tmpl, const juce::String& title, const juce::String& artist, juce::Colour base,
                         const juce::Image& photo, int size)
{
    juce::Image img (juce::Image::RGB, size, size, true, juce::SoftwareImageType());
    juce::Graphics g (img);
    const float s = (float) size;
    auto area = juce::Rectangle<float> (0, 0, s, s);

    if (tmpl == "Photo" && photo.isValid())
    {
        g.drawImage (photo, area, juce::RectanglePlacement::fillDestination);
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0, s * 0.5f, juce::Colours::black.withAlpha (0.75f), 0, s, false));
        g.fillRect (area);
    }
    else if (tmpl == "Minimal")
    {
        g.fillAll (base.withSaturation (0.25f).withBrightness (0.92f));
    }
    else if (tmpl == "Bold")
    {
        g.fillAll (base);
        g.setColour (base.contrasting (0.2f));
        for (int i = 0; i < 6; ++i)
            g.fillEllipse (s * (0.1f + 0.15f * (float) i), s * (0.55f - 0.07f * (float) i), s * 0.5f, s * 0.5f);
    }
    else // Gradient
    {
        g.setGradientFill (juce::ColourGradient (base.brighter (0.4f), 0, 0, base.darker (0.8f), s, s, false));
        g.fillRect (area);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillEllipse (s * 0.45f, s * -0.1f, s * 0.8f, s * 0.8f);
    }

    const bool darkText = tmpl == "Minimal";
    g.setColour (darkText ? juce::Colour (0xff1a1a1a) : juce::Colours::white);
    g.setFont (juce::FontOptions (s * 0.09f, juce::Font::bold));
    g.drawFittedText (title, juce::Rectangle<int> ((int) (s * 0.08f), (int) (s * 0.62f), (int) (s * 0.84f), (int) (s * 0.2f)),
                      juce::Justification::bottomLeft, 2);
    g.setFont (juce::FontOptions (s * 0.045f));
    g.drawFittedText (artist, juce::Rectangle<int> ((int) (s * 0.08f), (int) (s * 0.83f), (int) (s * 0.84f), (int) (s * 0.08f)),
                      juce::Justification::topLeft, 1);
    return img;
}

bool saveJpeg (const juce::Image& img, const juce::File& file, float quality)
{
    juce::JPEGImageFormat jpg;
    jpg.setQuality (quality);
    file.deleteFile();
    juce::FileOutputStream out (file);
    return out.openedOk() && jpg.writeImageToStream (img, out);
}

//==============================================================================
std::vector<Check> preReleaseChecklist (const Project& p, const juce::File& folder, const juce::AudioBuffer<float>& master, double sr)
{
    std::vector<Check> out;

    // Audio
    const auto s = LoudnessMeter::measure (master, sr);
    const float target = p.mastering.targetLufs;
    if (s.integratedLufs < -100)
        out.push_back (error ("Audio", "The master is silent."));
    else if (std::abs (s.integratedLufs - target) > 1.5f)
        out.push_back (warn ("Audio", juce::String::formatted ("Loudness is %.1f LUFS; the target is %.1f LUFS. Platforms will turn it %s by %.1f dB.",
                                                               (double) s.integratedLufs, (double) target,
                                                               s.integratedLufs > target ? "down" : "up", (double) std::abs (s.integratedLufs - target))));
    else
        out.push_back (pass ("Audio", juce::String::formatted ("Loudness %.1f LUFS (target %.1f).", (double) s.integratedLufs, (double) target)));

    if (s.truePeakDb > p.mastering.ceilingDbtp + 0.1f)
        out.push_back (error ("Audio", juce::String::formatted ("True peak is %.2f dBTP; keep it at or below %.1f dBTP to avoid distortion after encoding.",
                                                                (double) s.truePeakDb, (double) p.mastering.ceilingDbtp)));
    else
        out.push_back (pass ("Audio", juce::String::formatted ("True peak %.2f dBTP.", (double) s.truePeakDb)));

    const int clipped = analysis::countClippedSamples (master);
    if (clipped > 0) out.push_back (error ("Audio", juce::String (clipped) + " clipped samples. Lower the master or the mix."));
    else out.push_back (pass ("Audio", "No clipping."));

    const float lead = analysis::leadingSilenceSeconds (master, sr);
    const float trail = analysis::trailingSilenceSeconds (master, sr);
    if (lead > 2.0f) out.push_back (warn ("Audio", juce::String::formatted ("%.1f s of silence at the start. Stores prefer the song to start within 2 s.", (double) lead)));
    else out.push_back (pass ("Audio", "Start is tight."));
    if (trail > 5.0f) out.push_back (warn ("Audio", juce::String::formatted ("%.1f s of silence at the end. Trim it so playlists flow.", (double) trail)));
    else out.push_back (pass ("Audio", "Ending is tight."));

    // Metadata, artwork, lyrics
    auto meta = validateMetadata (p.release);
    out.insert (out.end(), meta.begin(), meta.end());

    if (p.release.artworkFile.isNotEmpty())
    {
        auto art = checkArtwork (store::resolve (folder, p.release.artworkFile), p.release);
        out.insert (out.end(), art.checks.begin(), art.checks.end());
    }
    else
        out.push_back (error ("Artwork", "No cover art yet."));

    if (p.release.lyrics.trim().isEmpty())
        out.push_back (warn ("Metadata", "No lyrics. Add them so platforms can show them (and sync them for live lyrics)."));
    else if (p.release.syncedLyrics.empty())
        out.push_back (warn ("Metadata", "Lyrics are not time-synced yet. Sync them for platforms with live lyrics."));
    else
        out.push_back (pass ("Metadata", "Lyrics are synced."));

    // Artist name consistency with the project and the lyric sheet.
    if (p.release.primaryArtist.isNotEmpty())
        for (auto& c : p.release.splits)
            if (c.name.removeCharacters (" ").equalsIgnoreCase (p.release.primaryArtist.removeCharacters (" ")) && c.name != p.release.primaryArtist)
                out.push_back (warn ("Metadata", "\"" + c.name + "\" in splits is spelled differently from the artist name \"" + p.release.primaryArtist + "\"."));

    return out;
}

//==============================================================================
juce::String toLrc (const ReleaseMetadata& m)
{
    juce::String s;
    s << "[ti:" << m.title << "]\n[ar:" << m.primaryArtist << "]\n";
    for (auto& l : m.syncedLyrics)
    {
        const int cs = (int) std::round (l.timeSeconds * 100.0);
        s << juce::String::formatted ("[%02d:%02d.%02d]", cs / 6000, (cs / 100) % 60, cs % 100) << l.text << "\n";
    }
    return s;
}

std::vector<SyncedLyricLine> fromLrc (const juce::String& lrc)
{
    std::vector<SyncedLyricLine> out;
    for (auto& line : juce::StringArray::fromLines (lrc))
    {
        auto l = line.trim();
        if (! l.startsWith ("[") || l.length() < 10 || ! juce::CharacterFunctions::isDigit (l[1])) continue;
        auto tag = l.substring (1, l.indexOfChar (']'));
        auto mins = tag.upToFirstOccurrenceOf (":", false, false).getIntValue();
        auto secs = tag.fromFirstOccurrenceOf (":", false, false).getDoubleValue();
        out.push_back ({ mins * 60 + secs, l.fromFirstOccurrenceOf ("]", false, false).trim() });
    }
    return out;
}

juce::String metadataSheet (const Project& p, const LoudnessMeter::Summary& loud)
{
    const auto& m = p.release;
    auto list = [] (const juce::StringArray& a) { return a.isEmpty() ? juce::String ("-") : a.joinIntoString (", "); };
    juce::String s;
    s << "RELEASE METADATA\n================\n\n"
      << "Release type:           " << m.releaseType << "\n"
      << "Song title:             " << m.title << "\n"
      << "Primary artist:         " << m.primaryArtist << "\n"
      << "Featured artists:       " << list (m.featuredArtists) << "\n"
      << "Songwriters:            " << list (m.songwriters) << "\n"
      << "Composers:              " << list (m.composers) << "\n"
      << "Producers:              " << list (m.producers) << "\n"
      << "Genre / subgenre:       " << m.genre << (m.subgenre.isNotEmpty() ? " / " + m.subgenre : juce::String()) << "\n"
      << "Language:               " << m.language << "\n"
      << "Explicit:               " << (m.explicitContent ? "Yes" : "No") << "\n"
      << "Release date:           " << m.releaseDate << "\n"
      << "Original release date:  " << (m.originalReleaseDate.isNotEmpty() ? m.originalReleaseDate : juce::String ("-")) << "\n"
      << "ISRC:                   " << (m.isrc.isNotEmpty() ? m.isrc : juce::String ("(assigned by distributor)")) << "\n"
      << "UPC:                    " << (m.upc.isNotEmpty() ? m.upc : juce::String ("(assigned by distributor)")) << "\n"
      << "Label:                  " << (m.label.isNotEmpty() ? m.label : juce::String ("-")) << "\n"
      << "Copyright:              " << m.copyrightLine << "\n\n"
      << "AUDIO\n-----\n"
      << "Tempo / key:            " << juce::String (p.bpm, 1) << " BPM, " << p.key.toString() << "\n"
      << juce::String::formatted ("Loudness:               %.1f LUFS integrated, %.2f dBTP true peak, LRA %.1f LU\n\n",
                                  (double) loud.integratedLufs, (double) loud.truePeakDb, (double) loud.loudnessRange)
      << "SPLITS\n------\n";
    for (auto& sp : m.splits)
        s << "  " << sp.name << " (" << sp.role << "): " << juce::String (sp.percent, 2) << "%" << (sp.email.isNotEmpty() ? "  <" + sp.email + ">" : juce::String()) << "\n";
    if (m.splits.empty()) s << "  -\n";
    return s;
}

PackageResult writeExportPackage (const Project& p, const juce::File& folder, const juce::File& outParent,
                                  std::function<bool (float, const juce::String&)> progress)
{
    PackageResult r;
    const auto base = exporting::baseName (p);
    r.folder = outParent.getChildFile (base + " - Upload package").getNonexistentSibling();
    r.folder.createDirectory();

    exporting::Options o;
    o.masterWav = true;
    o.flac = false; o.mp3 = true; o.instrumental = false; o.acappella = false; o.midi = false;
    auto ex = exporting::exportAll (p, folder, r.folder, o, progress);
    r.errors.addArray (ex.errors);

    // Checklist on the exported master.
    juce::AudioBuffer<float> master;
    double rate = 44100;
    ClipCache::readFile (r.folder.getChildFile (base + ".wav"), master, rate);
    r.checks = preReleaseChecklist (p, folder, master, rate);

    if (p.release.artworkFile.isNotEmpty())
    {
        auto art = store::resolve (folder, p.release.artworkFile);
        art.copyFileTo (r.folder.getChildFile (base + " - Cover" + art.getFileExtension()));
    }

    r.folder.getChildFile ("Metadata.txt").replaceWithText (metadataSheet (p, ex.masterLoudness));
    r.folder.getChildFile ("metadata.json").replaceWithText (juce::JSON::toString (p.toVar().getProperty ("release", {}), false));

    // CSV in the column order most distributors' bulk upload sheets use.
    auto csvEscape = [] (const juce::String& v) { return "\"" + v.replace ("\"", "\"\"") + "\""; };
    juce::StringArray header { "Title", "Primary Artist", "Featured Artists", "Songwriters", "Composers", "Producers", "Genre", "Subgenre",
                               "Language", "Explicit", "Release Date", "Original Release Date", "ISRC", "UPC", "Label", "Copyright" };
    const auto& m = p.release;
    juce::StringArray row { m.title, m.primaryArtist, m.featuredArtists.joinIntoString ("; "), m.songwriters.joinIntoString ("; "),
                            m.composers.joinIntoString ("; "), m.producers.joinIntoString ("; "), m.genre, m.subgenre, m.language,
                            m.explicitContent ? "Yes" : "No", m.releaseDate, m.originalReleaseDate, m.isrc, m.upc, m.label, m.copyrightLine };
    for (auto& v : row) v = csvEscape (v);
    r.folder.getChildFile ("Metadata.csv").replaceWithText (header.joinIntoString (",") + "\n" + row.joinIntoString (",") + "\n");

    if (m.lyrics.isNotEmpty()) r.folder.getChildFile ("Lyrics.txt").replaceWithText (m.lyrics);
    if (! m.syncedLyrics.empty()) r.folder.getChildFile ("Lyrics.lrc").replaceWithText (toLrc (m));

    juce::String report = "PRE-RELEASE CHECKLIST\n=====================\n\n";
    for (auto& c : r.checks)
        report << (c.level == Check::Level::Pass ? "[ok]   " : c.level == Check::Level::Warning ? "[warn] " : "[FIX]  ") << c.area << ": " << c.message << "\n";
    report << "\nUPLOAD\n------\nUpload the WAV, cover and the details in Metadata.txt to your distributor:\n";
    for (auto& d : distributorsForManualUpload()) report << "  - " << d << "\n";
    r.folder.getChildFile ("Checklist.txt").replaceWithText (report);

    // Zip everything for easy transfer.
    r.zip = outParent.getChildFile (r.folder.getFileName() + ".zip");
    r.zip.deleteFile();
    juce::ZipFile::Builder zb;
    for (auto& f : r.folder.findChildFiles (juce::File::findFiles, true))
        zb.addFile (f, 6, f.getRelativePathFrom (r.folder));
    juce::FileOutputStream zo (r.zip);
    if (zo.openedOk()) zb.writeToStream (zo, nullptr);
    return r;
}

//==============================================================================
juce::Result writeSmartLinkPage (const Project& p, const juce::File& folder, const std::vector<StoreLink>& links, const juce::File& out, bool preSave)
{
    out.createDirectory();
    juce::String artName;
    if (p.release.artworkFile.isNotEmpty())
    {
        auto art = store::resolve (folder, p.release.artworkFile);
        auto img = juce::ImageFileFormat::loadFrom (art);
        if (img.isValid())
        {
            artName = "cover.jpg";
            saveJpeg (img.rescaled (1000, 1000, juce::Graphics::highResamplingQuality), out.getChildFile (artName), 0.88f);
        }
    }
    auto esc = [] (const juce::String& s) { return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;"); };
    const auto title = esc (p.release.title.isNotEmpty() ? p.release.title : p.name);
    const auto artist = esc (p.release.primaryArtist);

    juce::String html;
    html << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         << "<title>" << title << " - " << artist << "</title>"
         << "<meta property=\"og:title\" content=\"" << title << " - " << artist << "\">"
         << (artName.isNotEmpty() ? "<meta property=\"og:image\" content=\"" + artName + "\">" : juce::String())
         << "<style>body{margin:0;font-family:system-ui,sans-serif;background:#111;color:#fff;display:flex;justify-content:center}"
         << "main{max-width:420px;width:100%;padding:24px 16px}img{width:100%;border-radius:12px}h1{margin:16px 0 4px;font-size:24px}"
         << "p{margin:0 0 20px;color:#aaa}a{display:flex;justify-content:space-between;align-items:center;padding:14px 16px;margin:8px 0;"
         << "background:#1e1e24;border-radius:10px;color:#fff;text-decoration:none}a span{color:#8ab4ff}</style></head><body><main>"
         << (artName.isNotEmpty() ? "<img src=\"" + artName + "\" alt=\"Cover art\">" : juce::String())
         << "<h1>" << title << "</h1><p>" << artist
         << (preSave && p.release.releaseDate.isNotEmpty() ? " &middot; out " + esc (p.release.releaseDate) : juce::String()) << "</p>";
    for (auto& l : links)
        html << "<a href=\"" << esc (l.url) << "\">" << esc (l.store) << "<span>" << (preSave ? "Pre-save" : "Listen") << "</span></a>";
    html << "</main></body></html>\n";
    return out.getChildFile ("index.html").replaceWithText (html) ? juce::Result::ok() : juce::Result::fail ("Could not write the page.");
}

std::vector<EarningsRow> parseEarningsCsv (const juce::File& csv)
{
    std::vector<EarningsRow> rows;
    auto lines = juce::StringArray::fromLines (csv.loadFileAsString());
    lines.removeEmptyStrings();
    if (lines.size() < 2) return rows;

    auto split = [] (const juce::String& line)
    {
        juce::StringArray cells;
        juce::String cur;
        bool quoted = false;
        for (int i = 0; i < line.length(); ++i)
        {
            auto c = line[i];
            if (c == '"') { if (quoted && i + 1 < line.length() && line[i + 1] == '"') { cur << '"'; ++i; } else quoted = ! quoted; }
            else if ((c == ',' || c == ';' || c == '\t') && ! quoted) { cells.add (cur.trim()); cur.clear(); }
            else cur << juce::String::charToString (c);
        }
        cells.add (cur.trim());
        return cells;
    };

    auto header = split (lines[0]);
    auto find = [&] (std::initializer_list<const char*> names)
    {
        for (int i = 0; i < header.size(); ++i)
            for (auto n : names)
                if (header[i].containsIgnoreCase (n)) return i;
        return -1;
    };
    const int cStore = find ({ "store", "platform", "service", "dsp" });
    const int cStreams = find ({ "streams", "quantity", "units", "plays" });
    const int cEarn = find ({ "earnings", "revenue", "amount", "royalt", "net" });
    const int cPeriod = find ({ "period", "month", "date", "reporting" });
    const int cCountry = find ({ "country", "territory" });

    for (int i = 1; i < lines.size(); ++i)
    {
        auto cells = split (lines[i]);
        EarningsRow r;
        if (cStore >= 0) r.store = cells[cStore];
        if (cStreams >= 0) r.streams = cells[cStreams].removeCharacters (",").getLargeIntValue();
        if (cEarn >= 0) r.earnings = cells[cEarn].removeCharacters (",$\xe2\x82\xa6").getDoubleValue();
        if (cPeriod >= 0) r.period = cells[cPeriod];
        if (cCountry >= 0) r.country = cells[cCountry];
        rows.push_back (r);
    }
    return rows;
}

juce::StringArray afterReleaseReminders (const Project& p)
{
    juce::StringArray r;
    r.add ("Register the song with your collecting society so you are paid for radio, TV, church broadcasts and public performance "
           "(in Nigeria: COSON). Use the same title, writers and splits as the release.");
    r.add ("Sign up with a publishing administrator to collect mechanical and international songwriter royalties.");
    if (! p.release.splits.empty())
        r.add ("Send every co-writer the final split sheet and keep a signed copy.");
    r.add ("Claim your artist profiles (Spotify for Artists, Apple Music for Artists, Audiomack, Boomplay) and pitch the song to playlists.");
    r.add ("Upload the lyric video and performance track for your church and live team.");
    return r;
}

juce::StringArray distributorsForManualUpload() { return { "DistroKid", "TuneCore", "Amuse", "CD Baby", "Ditto", "ONErpm", "Audiomack (direct)", "Boomplay (direct)" }; }

juce::StringArray genres()
{
    return { "Christian & Gospel", "Gospel", "Worship", "Afrobeats", "Afropop", "Highlife", "Amapiano", "R&B/Soul", "Hip-Hop/Rap",
             "Reggae", "Dancehall", "Pop", "Inspirational", "Spoken Word" };
}

juce::StringArray languages()
{
    return { "English", "Yoruba", "Igbo", "Hausa", "Nigerian Pidgin", "Efik", "Twi", "Swahili", "Zulu", "French", "Portuguese", "Instrumental" };
}

} // namespace studio::release
