#pragma once

#include <vector>
#include "Instrument.h"
#include "../model/Project.h"

namespace studio
{

struct InstrumentInfo
{
    juce::String id;
    juce::String name;
    juce::String family;        // Keys, Bass, Guitars, Drums, African percussion, Strings and brass, Pads and synths, Choir
    juce::String description;
    TrackRole role = TrackRole::Unknown;
    bool isDrumKit = false;     // uses GM drum note numbers
    bool starter = true;        // ships in the core install (synthesised); false = needs a downloaded pack
};

namespace instruments
{
    /** Every built-in starter instrument, grouped by family. */
    const std::vector<InstrumentInfo>& catalogue();
    const InstrumentInfo* find (const juce::String& id);
    juce::StringArray families();

    /** Creates an instrument by id. Handles built-ins and "sampler:<pack>/<instrument>"
        ids (sample packs on disk). Returns a basic piano if the id is unknown. */
    std::unique_ptr<Instrument> create (const juce::String& id);

    /** GM drum note numbers used by kits and the beat generator. */
    namespace gm
    {
        enum : int
        {
            kick = 36, rim = 37, snare = 38, clap = 39, snare2 = 40, lowTom = 41, closedHat = 42,
            midLowTom = 45, pedalHat = 44, openHat = 46, midTom = 47, highTom = 48, crash = 49, ride = 51,
            shaker = 70, shekere = 69, congaHighMute = 62, congaHigh = 63, congaLow = 64,
            djembeBass = 60, djembeTone = 61, djembeSlap = 65, agogoHigh = 67, agogoLow = 68,
            talkingDrumLow = 71, talkingDrumHigh = 72, udu = 73, tambourine = 54
        };
    }
}

} // namespace studio
