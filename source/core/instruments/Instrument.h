#pragma once

#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>

namespace studio
{

/** A playable instrument. The engine splits each audio block at MIDI event
    positions and calls noteOn/noteOff between render() calls, so events are
    sample-accurate. All methods are called on the audio thread and must not
    allocate (prepare() excepted). */
class Instrument
{
public:
    virtual ~Instrument() = default;

    virtual juce::String getId() const = 0;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void noteOn (int note, float velocity) = 0;
    virtual void noteOff (int note) = 0;
    virtual void allNotesOff (bool immediately) = 0;
    virtual void setPitchBend (float /*semitones*/) {}
    virtual void setSustain (bool /*down*/) {}
    virtual void setModWheel (float /*value*/) {}

    /** Adds output into the buffer (stereo) over [start, start + num). */
    virtual void render (juce::AudioBuffer<float>& out, int start, int num) = 0;

    /** Number of voices currently sounding (for the CPU meter). */
    virtual int getActiveVoices() const { return 0; }

    /** Parameters the instrument exposes (name -> value), applied off the audio thread. */
    virtual void setParameter (const juce::String& /*name*/, float /*value*/) {}
};

} // namespace studio
