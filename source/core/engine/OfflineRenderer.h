#pragma once

#include <functional>
#include "MixGraph.h"

namespace studio
{

struct RenderRequest
{
    double sampleRate = 48000;
    int blockSize = 1024;
    bool includeMastering = true;
    double startBeat = 0;
    double endBeat = -1;            // -1 = end of the song
    double tailSeconds = 2.5;       // let reverbs and releases ring out
    bool includeClick = false;
    bool clickOnLeftOnly = false;   // performance tracks: click/cues left, music right
    std::function<bool (const Track&)> trackFilter;
    juce::File projectFolder;
};

/** Renders a project faster than real time with the same graph as playback. */
class OfflineRenderer
{
public:
    /** progress(0..1) may return false to cancel; returns an empty buffer when cancelled. */
    static juce::AudioBuffer<float> render (const Project& project, const RenderRequest& request,
                                            std::function<bool (float)> progress = {});

    /** Renders one track (pre-master, post-fader) - used for bounces and analysis. */
    static juce::AudioBuffer<float> renderTrack (const Project& project, const juce::String& trackId,
                                                 const RenderRequest& request);
};

} // namespace studio
