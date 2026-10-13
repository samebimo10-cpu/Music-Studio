#pragma once

#include <array>
#include "Instrument.h"
#include "../dsp/DspCommon.h"

namespace studio
{

/** Describes a synthesised starter instrument. One engine covers the families
    in the spec: subtractive (bass, brass, pads, leads), FM (electric pianos,
    bells), modal/additive (piano, organ drawbars), plucked strings (Karplus-
    Strong guitars) and formant (choir). */
struct SynthPatch
{
    enum class Mode { Subtractive, FM, Modal, Organ, Pluck, Formant };
    enum class Wave { Sine, Saw, Square, Triangle, Noise, Pulse };

    struct Osc
    {
        Wave wave = Wave::Saw;
        float level = 1.0f;
        float detuneCents = 0;
        int octave = 0;
    };

    struct Adsr { float a = 0.005f, d = 0.3f, s = 0.7f, r = 0.3f; };

    juce::String name;
    Mode mode = Mode::Subtractive;
    std::array<Osc, 3> osc { Osc { Wave::Saw, 1.0f, 0, 0 }, Osc { Wave::Saw, 0.0f, 0, 0 }, Osc { Wave::Sine, 0.0f, 0, 0 } };
    int unison = 1;                 // voices per note for pads/strings
    float unisonDetune = 10;        // cents
    float noiseLevel = 0;

    // Filter (state-variable low-pass)
    float cutoff = 8000, resonance = 0.2f, filterEnvAmount = 0, keyTrack = 0.5f, velToCutoff = 0.3f;
    Adsr ampEnv, filterEnv { 0.005f, 0.3f, 0.0f, 0.3f };

    // Pitch envelope (808s, toms, plucks) and vibrato
    float pitchEnvSemis = 0, pitchEnvDecay = 0.05f;
    float vibratoRate = 5.0f, vibratoDepth = 0, vibratoDelay = 0.3f;

    // FM: modulator ratio, index and how fast the index decays (bell/tine)
    float fmRatio = 1.0f, fmIndex = 0, fmIndexDecay = 1.0f, fmVelToIndex = 0.5f;

    // Modal (piano family): partial count, inharmonicity, brightness, decay, hammer noise
    int partials = 12;
    float inharmonicity = 0.0004f, brightness = 0.6f, decaySeconds = 4.0f, hammer = 0.1f;

    // Organ drawbars (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1') and rotary speaker
    std::array<float, 9> drawbars { 0.8f, 0.6f, 1.0f, 0.5f, 0, 0.3f, 0, 0, 0.2f };
    float leslieRate = 0;           // Hz; 0 = off
    float percussion = 0;

    // Pluck (Karplus-Strong)
    float pluckDamping = 0.5f, pluckBrightness = 0.7f, pickPosition = 0.2f, mute = 0;

    // Formant (choir): vowel morph 0 = "ah", 1 = "oo"
    float vowel = 0;

    float gain = 0.5f;
    float stereoSpread = 0.3f;
    float velocitySensitivity = 0.8f;
    bool mono = false;
    float glide = 0;                // seconds
    int maxVoices = 24;
};

/** Polyphonic voice engine that plays a SynthPatch. */
class SynthInstrument : public Instrument
{
public:
    SynthInstrument (juce::String id, SynthPatch patch);

    juce::String getId() const override { return instrumentId; }
    void prepare (double sampleRate, int maxBlockSize) override;
    void noteOn (int note, float velocity) override;
    void noteOff (int note) override;
    void allNotesOff (bool immediately) override;
    void setPitchBend (float semitones) override { bend = semitones; }
    void setSustain (bool down) override;
    void render (juce::AudioBuffer<float>& out, int start, int num) override;
    int getActiveVoices() const override;
    void setParameter (const juce::String& name, float value) override;

    const SynthPatch& getPatch() const noexcept { return patch; }

private:
    struct Env
    {
        enum class Stage { Idle, Attack, Decay, Sustain, Release };
        Stage stage = Stage::Idle;
        float value = 0;
        float attackRate = 0, decayCoeff = 0, releaseCoeff = 0, sustain = 0;
        void set (const SynthPatch::Adsr& a, double sr);
        void trigger() { stage = Stage::Attack; }
        void release() { if (stage != Stage::Idle) stage = Stage::Release; }
        float next();
    };

    struct Voice
    {
        int note = -1;
        float velocity = 0;
        bool held = false, sustained = false;
        juce::uint32 age = 0;
        double freq = 440, targetFreq = 440;
        Env amp, filt;
        std::array<double, 8> phases {};       // oscillator / unison phases
        double fmPhase = 0, vibPhase = 0, time = 0;
        float svfLow = 0, svfBand = 0, svfLow2 = 0, svfBand2 = 0;
        float pan = 0;
        // Modal partials as rotating phasors
        std::array<float, 24> pRe {}, pIm {}, pCos {}, pSin {}, pAmp {}, pDecay {};
        int numPartials = 0;
        // Pluck delay line
        std::vector<float> ks;
        int ksPos = 0;
        float ksLen = 100, ksLast = 0;
        float noiseState = 0;
        std::array<float, 6> formantState {};
    };

    float renderVoice (Voice& v, float& outR) noexcept;
    void startVoice (Voice& v, int note, float velocity);
    float oscillator (SynthPatch::Wave w, double& phase, double inc) noexcept;

    juce::String instrumentId;
    SynthPatch patch;
    std::vector<Voice> voices;
    double sr = 48000;
    float bend = 0;
    bool sustainDown = false;
    juce::uint32 ageCounter = 0;
    juce::Random rng { 1234 };
    double lesliePhase = 0;
    int lastMonoNote = -1;
    juce::uint32 noiseSeed = 22222;
};

} // namespace studio
