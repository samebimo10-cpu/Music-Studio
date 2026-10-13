#pragma once

#include <juce_graphics/juce_graphics.h>
#include "../model/Project.h"
#include "../analysis/LoudnessMeter.h"

namespace studio::release
{

struct Check
{
    enum class Level { Pass, Warning, Error };
    Level level = Level::Pass;
    juce::String area;      // "Metadata", "Artwork", "Audio", "Splits", "Codes"
    juce::String message;
};

bool hasErrors (const std::vector<Check>& checks);

// ---- codes ----------------------------------------------------------------------
/** ISRC: CC-XXX-YY-NNNNN (12 characters; hyphens optional). */
bool isValidIsrc (const juce::String& isrc);
juce::String normaliseIsrc (const juce::String& isrc);
/** UPC-A (12 digits) or EAN-13 (13 digits) with a valid check digit. */
bool isValidUpc (const juce::String& upc);

// ---- metadata ---------------------------------------------------------------------
std::vector<Check> validateMetadata (const ReleaseMetadata& m);
std::vector<Check> validateSplits (const std::vector<Split>& splits);

// ---- artwork -------------------------------------------------------------------------
struct ArtworkReport
{
    int width = 0, height = 0;
    double sharpness = 0;     // Laplacian variance (higher = sharper)
    bool hasBorder = false;
    std::vector<Check> checks;
};
ArtworkReport checkArtwork (const juce::File& imageFile, const ReleaseMetadata& m);

/** Art maker: renders a 3000x3000 cover from a template. */
juce::StringArray artTemplates(); // "Gradient", "Photo", "Minimal", "Bold"
juce::Image makeArtwork (const juce::String& templateName, const juce::String& title, const juce::String& artist,
                         juce::Colour base, const juce::Image& photo = {}, int size = 3000);
bool saveJpeg (const juce::Image& img, const juce::File& file, float quality = 0.92f);

// ---- pre-release checklist --------------------------------------------------------------
std::vector<Check> preReleaseChecklist (const Project& p, const juce::File& projectFolder,
                                        const juce::AudioBuffer<float>& master, double sampleRate);

// ---- lyrics -----------------------------------------------------------------------------
juce::String toLrc (const ReleaseMetadata& m);
std::vector<SyncedLyricLine> fromLrc (const juce::String& lrc);

// ---- export package (distribution option 1) ----------------------------------------------
/** Writes the upload package for an existing distributor: master WAV, artwork,
    metadata sheet (TXT, CSV, JSON), lyrics (TXT, LRC) and the checklist report, plus a zip. */
struct PackageResult
{
    juce::File folder, zip;
    std::vector<Check> checks;
    juce::StringArray errors;
};
PackageResult writeExportPackage (const Project& p, const juce::File& projectFolder, const juce::File& outParent,
                                  std::function<bool (float, const juce::String&)> progress = {});

/** Human-readable metadata sheet laid out in the order distributors ask for it. */
juce::String metadataSheet (const Project& p, const LoudnessMeter::Summary& loudness);

// ---- after release -------------------------------------------------------------------------
struct StoreLink { juce::String store, url; };

/** A static pre-save / smart-link page (index.html with artwork) to host anywhere. */
juce::Result writeSmartLinkPage (const Project& p, const juce::File& projectFolder, const std::vector<StoreLink>& links,
                                 const juce::File& outFolder, bool preSave);

/** Streams and earnings from a distributor's CSV report (columns found by header name). */
struct EarningsRow { juce::String period, store, country; juce::int64 streams = 0; double earnings = 0; };
std::vector<EarningsRow> parseEarningsCsv (const juce::File& csv);

/** Reminders after release: collecting society (e.g. COSON in Nigeria), publishing admin, splits. */
juce::StringArray afterReleaseReminders (const Project& p);

juce::StringArray distributorsForManualUpload(); // DistroKid, TuneCore, Amuse, CD Baby...
juce::StringArray genres();
juce::StringArray languages();

} // namespace studio::release
