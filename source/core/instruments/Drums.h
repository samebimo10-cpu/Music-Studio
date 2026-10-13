#pragma once

#include <array>
#include <map>
#include "Instrument.h"

namespace studio
{

/** One synthesised drum or percussion sound. */
struct DrumPiece
{
    enum class Type { Kick, Snare, HiHat, Clap, Tom, Cymbal, Rim, Shaker, Shekere, Membrane, Bell, Udu, LogDrum };

    Type type = Type::Kick;
    juce::String name;
    float freq = 55;           // fundamental (Hz); pitched instruments override per note
    float decay = 0.4f;        // seconds
    float pitchDrop = 12;      // semitones at the attack (negative = pitch rises, e.g. talking drum)
    float pitchDecay = 0.04f;  // seconds
    float noise = 0.2f;        // noise / snare-wire amount
    float tone = 0.5f;         // click or body balance
    float brightness = 0.5f;
    float drive = 1.0f;        // saturation for punch
    float level = 0.8f;
    float pan = 0;
    int chokeGroup = 0;        // pieces in the same non-zero group cut each other (open/closed hat)
    bool pitched = false;      // pitch follows the MIDI note
};

/** A drum kit or percussion instrument: MIDI note -> piece. Uses General MIDI
    drum numbers so MIDI files and pads work without setup. */
class DrumKit : public Instrument
{
public:
    DrumKit (juce::String id, std::map<int, DrumPiece> pieces);

    juce::String getId() const override { return kitId; }
    void prepare (double sampleRate, int maxBlockSize) override;
    void noteOn (int note, float velocity) override;
    void noteOff (int) override {}
    void allNotesOff (bool immediately) override;
    void render (juce::AudioBuffer<float>& out, int start, int num) override;
    int getActiveVoices() const override;

    const std::map<int, DrumPiece>& getPieces() const noexcept { return pieces; }

    /** Pitched percussion (talking drum, log drum, udu): every note plays the piece at that pitch. */
    static std::unique_ptr<DrumKit> pitched (juce::String id, DrumPiece piece);

private:
    struct Svf
    {
        float low = 0, band = 0;
        float process (float x, float g, float k, float& bandOut, float& highOut) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float hp = (x - (k + g) * band - low) * a1;
            const float bp = g * hp + band;
            const float lp = g * bp + low;
            band = g * hp + bp;
            low = g * bp + lp;
            bandOut = bp;
            highOut = hp;
            return lp;
        }
    };

    struct Voice
    {
        const DrumPiece* piece = nullptr;
        float velocity = 0;
        double t = 0;
        double freq = 55;
        std::array<double, 6> phases {};
        Svf f1, f2;
        float last = 0;
        bool active = false;
        juce::uint32 age = 0;
    };

    float renderVoice (Voice& v, double dt) noexcept;
    float noise() noexcept
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) ((seed >> 9) * (1.0 / 4194304.0)) - 1.0f;
    }

    juce::String kitId;
    std::map<int, DrumPiece> pieces;
    std::array<Voice, 32> voices;
    double sr = 48000;
    juce::uint32 seed = 987654321u, ageCounter = 0;
    bool allPitched = false;
    DrumPiece pitchedPiece;
};

} // namespace studio
