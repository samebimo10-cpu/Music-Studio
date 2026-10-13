#include "InstrumentLibrary.h"
#include "Synth.h"
#include "Drums.h"
#include "SamplePack.h"

namespace studio::instruments
{

namespace
{
    using W = SynthPatch::Wave;
    using M = SynthPatch::Mode;
    using T = DrumPiece::Type;

    SynthPatch piano (float brightness, float decay, float inharm, float hammer, float gain = 0.35f)
    {
        SynthPatch p;
        p.mode = M::Modal;
        p.partials = 16;
        p.brightness = brightness;
        p.decaySeconds = decay;
        p.inharmonicity = inharm;
        p.hammer = hammer;
        p.ampEnv = { 0.001f, 10.0f, 1.0f, 0.25f };
        p.gain = gain;
        p.stereoSpread = 0.5f;
        p.maxVoices = 32;
        return p;
    }

    SynthPatch fmKeys (float ratio, float index, float indexDecay, float decay, float gain = 0.3f)
    {
        SynthPatch p;
        p.mode = M::FM;
        p.fmRatio = ratio;
        p.fmIndex = index;
        p.fmIndexDecay = indexDecay;
        p.fmVelToIndex = 0.7f;
        p.osc[1] = { W::Sine, 0.15f, 0, 1 };
        p.ampEnv = { 0.002f, decay, 0.0f, 0.35f };
        p.cutoff = 12000;
        p.gain = gain;
        p.stereoSpread = 0.6f;
        return p;
    }

    SynthPatch subtractive (W w1, W w2, float w2Level, float detune, float cutoff, float res, float envAmt,
                            SynthPatch::Adsr amp, SynthPatch::Adsr filt, float gain)
    {
        SynthPatch p;
        p.mode = M::Subtractive;
        p.osc[0] = { w1, 1.0f, 0, 0 };
        p.osc[1] = { w2, w2Level, detune, 0 };
        p.cutoff = cutoff;
        p.resonance = res;
        p.filterEnvAmount = envAmt;
        p.ampEnv = amp;
        p.filterEnv = filt;
        p.gain = gain;
        return p;
    }

    SynthPatch pluck (float damping, float brightness, float pick, float mute, float gain = 0.5f)
    {
        SynthPatch p;
        p.mode = M::Pluck;
        p.pluckDamping = damping;
        p.pluckBrightness = brightness;
        p.pickPosition = pick;
        p.mute = mute;
        p.cutoff = 9000;
        p.keyTrack = 0.3f;
        p.ampEnv = { 0.001f, 8.0f, 1.0f, mute > 0 ? 0.05f : 0.15f };
        p.gain = gain;
        return p;
    }

    SynthPatch bass (W w, float cutoff, float envAmt, float decay, float gain, bool mono = true)
    {
        auto p = subtractive (w, W::Sine, 0.6f, 0, cutoff, 0.15f, envAmt, { 0.002f, decay, 0.6f, 0.08f }, { 0.001f, 0.15f, 0.0f, 0.1f }, gain);
        p.osc[1].octave = -1;
        p.mono = mono;
        p.keyTrack = 0.2f;
        p.stereoSpread = 0;
        return p;
    }

    // ---- drum pieces -----------------------------------------------------------
    DrumPiece piece (T type, const char* name, float freq, float decay, float drop, float pitchDecay,
                     float noise, float tone, float bright, float level, float pan = 0, int choke = 0, float drive = 1.0f)
    {
        DrumPiece d;
        d.type = type; d.name = name; d.freq = freq; d.decay = decay; d.pitchDrop = drop; d.pitchDecay = pitchDecay;
        d.noise = noise; d.tone = tone; d.brightness = bright; d.level = level; d.pan = pan; d.chokeGroup = choke; d.drive = drive;
        return d;
    }

    std::map<int, DrumPiece> baseKit (float kickFreq, float kickDecay, float kickDrop, float snareFreq, float snareNoise,
                                      float hatBright, float kickDrive)
    {
        std::map<int, DrumPiece> k;
        k[gm::kick]       = piece (T::Kick,  "Kick",        kickFreq, kickDecay, kickDrop, 0.035f, 0, 0.4f, 0.5f, 1.0f, 0, 0, kickDrive);
        k[35]             = k[gm::kick];
        k[gm::rim]        = piece (T::Rim,   "Rim",         1700, 0.05f, 0, 0.01f, 0.3f, 0.5f, 0.6f, 0.55f, 0.1f);
        k[gm::snare]      = piece (T::Snare, "Snare",       snareFreq, 0.18f, 3, 0.02f, snareNoise, 0.6f, 0.6f, 0.8f, 0.05f);
        k[gm::snare2]     = piece (T::Snare, "Snare 2",     snareFreq * 1.1f, 0.14f, 3, 0.02f, snareNoise * 1.2f, 0.4f, 0.7f, 0.7f, 0.05f);
        k[gm::clap]       = piece (T::Clap,  "Clap",        1200, 0.18f, 0, 0.01f, 1, 0.5f, 0.5f, 0.75f);
        k[gm::closedHat]  = piece (T::HiHat, "Closed hat",  420, 0.045f, 0, 0.01f, 0.45f, 0.5f, hatBright, 0.45f, -0.25f, 1);
        k[gm::pedalHat]   = piece (T::HiHat, "Pedal hat",   420, 0.03f, 0, 0.01f, 0.4f, 0.5f, hatBright * 0.8f, 0.35f, -0.25f, 1);
        k[gm::openHat]    = piece (T::HiHat, "Open hat",    420, 0.35f, 0, 0.01f, 0.45f, 0.5f, hatBright, 0.4f, -0.25f, 1);
        k[gm::lowTom]     = piece (T::Tom,   "Low tom",     90, 0.45f, 5, 0.06f, 0.3f, 0.2f, 0.4f, 0.75f, 0.35f);
        k[43]             = piece (T::Tom,   "Floor tom",   80, 0.5f, 5, 0.06f, 0.3f, 0.2f, 0.4f, 0.75f, 0.4f);
        k[gm::midLowTom]  = piece (T::Tom,   "Mid tom",     120, 0.4f, 5, 0.05f, 0.3f, 0.2f, 0.45f, 0.75f, 0.1f);
        k[gm::midTom]     = piece (T::Tom,   "Mid tom 2",   140, 0.38f, 5, 0.05f, 0.3f, 0.2f, 0.45f, 0.72f, 0.0f);
        k[gm::highTom]    = piece (T::Tom,   "High tom",    170, 0.35f, 5, 0.05f, 0.3f, 0.2f, 0.5f, 0.7f, -0.2f);
        k[50]             = piece (T::Tom,   "High tom 2",  200, 0.32f, 5, 0.05f, 0.3f, 0.2f, 0.5f, 0.7f, -0.3f);
        k[gm::crash]      = piece (T::Cymbal,"Crash",       330, 1.6f, 0, 0.01f, 0.7f, 0.5f, 0.7f, 0.45f, -0.3f);
        k[57]             = piece (T::Cymbal,"Crash 2",     360, 1.4f, 0, 0.01f, 0.7f, 0.5f, 0.7f, 0.45f, 0.3f);
        k[gm::ride]       = piece (T::Cymbal,"Ride",        520, 1.1f, 0, 0.01f, 0.3f, 0.5f, 0.5f, 0.35f, 0.3f);
        k[gm::tambourine] = piece (T::Shekere, "Tambourine", 7000, 0.12f, 0, 0.01f, 1, 0.5f, 0.8f, 0.45f, 0.35f);
        k[gm::shaker]     = piece (T::Shaker, "Shaker",     7500, 0.07f, 0, 0.01f, 1, 0.5f, 0.7f, 0.4f, 0.3f);
        return k;
    }

    void addAfricanPercussion (std::map<int, DrumPiece>& k)
    {
        k[gm::shekere]       = piece (T::Shekere,  "Shekere",          6200, 0.11f, 0, 0.01f, 1, 0.5f, 0.75f, 0.55f, 0.35f);
        k[gm::congaHighMute] = piece (T::Membrane, "Conga mute",       330, 0.06f, 2, 0.01f, 0.5f, 0.9f, 0.6f, 0.6f, 0.3f);
        k[gm::congaHigh]     = piece (T::Membrane, "Conga open",       330, 0.32f, 1.5f, 0.02f, 0.15f, 1.0f, 0.5f, 0.6f, 0.3f);
        k[gm::congaLow]      = piece (T::Membrane, "Tumba",            220, 0.4f, 1.5f, 0.02f, 0.15f, 1.0f, 0.45f, 0.65f, 0.4f);
        k[gm::djembeBass]    = piece (T::Membrane, "Djembe bass",      85, 0.28f, 4, 0.03f, 0.05f, 1.0f, 0.3f, 0.8f, -0.2f, 0, 1.3f);
        k[gm::djembeTone]    = piece (T::Membrane, "Djembe tone",      300, 0.18f, 1, 0.01f, 0.25f, 0.9f, 0.55f, 0.6f, -0.2f);
        k[gm::djembeSlap]    = piece (T::Membrane, "Djembe slap",      380, 0.07f, 1, 0.01f, 0.9f, 0.5f, 0.85f, 0.65f, -0.2f);
        k[gm::agogoHigh]     = piece (T::Bell,     "Agogo high",       930, 0.35f, 0, 0.01f, 0, 1, 0.8f, 0.35f, -0.35f);
        k[gm::agogoLow]      = piece (T::Bell,     "Agogo low",        620, 0.4f, 0, 0.01f, 0, 1, 0.8f, 0.35f, -0.35f);
        k[gm::talkingDrumLow]  = piece (T::Membrane, "Talking drum low",  150, 0.35f, -3, 0.08f, 0.1f, 1.0f, 0.4f, 0.7f, 0.15f);
        k[gm::talkingDrumHigh] = piece (T::Membrane, "Talking drum high", 210, 0.3f, -4, 0.06f, 0.1f, 1.0f, 0.45f, 0.7f, 0.15f);
        k[gm::udu]           = piece (T::Udu,      "Udu",              110, 0.4f, 5, 0.12f, 0, 1, 0.4f, 0.7f, 0.0f);
    }

    std::map<int, DrumPiece> kitFor (const juce::String& id)
    {
        if (id == "drums_gospel")
        {
            auto k = baseKit (58, 0.35f, 10, 190, 0.7f, 0.55f, 1.4f);
            return k;
        }
        if (id == "drums_rock")
            return baseKit (52, 0.42f, 12, 175, 0.8f, 0.5f, 1.6f);
        if (id == "drums_electronic")
        {
            auto k = baseKit (50, 0.5f, 18, 220, 0.9f, 0.7f, 1.2f);
            k[gm::clap].decay = 0.25f;
            return k;
        }
        if (id == "drums_trap")
        {
            auto k = baseKit (45, 1.4f, 14, 230, 0.95f, 0.8f, 1.8f); // long 808 boom on the kick
            k[gm::kick].name = "808";
            k[gm::closedHat].decay = 0.03f;
            k[gm::clap].level = 0.9f;
            return k;
        }
        if (id == "drums_afro")
        {
            auto k = baseKit (60, 0.28f, 9, 240, 0.55f, 0.6f, 1.5f);
            k[gm::rim].level = 0.7f;
            k[gm::shaker].level = 0.5f;
            addAfricanPercussion (k);
            return k;
        }
        if (id == "drums_amapiano")
        {
            auto k = baseKit (52, 0.3f, 8, 250, 0.5f, 0.6f, 1.1f);
            k[gm::clap].decay = 0.3f;
            k[gm::shaker].decay = 0.09f;
            k[gm::shaker].level = 0.55f;
            addAfricanPercussion (k);
            return k;
        }
        if (id == "african_percussion")
        {
            std::map<int, DrumPiece> k;
            addAfricanPercussion (k);
            k[gm::shaker] = piece (T::Shaker, "Shaker", 7500, 0.07f, 0, 0.01f, 1, 0.5f, 0.7f, 0.45f, 0.3f);
            return k;
        }
        return baseKit (55, 0.35f, 10, 200, 0.7f, 0.55f, 1.4f);
    }

    std::unique_ptr<Instrument> createBuiltin (const juce::String& id)
    {
        // Drum kits
        if (id.startsWith ("drums_") || id == "african_percussion")
            return std::make_unique<DrumKit> (id, kitFor (id));

        // Pitched percussion
        if (id == "talking_drum") return DrumKit::pitched (id, piece (T::Membrane, "Talking drum", 0, 0.35f, -3, 0.09f, 0.08f, 1, 0.45f, 0.75f));
        if (id == "log_drum")     return DrumKit::pitched (id, piece (T::LogDrum, "Log drum", 0, 0.45f, 2.5f, 0.02f, 0, 0.15f, 0.35f, 0.85f, 0, 0, 1.6f));
        if (id == "udu")          return DrumKit::pitched (id, piece (T::Udu, "Udu", 0, 0.45f, 5, 0.12f, 0, 1, 0.4f, 0.75f));
        if (id == "conga")        return DrumKit::pitched (id, piece (T::Membrane, "Conga", 0, 0.3f, 1.5f, 0.02f, 0.2f, 1, 0.5f, 0.65f));
        if (id == "djembe")       return DrumKit::pitched (id, piece (T::Membrane, "Djembe", 0, 0.22f, 2, 0.02f, 0.35f, 1, 0.6f, 0.7f));
        if (id == "agogo")        return DrumKit::pitched (id, piece (T::Bell, "Agogo", 0, 0.38f, 0, 0.01f, 0, 1, 0.8f, 0.4f));
        if (id == "shekere")
        {
            std::map<int, DrumPiece> k;
            for (int n = 0; n < 128; ++n)
                k[n] = piece (T::Shekere, "Shekere", 5200.0f + (n % 12) * 250.0f, n % 2 ? 0.16f : 0.09f, 0, 0.01f, 1, 0.5f, 0.75f, 0.55f);
            return std::make_unique<DrumKit> (id, std::move (k));
        }

        // Keys
        if (id == "grand_piano")   return std::make_unique<SynthInstrument> (id, piano (0.65f, 6.0f, 0.0004f, 0.12f));
        if (id == "upright_piano") return std::make_unique<SynthInstrument> (id, piano (0.5f, 3.5f, 0.0009f, 0.18f));
        if (id == "rhodes")        return std::make_unique<SynthInstrument> (id, fmKeys (1.0f, 2.2f, 2.5f, 3.0f));
        if (id == "wurlitzer")
        {
            auto p = fmKeys (1.0f, 3.2f, 4.0f, 2.2f);
            p.osc[1] = { W::Sine, 0.25f, 0, 1 };
            return std::make_unique<SynthInstrument> (id, p);
        }
        if (id == "hammond_organ")
        {
            SynthPatch p;
            p.mode = M::Organ;
            p.drawbars = { 0.85f, 0.85f, 1.0f, 0.5f, 0.2f, 0.4f, 0, 0, 0.3f }; // 888 gospel-ish registration, softened
            p.leslieRate = 6.2f;
            p.percussion = 0.3f;
            p.ampEnv = { 0.004f, 0.1f, 1.0f, 0.06f };
            p.gain = 0.35f;
            p.velocitySensitivity = 0.1f;
            return std::make_unique<SynthInstrument> (id, p);
        }
        if (id == "pipe_organ")
        {
            SynthPatch p;
            p.mode = M::Organ;
            p.drawbars = { 0.9f, 0.3f, 1.0f, 0.7f, 0.3f, 0.5f, 0.15f, 0.1f, 0.35f };
            p.ampEnv = { 0.06f, 0.2f, 1.0f, 0.45f };
            p.gain = 0.3f;
            p.velocitySensitivity = 0.0f;
            return std::make_unique<SynthInstrument> (id, p);
        }
        if (id == "synth_keys")
            return std::make_unique<SynthInstrument> (id, subtractive (W::Saw, W::Square, 0.5f, 7, 2500, 0.2f, 0.8f,
                                                                       { 0.003f, 0.6f, 0.5f, 0.3f }, { 0.001f, 0.4f, 0.1f, 0.3f }, 0.25f));

        // Bass
        if (id == "electric_bass") return std::make_unique<SynthInstrument> (id, pluck (0.35f, 0.4f, 0.15f, 0.15f, 0.8f));
        if (id == "slap_bass")
        {
            auto p = pluck (0.3f, 0.9f, 0.08f, 0.0f, 0.75f);
            p.cutoff = 6000;
            return std::make_unique<SynthInstrument> (id, p);
        }
        if (id == "synth_bass")    return std::make_unique<SynthInstrument> (id, bass (W::Saw, 600, 1.2f, 0.4f, 0.45f));
        if (id == "bass_808")
        {
            auto p = bass (W::Sine, 3000, 0, 2.5f, 0.7f);
            p.osc[1] = { W::Triangle, 0.15f, 0, 0 };
            p.ampEnv = { 0.002f, 2.5f, 0.0f, 0.15f };
            p.pitchEnvSemis = 3;
            p.pitchEnvDecay = 0.03f;
            p.glide = 0.06f;
            return std::make_unique<SynthInstrument> (id, p);
        }
        if (id == "upright_bass")  return std::make_unique<SynthInstrument> (id, pluck (0.6f, 0.25f, 0.25f, 0.1f, 0.85f));

        // Guitars
        if (id == "clean_electric_guitar") return std::make_unique<SynthInstrument> (id, pluck (0.25f, 0.75f, 0.15f, 0.0f, 0.45f));
        if (id == "muted_guitar")          return std::make_unique<SynthInstrument> (id, pluck (0.5f, 0.6f, 0.2f, 0.6f, 0.55f));
        if (id == "acoustic_guitar")       return std::make_unique<SynthInstrument> (id, pluck (0.15f, 0.85f, 0.12f, 0.0f, 0.45f));
        if (id == "nylon_guitar")          return std::make_unique<SynthInstrument> (id, pluck (0.3f, 0.45f, 0.2f, 0.0f, 0.5f));

        // Strings and brass
        if (id == "string_ensemble" || id == "violin" || id == "cello")
        {
            auto p = subtractive (W::Saw, W::Saw, 0.7f, 9, id == "cello" ? 1800.0f : 3500.0f, 0.1f, 0.3f,
                                  { 0.18f, 0.5f, 0.85f, 0.6f }, { 0.25f, 0.6f, 0.6f, 0.5f }, 0.22f);
            p.unison = id == "string_ensemble" ? 4 : 1;
            p.unisonDetune = 14;
            p.vibratoDepth = id == "string_ensemble" ? 0.06f : 0.15f;
            p.vibratoRate = 5.2f;
            p.stereoSpread = id == "string_ensemble" ? 0.6f : 0.2f;
            return std::make_unique<SynthInstrument> (id, p);
        }
        if (id == "brass_section" || id == "trumpet")
        {
            auto p = subtractive (W::Saw, W::Saw, 0.6f, 6, 900, 0.15f, 1.6f, { 0.04f, 0.3f, 0.8f, 0.2f }, { 0.06f, 0.35f, 0.4f, 0.25f }, 0.25f);
            p.unison = id == "brass_section" ? 3 : 1;
            p.velToCutoff = 0.7f;
            p.vibratoDepth = 0.08f;
            return std::make_unique<SynthInstrument> (id, p);
        }
        if (id == "saxophone")
        {
            auto p = subtractive (W::Square, W::Saw, 0.5f, 0, 1400, 0.3f, 1.0f, { 0.03f, 0.3f, 0.8f, 0.15f }, { 0.04f, 0.3f, 0.5f, 0.2f }, 0.25f);
            p.noiseLevel = 0.05f;
            p.vibratoDepth = 0.12f;
            p.vibratoDelay = 0.25f;
            p.mono = true;
            p.glide = 0.03f;
            return std::make_unique<SynthInstrument> (id, p);
        }

        // Pads and synths
        if (id == "worship_pad")
        {
            auto p = subtractive (W::Saw, W::Triangle, 0.6f, 5, 1600, 0.1f, 0.4f, { 1.2f, 1.0f, 0.9f, 2.5f }, { 1.5f, 2.0f, 0.5f, 2.0f }, 0.18f);
            p.unison = 5;
            p.unisonDetune = 18;
            p.osc[2] = { W::Sine, 0.5f, 0, 1 };
            p.stereoSpread = 0.8f;
            p.velocitySensitivity = 0.3f;
            return std::make_unique<SynthInstrument> (id, p);
        }
        if (id == "ambient_swell")
        {
            auto p = subtractive (W::Saw, W::Saw, 0.8f, 12, 900, 0.25f, 1.2f, { 2.5f, 2.0f, 0.8f, 3.5f }, { 3.0f, 3.0f, 0.7f, 3.0f }, 0.16f);
            p.unison = 5;
            p.unisonDetune = 22;
            p.stereoSpread = 1.0f;
            return std::make_unique<SynthInstrument> (id, p);
        }
        if (id == "synth_lead")
        {
            auto p = subtractive (W::Saw, W::Square, 0.5f, 8, 3000, 0.35f, 0.8f, { 0.005f, 0.3f, 0.8f, 0.2f }, { 0.002f, 0.3f, 0.4f, 0.2f }, 0.25f);
            p.mono = true;
            p.glide = 0.04f;
            p.vibratoDepth = 0.1f;
            return std::make_unique<SynthInstrument> (id, p);
        }
        if (id == "pluck_synth")
            return std::make_unique<SynthInstrument> (id, subtractive (W::Saw, W::Square, 0.4f, 6, 700, 0.3f, 2.2f,
                                                                       { 0.001f, 0.35f, 0.0f, 0.3f }, { 0.001f, 0.18f, 0.0f, 0.2f }, 0.3f));

        // Choir
        if (id == "choir_ahh" || id == "choir_ooh")
        {
            SynthPatch p;
            p.mode = M::Formant;
            p.osc[0] = { W::Saw, 1.0f, 0, 0 };
            p.osc[1] = { W::Saw, 0.6f, 7, 0 };
            p.unison = 5;
            p.unisonDetune = 16;
            p.vowel = id == "choir_ooh" ? 1.0f : 0.0f;
            p.vibratoDepth = 0.1f;
            p.vibratoRate = 5.0f;
            p.ampEnv = { 0.35f, 0.5f, 0.9f, 0.8f };
            p.noiseLevel = 0.04f;
            p.gain = 0.5f;
            p.stereoSpread = 0.7f;
            return std::make_unique<SynthInstrument> (id, p);
        }
        if (id == "gospel_choir_stabs")
        {
            SynthPatch p;
            p.mode = M::Formant;
            p.osc[0] = { W::Saw, 1.0f, 0, 0 };
            p.osc[1] = { W::Saw, 0.6f, 9, 0 };
            p.unison = 5;
            p.unisonDetune = 20;
            p.vowel = 0.2f;
            p.ampEnv = { 0.01f, 0.35f, 0.0f, 0.2f };
            p.gain = 0.55f;
            p.stereoSpread = 0.7f;
            return std::make_unique<SynthInstrument> (id, p);
        }

        return nullptr;
    }
}

//==============================================================================
const std::vector<InstrumentInfo>& catalogue()
{
    static const std::vector<InstrumentInfo> list {
        { "grand_piano", "Grand piano", "Keys", "Full, bright concert piano", TrackRole::Keys },
        { "upright_piano", "Upright piano", "Keys", "Warmer, more intimate piano", TrackRole::Keys },
        { "rhodes", "Rhodes", "Keys", "Bell-like electric piano for gospel and R&B", TrackRole::Keys },
        { "wurlitzer", "Wurlitzer", "Keys", "Bark-y electric piano", TrackRole::Keys },
        { "hammond_organ", "Hammond organ (Leslie)", "Keys", "Gospel organ with rotary speaker", TrackRole::Keys },
        { "pipe_organ", "Church pipe organ", "Keys", "Full church organ", TrackRole::Keys },
        { "synth_keys", "Synth keys", "Keys", "Bright polysynth keys", TrackRole::Keys },

        { "electric_bass", "Electric bass (fingered)", "Bass", "Round fingerstyle bass", TrackRole::Bass },
        { "slap_bass", "Electric bass (slap)", "Bass", "Bright, punchy slap", TrackRole::Bass },
        { "synth_bass", "Synth bass", "Bass", "Analogue-style mono bass", TrackRole::Bass },
        { "bass_808", "808", "Bass", "Long sub boom with glide", TrackRole::Bass },
        { "upright_bass", "Upright bass", "Bass", "Woody acoustic bass", TrackRole::Bass },

        { "clean_electric_guitar", "Clean electric (highlife lead)", "Guitars", "Bright single-note highlife and Afro lead lines", TrackRole::Guitar },
        { "muted_guitar", "Muted rhythm guitar", "Guitars", "Palm-muted Afrobeats rhythm", TrackRole::Guitar },
        { "acoustic_guitar", "Acoustic guitar", "Guitars", "Steel-string strums and picking", TrackRole::Guitar },
        { "nylon_guitar", "Nylon guitar", "Guitars", "Soft classical guitar", TrackRole::Guitar },

        { "drums_gospel", "Gospel live kit", "Drums", "Tight live kit for praise and gospel", TrackRole::Drums, true },
        { "drums_rock", "Rock kit", "Drums", "Bigger, heavier live kit", TrackRole::Drums, true },
        { "drums_electronic", "Electronic kit", "Drums", "Drum-machine sounds", TrackRole::Drums, true },
        { "drums_trap", "808 / trap kit", "Drums", "808 boom, crisp hats and claps", TrackRole::Drums, true },
        { "drums_afro", "Afrobeats kit", "Drums", "Afrobeats kit with African percussion", TrackRole::Drums, true },
        { "drums_amapiano", "Amapiano kit", "Drums", "Amapiano kit with shakers and percussion", TrackRole::Drums, true },

        { "african_percussion", "African percussion set", "African percussion", "Shekere, conga, djembe, agogo, udu and talking drum on one pad set", TrackRole::Percussion, true },
        { "talking_drum", "Talking drum", "African percussion", "Pitched talking drum; play melodies", TrackRole::Percussion },
        { "shekere", "Shekere", "African percussion", "Beaded gourd shaker", TrackRole::Percussion },
        { "conga", "Conga", "African percussion", "Tuned conga", TrackRole::Percussion },
        { "djembe", "Djembe", "African percussion", "Tuned djembe", TrackRole::Percussion },
        { "udu", "Udu", "African percussion", "Clay pot drum", TrackRole::Percussion },
        { "agogo", "Agogo", "African percussion", "Bell", TrackRole::Percussion },
        { "log_drum", "Log drum", "African percussion", "Amapiano log drum bass", TrackRole::Bass },

        { "string_ensemble", "String ensemble", "Strings and brass", "Lush section strings", TrackRole::Strings },
        { "violin", "Solo violin", "Strings and brass", "Expressive solo violin", TrackRole::Strings },
        { "cello", "Solo cello", "Strings and brass", "Warm solo cello", TrackRole::Strings },
        { "brass_section", "Brass section", "Strings and brass", "Punchy horn section", TrackRole::Brass },
        { "saxophone", "Saxophone", "Strings and brass", "Breathy sax lead", TrackRole::Brass },
        { "trumpet", "Trumpet", "Strings and brass", "Solo trumpet", TrackRole::Brass },

        { "worship_pad", "Worship pad", "Pads and synths", "Warm evolving pad for worship", TrackRole::Pad },
        { "ambient_swell", "Ambient swell", "Pads and synths", "Slow cinematic swell", TrackRole::Pad },
        { "synth_lead", "Synth lead", "Pads and synths", "Mono lead with glide", TrackRole::Lead },
        { "pluck_synth", "Pluck", "Pads and synths", "Short bright pluck", TrackRole::Keys },

        { "choir_ahh", "Choir \"ahh\"", "Choir", "Sustained choir pad, open vowel", TrackRole::Choir },
        { "choir_ooh", "Choir \"ooh\"", "Choir", "Sustained choir pad, round vowel", TrackRole::Choir },
        { "gospel_choir_stabs", "Gospel choir stabs", "Choir", "Short choir hits", TrackRole::Choir },
    };
    return list;
}

const InstrumentInfo* find (const juce::String& id)
{
    for (auto& i : catalogue())
        if (i.id == id) return &i;
    return nullptr;
}

juce::StringArray families()
{
    return { "Keys", "Bass", "Guitars", "Drums", "African percussion", "Strings and brass", "Pads and synths", "Choir" };
}

std::unique_ptr<Instrument> create (const juce::String& id)
{
    if (id.startsWith ("sampler:"))
        if (auto s = createSamplerInstrument (id.fromFirstOccurrenceOf ("sampler:", false, false)))
            return s;

    if (auto inst = createBuiltin (id))
        return inst;
    return createBuiltin ("grand_piano");
}

} // namespace studio::instruments
