#pragma once

#include <functional>
#include "../model/Project.h"
#include "../analysis/LoudnessMeter.h"

namespace studio::exporting
{

/** Every file a release needs (spec, Module 5). */
struct Options
{
    bool masterWav = true;          // WAV 24-bit, 44.1 or 48 kHz - upload to the distributor
    double sampleRate = 44100;      // 44100 or 48000
    bool flac = true;               // lossless archive
    bool mp3 = true;                // 320 kbps preview (needs ffmpeg or lame)
    bool aac = false;               // AAC preview (needs ffmpeg)
    bool instrumental = true;       // vocals muted
    bool acappella = true;          // vocals only
    bool stems = false;             // one WAV per bus
    bool performanceTrack = false;  // click + cues left, music right
    bool midi = true;
    bool lyricVideo = false;        // MP4 with synced lyrics over artwork (needs ffmpeg)
    bool applyMastering = true;
};

struct Result
{
    juce::Array<juce::File> files;
    juce::StringArray warnings;
    juce::StringArray errors;
    LoudnessMeter::Summary masterLoudness;
};

/** Finds an external encoder (ffmpeg, lame) on PATH or next to the app. */
juce::File findTool (const juce::String& name);

/** Renders and writes everything requested into `outFolder`. progress(0..1, label). */
Result exportAll (const Project& project, const juce::File& projectFolder, const juce::File& outFolder, const Options& options,
                  std::function<bool (float, const juce::String&)> progress = {});

/** Encodes a WAV to MP3 320 (ffmpeg or lame) / AAC 256 (ffmpeg). */
juce::Result encodeMp3 (const juce::File& wav, const juce::File& mp3, const juce::StringPairArray& tags = {});
juce::Result encodeAac (const juce::File& wav, const juce::File& m4a, const juce::StringPairArray& tags = {});

/** Base file name for exports: "Artist - Title". */
juce::String baseName (const Project& p);

/** Lyric video: one frame per lyric line over the artwork, muxed with the audio by ffmpeg. */
juce::Result renderLyricVideo (const Project& p, const juce::File& projectFolder, const juce::File& audioFile, const juce::File& mp4,
                               int width = 1920, int height = 1080);

//==============================================================================
/** Album mastering: orders songs, matches loudness and sets the gaps between tracks. */
struct AlbumTrack
{
    juce::File masterFile;      // a mastered WAV
    juce::String title;
    double gapAfterSeconds = 2.0;
    float trimDb = 0;           // set by matchAlbumLoudness
    LoudnessMeter::Summary loudness;
};

/** Measures every track and sets trims so they sit within +-0.5 LU of the target,
    without pushing peaks over the ceiling. */
void matchAlbumLoudness (std::vector<AlbumTrack>& tracks, float targetLufs = -14.0f, float ceilingDbtp = -1.0f);

/** Writes the sequenced album: numbered track files (gaps included) and a continuous WAV with a CUE sheet. */
Result writeAlbum (const std::vector<AlbumTrack>& tracks, const juce::File& outFolder, double sampleRate = 44100);

} // namespace studio::exporting
