#include "TestHelpers.h"
#include "core/analysis/LoudnessMeter.h"
#include "core/analysis/PitchDetector.h"
#include "core/analysis/AudioAnalysis.h"
#include "core/dsp/BuiltinEffects.h"
#include "core/dsp/SpectralEffects.h"
#include "core/dsp/VoiceEffects.h"
#include "core/dsp/TimeStretch.h"
#include "core/mastering/MasteringChain.h"
#include "core/vocal/VocalChain.h"
#include "core/vocal/VocalTools.h"

using namespace studio;
using namespace testutil;

class LoudnessTests : public juce::UnitTest
{
public:
    LoudnessTests() : juce::UnitTest ("Loudness meter", "Studio") {}

    void runTest() override
    {
        for (double sr : { 44100.0, 48000.0 })
        {
            beginTest ("EBU Tech 3341: 1 kHz stereo sine at -23 dBFS reads -23 LUFS @ " + juce::String (sr));
            auto s = sine (1000, 20, sr, juce::Decibels::decibelsToGain (-23.0f));
            auto m = LoudnessMeter::measure (s, sr);
            expectWithinAbsoluteError (m.integratedLufs, -23.0f, 0.1f);
            expectWithinAbsoluteError (m.loudnessRange, 0.0f, 0.2f);
        }

        beginTest ("Gating ignores silence");
        {
            const double sr = 48000;
            auto tone = sine (1000, 10, sr, juce::Decibels::decibelsToGain (-20.0f));
            juce::AudioBuffer<float> withSilence (2, tone.getNumSamples() * 2);
            withSilence.clear();
            for (int ch = 0; ch < 2; ++ch) withSilence.copyFrom (ch, 0, tone, ch, 0, tone.getNumSamples());
            auto m = LoudnessMeter::measure (withSilence, sr);
            expectWithinAbsoluteError (m.integratedLufs, -20.0f, 0.15f);
        }

        beginTest ("True peak catches inter-sample peaks");
        {
            // A sine at fs/4 with a 45 degree phase: samples read 0.707 of the real peak.
            const double sr = 48000;
            auto s = sine (sr / 4, 1, sr, 1.0f, 2, juce::MathConstants<double>::pi / 4);
            auto m = LoudnessMeter::measure (s, sr);
            expectWithinAbsoluteError (m.samplePeakDb, -3.01f, 0.1f);
            expect (m.truePeakDb > -0.6f, "true peak " + juce::String (m.truePeakDb));
        }

        beginTest ("Platform adjustments");
        PlatformTarget spotify { "Spotify", -14, true };
        expectWithinAbsoluteError (platformAdjustmentDb (spotify, -9, -0.5f), -5.0f, 0.001f);
        expectWithinAbsoluteError (platformAdjustmentDb (spotify, -18, -3.0f), 2.0f, 0.001f); // limited by peak headroom
        PlatformTarget yt { "YouTube", -14, false };
        expectEquals (platformAdjustmentDb (yt, -18, -6), 0.0f);
    }
};

class EffectTests : public juce::UnitTest
{
public:
    EffectTests() : juce::UnitTest ("Effects", "Studio") {}

    void runTest() override
    {
        const double sr = 48000;

        beginTest ("EQ boosts at the band frequency");
        {
            dsp::EqEffect eq;
            eq.prepare (sr, 512, 2);
            eq.setParam ("hpOn", 0);
            eq.setParam ("p2Freq", 3000);
            eq.setParam ("p2Gain", 6);
            eq.setParam ("p2Q", 1);
            auto s = sine (3000, 1, sr, 0.1f);
            runEffect (eq, s);
            const int skip = (int) (0.2 * sr);
            expectWithinAbsoluteError (rmsDb (s, skip, s.getNumSamples() - skip) - juce::Decibels::gainToDecibels (0.1f / std::sqrt (2.0f)), 6.0f, 0.3f);
            expectWithinAbsoluteError ((float) eq.responseDb (3000), 6.0f, 0.1f);
            expectWithinAbsoluteError ((float) eq.responseDb (100), 0.0f, 0.3f);
        }

        beginTest ("Compressor reduces level above threshold");
        {
            dsp::CompressorEffect c;
            c.prepare (sr, 512, 2);
            c.setParam ("threshold", -20);
            c.setParam ("ratio", 4);
            c.setParam ("knee", 0);
            c.setParam ("attack", 1);
            auto s = sine (200, 1, sr, juce::Decibels::decibelsToGain (-8.0f)); // peak -8 dBFS: 12 dB over
            runEffect (c, s);
            const float outPeak = juce::Decibels::gainToDecibels (s.getMagnitude (0, (int) (0.5 * sr), (int) (0.4 * sr)));
            expectWithinAbsoluteError (outPeak, -17.0f, 1.0f); // -20 + 12/4
            expectWithinAbsoluteError (c.curveDb (-8), -17.0f, 0.01f);
        }

        beginTest ("Sidechain ducks the signal when the key plays");
        {
            dsp::CompressorEffect c;
            c.prepare (sr, 512, 2);
            c.setParam ("sidechain", 1);
            c.setParam ("threshold", -30);
            c.setParam ("ratio", 8);
            auto bass = sine (60, 1, sr, 0.3f);
            auto kick = sine (50, 1, sr, 0.9f);
            auto before = rmsDb (bass);
            runEffect (c, bass, 512, &kick);
            expect (rmsDb (bass) < before - 6.0f);
        }

        beginTest ("Gate turns quiet noise down");
        {
            dsp::GateEffect g;
            g.prepare (sr, 512, 2);
            g.setParam ("threshold", -40);
            g.setParam ("range", -60);
            auto n = noise (1, sr, juce::Decibels::decibelsToGain (-55.0f));
            runEffect (g, n);
            expect (rmsDb (n, (int) (0.5 * sr), (int) (0.4 * sr)) < -90.0f);
        }

        beginTest ("True-peak limiter holds the ceiling");
        {
            dsp::LimiterEffect lim;
            lim.prepare (sr, 512, 2);
            lim.setParam ("ceiling", -1.0f);
            lim.setParam ("inputGain", 12);
            auto s = noise (2, sr, 0.5f);
            runEffect (lim, s);
            auto m = LoudnessMeter::measure (s, sr);
            expect (m.truePeakDb <= -0.85f, "true peak " + juce::String (m.truePeakDb));
            expect (m.samplePeakDb <= -1.0f + 0.01f);
        }

        beginTest ("Reverb adds a decaying tail");
        {
            dsp::ReverbEffect r;
            r.prepare (sr, 512, 2);
            r.setParam ("mix", 1.0f);
            r.setParam ("decay", 2.0f);
            juce::AudioBuffer<float> b (2, (int) (3 * sr));
            b.clear();
            b.setSample (0, 0, 1.0f);
            b.setSample (1, 0, 1.0f);
            runEffect (r, b);
            const float early = rmsDb (b, (int) (0.1 * sr), (int) (0.2 * sr));
            const float late = rmsDb (b, (int) (1.5 * sr), (int) (0.2 * sr));
            expect (early > -70.0f);
            expect (late < early - 15.0f && late > -140.0f);
            expect (std::isfinite (b.getSample (0, b.getNumSamples() - 1)));
        }

        beginTest ("De-esser reduces sibilance more than the body");
        {
            dsp::DeEsserEffect d;
            d.prepare (sr, 512, 2);
            d.setParam ("threshold", -40);
            d.setParam ("range", 12);
            auto hi = sine (7500, 1, sr, 0.3f);
            auto lo = sine (300, 1, sr, 0.3f);
            const float hiBefore = rmsDb (hi), loBefore = rmsDb (lo);
            runEffect (d, hi);
            d.reset();
            runEffect (d, lo);
            expect (rmsDb (hi) < hiBefore - 4.0f);
            expectWithinAbsoluteError (rmsDb (lo), loBefore, 1.0f);
        }

        beginTest ("STFT engine reconstructs the input when idle");
        {
            dsp::DeReverbEffect dr;
            dr.prepare (sr, 512, 2);
            dr.setParam ("amount", 0);
            auto s = sine (440, 1, sr, 0.5f);
            auto copy = s;
            runEffect (dr, s);
            const int lat = dr.getLatencySamples();
            double err = 0, ref = 0;
            for (int i = 4096; i < s.getNumSamples() - lat; ++i)
            {
                const double e = s.getSample (0, i + lat) - copy.getSample (0, i);
                err += e * e;
                ref += (double) copy.getSample (0, i) * copy.getSample (0, i);
            }
            expect (10.0 * std::log10 (err / ref) < -40.0, "reconstruction error " + juce::String (10.0 * std::log10 (err / ref)));
        }

        beginTest ("Noise removal lowers the noise floor and keeps the voice");
        {
            dsp::NoiseReductionEffect nr;
            nr.prepare (sr, 512, 2);
            nr.setParam ("amount", 24);
            // Learn on noise only.
            auto learnBuf = noise (1, sr, 0.02f, 2, 7);
            nr.setParam ("learn", 1);
            runEffect (nr, learnBuf);
            nr.setParam ("learn", 0);
            auto noiseOnly = noise (2, sr, 0.02f, 2, 9);
            auto before = rmsDb (noiseOnly);
            runEffect (nr, noiseOnly);
            expect (rmsDb (noiseOnly, (int) sr, (int) (0.5 * sr)) < before - 10.0f, "noise only after: " + juce::String (rmsDb (noiseOnly, (int) sr, (int) (0.5 * sr))));

            auto voice = sine (300, 2, sr, 0.3f);
            auto n2 = noise (2, sr, 0.02f, 2, 11);
            for (int ch = 0; ch < 2; ++ch) voice.addFrom (ch, 0, n2, ch, 0, voice.getNumSamples());
            const float voiceBefore = rmsDb (voice);
            runEffect (nr, voice);
            expectWithinAbsoluteError (rmsDb (voice, (int) sr, (int) (0.5 * sr)), voiceBefore, 1.5f);
        }

        beginTest ("Hum removal notches 50 Hz");
        {
            dsp::NoiseReductionEffect nr;
            nr.prepare (sr, 512, 2);
            nr.setParam ("amount", 0);
            nr.setParam ("adaptive", 0);
            nr.setParam ("hum", 1);
            auto hum = sine (50, 2, sr, 0.2f);
            auto before = rmsDb (hum);
            runEffect (nr, hum);
            expect (rmsDb (hum, (int) sr, (int) (0.5 * sr)) < before - 20.0f);
        }
    }
};

class PitchTests : public juce::UnitTest
{
public:
    PitchTests() : juce::UnitTest ("Pitch", "Studio") {}

    void runTest() override
    {
        const double sr = 48000;

        beginTest ("YIN finds 220 Hz");
        {
            PitchDetector det;
            det.prepare (sr, 2048);
            auto s = sine (220, 0.1, sr, 0.5f, 1);
            auto r = det.detect (s.getReadPointer (0));
            expectWithinAbsoluteError (r.frequency, 220.0f, 1.0f);
            expect (r.confidence > 0.9f);
        }

        beginTest ("Pitch shifter moves a tone up an octave");
        {
            dsp::PitchShifter ps;
            ps.prepare (sr, 40);
            ps.setRatio (2.0f);
            ps.setGrainSamples ((float) (sr / 220.0 * 8)); // grains follow the period, as pitch correction does
            auto s = sine (220, 1, sr, 0.5f, 1);
            juce::AudioBuffer<float> out (1, s.getNumSamples());
            for (int i = 0; i < s.getNumSamples(); ++i) out.setSample (0, i, ps.process (s.getSample (0, i)));
            PitchDetector det;
            det.prepare (sr, 2048);
            auto r = det.detect (out.getReadPointer (0, (int) (0.5 * sr)));
            expectWithinAbsoluteError (r.frequency, 440.0f, 6.0f);
        }

        beginTest ("Pitch correction pulls a flat note to the key");
        {
            dsp::PitchCorrectEffect pc;
            pc.prepare (sr, 512, 2);
            pc.setParam ("tonic", 9);   // A
            pc.setParam ("scale", 0);
            pc.setParam ("strength", 1);
            pc.setParam ("speed", 0);
            pc.setParam ("humanize", 0);
            const double flatA = 440.0 * std::pow (2.0, -0.35 / 12.0); // 35 cents flat
            auto s = sine (flatA, 1.5, sr, 0.4f);
            runEffect (pc, s);
            PitchDetector det;
            det.prepare (sr, 2048);
            auto r = det.detect (s.getReadPointer (0, (int) (1.0 * sr)));
            expectWithinAbsoluteError (r.frequency, 440.0f, 3.0f);
            expectWithinAbsoluteError (pc.getTargetMidi(), 69.0f, 0.01f);
        }

        beginTest ("Note segmentation for voice-to-MIDI");
        {
            juce::AudioBuffer<float> b (1, (int) (1.2 * sr));
            b.clear();
            auto a = sine (261.63, 0.5, sr, 0.4f, 1), e = sine (329.63, 0.5, sr, 0.4f, 1);
            b.copyFrom (0, 0, a, 0, 0, a.getNumSamples());
            b.copyFrom (0, (int) (0.6 * sr), e, 0, 0, e.getNumSamples());
            auto notes = vocal::voiceToMidi (b, sr, 120, nullptr, 0);
            expectEquals ((int) notes.size(), 2);
            if (notes.size() == 2)
            {
                expectEquals (notes[0].pitch, 60);
                expectEquals (notes[1].pitch, 64);
                expectWithinAbsoluteError (notes[1].startBeat, 1.2, 0.15);
            }
        }

        beginTest ("Time-stretch changes length, keeps pitch");
        {
            auto s = sine (330, 1, sr, 0.4f, 1);
            auto st = dsp::timestretch::stretch (s, 1.5, sr);
            expectEquals (st.getNumSamples(), (int) std::ceil (s.getNumSamples() * 1.5));
            PitchDetector det;
            det.prepare (sr, 2048);
            auto r = det.detect (st.getReadPointer (0, (int) (0.6 * sr)));
            expectWithinAbsoluteError (r.frequency, 330.0f, 3.0f);

            auto shifted = dsp::timestretch::pitchShift (s, 7, sr);
            expectEquals (shifted.getNumSamples(), s.getNumSamples());
            r = det.detect (shifted.getReadPointer (0, (int) (0.4 * sr)));
            expectWithinAbsoluteError (r.frequency, (float) (330.0 * std::pow (2.0, 7.0 / 12.0)), 5.0f);
        }

        beginTest ("Harmonies land on scale tones");
        {
            auto lead = sine (261.63, 1.0, sr, 0.4f, 1); // C4
            auto parts = vocal::generateHarmonies (lead, sr, Key::fromString ("C major"), { 2, 4 });
            expectEquals ((int) parts.size(), 2);
            PitchDetector det;
            det.prepare (sr, 2048);
            auto third = det.detect (parts[0].getReadPointer (0, (int) (0.5 * sr)));
            auto fifth = det.detect (parts[1].getReadPointer (0, (int) (0.5 * sr)));
            expectWithinAbsoluteError (third.frequency, 329.63f, 6.0f); // E4
            expectWithinAbsoluteError (fifth.frequency, 392.0f, 7.0f);  // G4
        }
    }
};

class AnalysisTests : public juce::UnitTest
{
public:
    AnalysisTests() : juce::UnitTest ("Analysis", "Studio") {}

    void runTest() override
    {
        const double sr = 44100;

        beginTest ("Key detection on a C major triad sequence");
        {
            juce::AudioBuffer<float> b (2, (int) (8 * sr));
            b.clear();
            const double notes[4][3] { { 261.63, 329.63, 392.0 }, { 349.23, 440.0, 523.25 }, { 392.0, 493.88, 587.33 }, { 261.63, 329.63, 392.0 } };
            for (int c = 0; c < 4; ++c)
                for (auto f : notes[c])
                {
                    auto s = sine (f, 2, sr, 0.15f);
                    for (int ch = 0; ch < 2; ++ch) b.addFrom (ch, (int) (c * 2 * sr), s, ch, 0, s.getNumSamples());
                }
            auto k = analysis::detectKey (b, sr);
            expectEquals (k.key.toString(), juce::String ("C major"));
        }

        beginTest ("Tempo detection on a click track");
        {
            const double bpm = 96;
            juce::AudioBuffer<float> b (2, (int) (16 * sr));
            b.clear();
            for (double t = 0; t < 16; t += 60.0 / bpm)
            {
                const int at = (int) (t * sr);
                for (int i = 0; i < 400 && at + i < b.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        b.setSample (ch, at + i, 0.8f * std::exp (-i / 60.0f) * (float) std::sin (i * 0.3));
            }
            auto t = analysis::detectTempo (b, sr);
            expectWithinAbsoluteError (t.bpm, bpm, 1.5);
        }

        beginTest ("Room check flags hum");
        {
            auto room = noise (5, sr, 0.0005f);
            auto hum = sine (50, 5, sr, 0.01f);
            for (int ch = 0; ch < 2; ++ch) room.addFrom (ch, 0, hum, ch, 0, room.getNumSamples());
            auto r = analysis::analyseRoom (room, sr);
            expectEquals (r.humFrequency, 50);
            expect (r.suggestions.size() >= 1);
            expectEquals ((int) r.noiseProfile.size(), 513);
        }

        beginTest ("Silence and clipping checks");
        {
            juce::AudioBuffer<float> b (2, (int) (5 * sr));
            b.clear();
            for (int i = (int) (1.5 * sr); i < (int) (3 * sr); ++i) { b.setSample (0, i, 0.5f); b.setSample (1, i, 1.0f); }
            expectWithinAbsoluteError (analysis::leadingSilenceSeconds (b, sr), 1.5f, 0.01f);
            expectWithinAbsoluteError (analysis::trailingSilenceSeconds (b, sr), 2.0f, 0.01f);
            expect (analysis::countClippedSamples (b) > 0);
        }
    }
};

class ChainTests : public juce::UnitTest
{
public:
    ChainTests() : juce::UnitTest ("Vocal and mastering chains", "Studio") {}

    void runTest() override
    {
        const double sr = 48000;

        beginTest ("Every vocal preset runs cleanly");
        for (auto& preset : VocalChain::presetNames())
        {
            VocalChain chain;
            chain.prepare (sr, 512, 2);
            VocalChainSettings s;
            s.enabled = true;
            s.preset = preset;
            s.amount = 1.0f;
            chain.applySettings (s, Key::fromString ("G major"));
            auto voice = sine (196, 2, sr, 0.3f);
            auto n = noise (2, sr, 0.01f);
            for (int ch = 0; ch < 2; ++ch) voice.addFrom (ch, 0, n, ch, 0, voice.getNumSamples());
            runEffect (chain, voice);
            const float out = rmsDb (voice, (int) sr, (int) (0.5 * sr));
            expect (std::isfinite (out) && out > -40.0f && out < 6.0f, preset + " level " + juce::String (out));
        }

        beginTest ("Bypassing steps keeps the latency constant");
        {
            VocalChain chain;
            chain.prepare (sr, 512, 2);
            VocalChainSettings s;
            s.enabled = true;
            const int before = chain.getLatencySamples();
            s.bypass["denoise"] = true;
            s.bypass["pitch"] = true;
            chain.applySettings (s, Key());
            expectEquals (chain.getLatencySamples(), before);
        }

        beginTest ("Mastering hits -14 LUFS with peaks under -1 dBTP");
        for (auto& preset : MasteringChain::presetNames())
        {
            auto mix = noise (12, sr, 0.05f);
            auto tone = sine (110, 12, sr, 0.1f);
            for (int ch = 0; ch < 2; ++ch) mix.addFrom (ch, 0, tone, ch, 0, mix.getNumSamples());
            MasteringSettings ms;
            ms.preset = preset;
            LoudnessMeter::Summary after;
            mastering::fitLoudness (mix, sr, ms, &after);
            expectWithinAbsoluteError (after.integratedLufs, -14.0f, 0.6f);
            expect (after.truePeakDb <= -0.8f, preset + " true peak " + juce::String (after.truePeakDb));
        }

        beginTest ("AI master explains its choices");
        {
            auto mix = noise (10, sr, 0.03f);
            auto sug = mastering::suggest (mix, sr, "worship_ballad");
            expect (sug.notes.size() >= 1);
            expectWithinAbsoluteError (sug.after.integratedLufs, -14.0f, 0.8f);
        }
    }
};

static LoudnessTests loudnessTests;
static EffectTests effectTests;
static PitchTests pitchTests;
static AnalysisTests analysisTests;
static ChainTests chainTests;
