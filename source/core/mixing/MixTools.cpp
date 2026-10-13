#include "MixTools.h"
#include "../engine/OfflineRenderer.h"
#include "../mastering/MasteringChain.h"
#include "../model/TempoMap.h"

namespace studio::mixing
{

TrackRole guessRole (const Track& t)
{
    if (t.role != TrackRole::Unknown) return t.role;
    const auto n = (t.name + " " + t.instrument.id).toLowerCase();
    auto has = [&n] (const char* s) { return n.contains (s); };
    if (has ("lead vox") || has ("lead vocal") || has ("vocal") || has ("vox")) return has ("bv") || has ("backing") || has ("harm") ? TrackRole::BackingVocal : TrackRole::LeadVocal;
    if (has ("choir")) return TrackRole::Choir;
    if (has ("kick")) return TrackRole::Kick;
    if (has ("snare")) return TrackRole::Snare;
    if (has ("drum")) return TrackRole::Drums;
    if (has ("perc") || has ("shaker") || has ("conga") || has ("shekere") || has ("talking")) return TrackRole::Percussion;
    if (has ("bass") || has ("808") || has ("log_drum")) return TrackRole::Bass;
    if (has ("piano") || has ("keys") || has ("rhodes") || has ("organ")) return TrackRole::Keys;
    if (has ("guitar") || has ("gtr")) return TrackRole::Guitar;
    if (has ("pad")) return TrackRole::Pad;
    if (has ("string")) return TrackRole::Strings;
    if (has ("brass") || has ("horn") || has ("sax") || has ("trumpet")) return TrackRole::Brass;
    return TrackRole::Unknown;
}

juce::String setEffect (Track& t, const juce::String& type, const std::map<juce::String, float>& params, const juce::String& tag)
{
    // Reuse a slot with the same type (and tag, stored in the id) so repeated runs do not stack effects.
    for (auto& e : t.effects)
        if (e.type == type && (tag.isEmpty() || e.id.startsWith (tag)))
        {
            for (auto& [k, v] : params) e.params[k] = v;
            e.bypassed = false;
            return e.id;
        }
    EffectSlot slot;
    slot.id = (tag.isNotEmpty() ? tag + "_" : juce::String()) + makeId ("fx");
    slot.type = type;
    slot.params = params;
    t.effects.push_back (slot);
    return slot.id;
}

Track& ensureBus (Project& p, const juce::String& name, TrackRole role)
{
    for (auto& t : p.tracks)
        if (t.type == TrackType::Bus && t.name.equalsIgnoreCase (name)) return t;
    auto& bus = p.addTrack (TrackType::Bus, name, role);
    return bus;
}

std::vector<TrackAnalysis> analyseTracks (const Project& p, const juce::File& folder, double sr, double maxSeconds)
{
    // Pick the busiest stretch: the first chorus, else the middle of the song.
    TempoMap tm (p);
    double startBeat = 0, endBeat = p.lengthInBeats();
    for (auto& s : p.sections)
        if (s.type == SectionType::Chorus) { startBeat = s.startBeat; endBeat = s.endBeat(); break; }
    const double maxBeats = maxSeconds * tm.bpmAt (startBeat) / 60.0;
    endBeat = std::min (endBeat, startBeat + maxBeats);
    if (endBeat <= startBeat) endBeat = startBeat + maxBeats;

    std::vector<TrackAnalysis> out;
    for (auto& t : p.tracks)
    {
        if (t.type == TrackType::Bus) continue;
        RenderRequest req;
        req.sampleRate = sr;
        req.includeMastering = false;
        req.startBeat = startBeat;
        req.endBeat = endBeat;
        req.tailSeconds = 0;
        req.projectFolder = folder;
        Project solo = p;
        for (auto& st : solo.tracks)
        {
            st.mute = false; st.solo = false;
            st.outputBusId.clear(); st.sends.clear(); // analyse the track itself, unrouted
            if (st.id == t.id) { st.volumeDb = 0; st.pan = 0; }
        }
        auto audio = OfflineRenderer::renderTrack (solo, t.id, req);

        TrackAnalysis a;
        a.trackId = t.id;
        a.role = guessRole (t);
        a.spectrum = analysis::analyseSpectrum (audio, sr);
        // Active level: RMS over blocks louder than -50 dBFS.
        double sum = 0; int count = 0;
        const int block = (int) (0.1 * sr);
        for (int b = 0; b + block <= audio.getNumSamples(); b += block)
        {
            float r = 0;
            for (int ch = 0; ch < audio.getNumChannels(); ++ch) r = std::max (r, audio.getRMSLevel (ch, b, block));
            if (dsp::gainToDb (r) > -50.0f) { sum += (double) r * r; ++count; }
        }
        a.activeRmsDb = count > 0 ? dsp::gainToDb ((float) std::sqrt (sum / count)) : -120.0f;
        out.push_back (a);
    }
    return out;
}

AutoMixReport autoMix (Project& p, const std::vector<TrackAnalysis>& analyses)
{
    AutoMixReport rep;

    // Target active levels relative to the anchor (lead vocal, or the kick/drums without one).
    auto targetFor = [] (TrackRole r) -> float
    {
        switch (r)
        {
            case TrackRole::LeadVocal:    return 0;
            case TrackRole::BackingVocal: return -6;
            case TrackRole::Choir:        return -5;
            case TrackRole::Kick:         return -2;
            case TrackRole::Snare:        return -4;
            case TrackRole::HiHat:        return -12;
            case TrackRole::Drums:        return -2;
            case TrackRole::Percussion:   return -10;
            case TrackRole::Bass:         return -3;
            case TrackRole::Keys:         return -8;
            case TrackRole::Guitar:       return -9;
            case TrackRole::Pad:          return -14;
            case TrackRole::Strings:      return -10;
            case TrackRole::Brass:        return -8;
            case TrackRole::Lead:         return -6;
            default:                      return -8;
        }
    };

    bool hasVocal = false, hasBass = false;
    for (auto& a : analyses) { hasVocal |= a.role == TrackRole::LeadVocal; hasBass |= a.role == TrackRole::Bass; }

    // The mix is built around -18 dBFS RMS for the anchor (leaves headroom for mastering).
    const float anchorLevel = -18.0f;
    int panToggle = 0, bvToggle = 0;
    auto& reverbBus = ensureBus (p, "Reverb");
    setEffect (reverbBus, "reverb", { { "type", 1 }, { "decay", 2.2f }, { "mix", 1.0f }, { "preDelay", 25 } }, "auto");
    setEffect (reverbBus, "eq", { { "hpOn", 1 }, { "hpFreq", 250 }, { "lpOn", 1 }, { "lpFreq", 9000 } }, "auto");
    reverbBus.volumeDb = -6;

    for (auto& a : analyses)
    {
        auto* t = p.findTrack (a.trackId);
        if (t == nullptr || a.activeRmsDb < -90.0f) continue;
        const auto role = a.role;
        const float target = anchorLevel + targetFor (role) + (hasVocal ? 0.0f : 2.0f);
        const float change = juce::jlimit (-24.0f, 18.0f, target - a.activeRmsDb);
        t->volumeDb = juce::jlimit (-60.0f, 12.0f, change);
        rep.changes.add (t->name + ": level " + juce::String (t->volumeDb, 1) + " dB");

        // Panning by role.
        float pan = 0;
        switch (role)
        {
            case TrackRole::Guitar:       pan = (panToggle++ % 2 == 0) ? -0.35f : 0.35f; break;
            case TrackRole::Keys:         pan = 0.15f; break;
            case TrackRole::Percussion:   pan = 0.3f; break;
            case TrackRole::Strings:      pan = -0.2f; break;
            case TrackRole::Brass:        pan = 0.25f; break;
            case TrackRole::BackingVocal: pan = (bvToggle++ % 2 == 0) ? -0.45f : 0.45f; break;
            case TrackRole::HiHat:        pan = -0.25f; break;
            default: break;
        }
        t->pan = pan;

        // EQ to carve space for each role.
        std::map<juce::String, float> eq;
        switch (role)
        {
            case TrackRole::LeadVocal:
            case TrackRole::BackingVocal:
            case TrackRole::Choir:
                eq = { { "hpOn", 1 }, { "hpFreq", role == TrackRole::LeadVocal ? 90.0f : 140.0f }, { "p1Freq", 300 }, { "p1Gain", -2 },
                       { "p2Freq", 3000 }, { "p2Gain", role == TrackRole::LeadVocal ? 2.0f : 0.0f }, { "highGain", 2 } };
                setEffect (*t, "compressor", { { "threshold", -20 }, { "ratio", 3 }, { "attack", 8 }, { "release", 120 }, { "makeup", 3 } }, "auto");
                break;
            case TrackRole::Bass:
                eq = { { "hpOn", 1 }, { "hpFreq", 35 }, { "p1Freq", 250 }, { "p1Gain", -2.5f }, { "p2Freq", 900 }, { "p2Gain", 1.5f } };
                break;
            case TrackRole::Kick:
                eq = { { "hpOn", 1 }, { "hpFreq", 30 }, { "lowFreq", 60 }, { "lowGain", 2 }, { "p1Freq", 350 }, { "p1Gain", -3 }, { "p2Freq", 4000 }, { "p2Gain", 2 } };
                break;
            case TrackRole::Drums:
                eq = { { "hpOn", 1 }, { "hpFreq", 30 }, { "p1Freq", 400 }, { "p1Gain", -2 }, { "highGain", 1.5f } };
                break;
            case TrackRole::Keys:
            case TrackRole::Guitar:
                eq = { { "hpOn", 1 }, { "hpFreq", hasBass ? 120.0f : 60.0f }, { "p1Freq", 300 }, { "p1Gain", -2 },
                       { "p2Freq", 2800 }, { "p2Gain", hasVocal ? -1.5f : 0.0f } };
                break;
            case TrackRole::Pad:
            case TrackRole::Strings:
                eq = { { "hpOn", 1 }, { "hpFreq", 150 }, { "p2Freq", 3000 }, { "p2Gain", hasVocal ? -2.5f : 0.0f } };
                break;
            case TrackRole::Percussion:
            case TrackRole::HiHat:
                eq = { { "hpOn", 1 }, { "hpFreq", 200 } };
                break;
            default:
                eq = { { "hpOn", 1 }, { "hpFreq", 60 } };
                break;
        }
        setEffect (*t, "eq", eq, "auto");

        // Reverb sends: vocals, keys, pads and strings live in the shared space.
        const bool wantsReverb = isVocalRole (role) || role == TrackRole::Keys || role == TrackRole::Pad || role == TrackRole::Strings
                              || role == TrackRole::Guitar || role == TrackRole::Snare;
        t->sends.erase (std::remove_if (t->sends.begin(), t->sends.end(), [&] (auto& s) { return s.busId == reverbBus.id; }), t->sends.end());
        if (wantsReverb)
            t->sends.push_back ({ reverbBus.id, role == TrackRole::LeadVocal ? -12.0f : (role == TrackRole::Pad ? -10.0f : -14.0f), false });
    }

    // Bass ducks under the kick (sidechain) when both exist.
    const Track* kickOrDrums = nullptr;
    for (auto& t : p.tracks)
        if (guessRole (t) == TrackRole::Kick) kickOrDrums = &t;
    if (kickOrDrums == nullptr)
        for (auto& t : p.tracks) if (guessRole (t) == TrackRole::Drums && t.type != TrackType::Bus) { kickOrDrums = &t; break; }
    if (kickOrDrums != nullptr)
    {
        const auto kickId = kickOrDrums->id;
        for (auto& t : p.tracks)
            if (guessRole (t) == TrackRole::Bass && t.type != TrackType::Bus)
            {
                auto id = setEffect (t, "compressor", { { "threshold", -24 }, { "ratio", 4 }, { "attack", 2 }, { "release", 90 },
                                                        { "sidechain", 1 }, { "scHighPass", 20 } }, "duck");
                for (auto& e : t.effects) if (e.id == id) e.sidechainTrackId = kickId;
                rep.changes.add (t.name + ": ducks under " + kickOrDrums->name);
            }
    }
    return rep;
}

//==============================================================================
std::vector<MixIssue> assistMix (const Project& p, const juce::AudioBuffer<float>& mix, double sr, const std::vector<TrackAnalysis>& tracks)
{
    std::vector<MixIssue> issues;
    auto prof = analysis::analyseSpectrum (mix, sr);
    auto tilt = prof.tilt();
    auto ref = mastering::referenceTilt (p.style);
    float refMean = 0;
    for (auto v : ref) refMean += v;
    refMean /= (float) ref.size();
    auto excess = [&] (int b) { return tilt[(size_t) b] - (ref[(size_t) b] - refMean); };

    const TrackAnalysis* vocal = nullptr;
    const TrackAnalysis* bass = nullptr;
    const TrackAnalysis* kick = nullptr;
    for (auto& t : tracks)
    {
        if (t.role == TrackRole::LeadVocal && vocal == nullptr && t.activeRmsDb > -60.0f) vocal = &t; // only a vocal that has audio
        if (t.role == TrackRole::Bass && bass == nullptr) bass = &t;
        if ((t.role == TrackRole::Kick || t.role == TrackRole::Drums) && kick == nullptr) kick = &t;
    }

    auto trackLevelDb = [&] (const TrackAnalysis& a, int band)
    {
        const auto* t = p.findTrack (a.trackId);
        return a.spectrum.bandDb[(size_t) band] + (t != nullptr ? t->volumeDb : 0.0f);
    };

    // Vocal buried: compare vocal presence (1-4 kHz) with everything else.
    if (vocal != nullptr)
    {
        double others = 0;
        for (auto& t : tracks)
            if (&t != vocal && ! isVocalRole (t.role))
                for (int b : { 5, 6, 7 }) others += std::pow (10.0, trackLevelDb (t, b) / 10.0);
        double v = 0;
        for (int b : { 5, 6, 7 }) v += std::pow (10.0, trackLevelDb (*vocal, b) / 10.0);
        const float diff = (float) (10.0 * std::log10 (v + 1e-20) - 10.0 * std::log10 (others + 1e-20));
        if (diff < -2.0f)
        {
            const auto vid = vocal->trackId;
            issues.push_back ({ "vocal-buried", "Vocal buried in the chorus",
                                juce::String::formatted ("The lead vocal's clarity range (1-4 kHz) is %.1f dB under the band. "
                                                         "Fix: vocal up 2.5 dB with a presence lift, and keys, guitars and pads make room.", (double) -diff),
                                [vid] (Project& pr)
                                {
                                    for (auto& t : pr.tracks)
                                    {
                                        if (t.id == vid)
                                        {
                                            t.volumeDb += 2.5f;
                                            setEffect (t, "eq", { { "p2Freq", 3000 }, { "p2Gain", 3 } }, "auto");
                                        }
                                        else
                                        {
                                            const auto r = guessRole (t);
                                            if (r == TrackRole::Keys || r == TrackRole::Guitar || r == TrackRole::Pad || r == TrackRole::Strings)
                                                setEffect (t, "eq", { { "p2Freq", 3000 }, { "p2Gain", -3 } }, "auto");
                                        }
                                    }
                                } });
        }
    }

    // Bass and kick fighting in the low end.
    if (bass != nullptr && kick != nullptr)
    {
        const float b = trackLevelDb (*bass, 1), k = trackLevelDb (*kick, 1);
        if (std::abs (b - k) < 6.0f && b > -40.0f && k > -40.0f)
        {
            const auto bassId = bass->trackId, kickId = kick->trackId;
            issues.push_back ({ "kick-bass", "Bass and kick clashing",
                                "Both sit at similar levels around 60 Hz, so the low end blurs. Fix: the bass ducks briefly each time the kick hits, "
                                "and a small cut opens a pocket for the kick.",
                                [bassId, kickId] (Project& pr)
                                {
                                    if (auto* t = pr.findTrack (bassId))
                                    {
                                        auto id = setEffect (*t, "compressor", { { "threshold", -26 }, { "ratio", 4 }, { "attack", 2 }, { "release", 80 }, { "sidechain", 1 } }, "duck");
                                        for (auto& e : t->effects) if (e.id == id) e.sidechainTrackId = kickId;
                                        setEffect (*t, "eq", { { "p1Freq", 70 }, { "p1Gain", -2 }, { "p1Q", 1.5f } }, "auto");
                                    }
                                } });
        }
    }

    // Overall tone.
    const float low = 0.5f * (excess (0) + excess (1));
    if (low > 3.5f)
        issues.push_back ({ "low-end", "Too much low end",
                            juce::String::formatted ("The bass range is %.1f dB heavier than typical %s masters. It will boom in cars and swamp phone speakers. "
                                                     "Fix: low cut on everything except bass and kick, and a gentle low shelf cut on the bass.", (double) low,
                                                     p.style.isEmpty() ? "songs" : p.style.replace ("_", " ").toRawUTF8()),
                            [] (Project& pr)
                            {
                                for (auto& t : pr.tracks)
                                {
                                    const auto r = guessRole (t);
                                    if (t.type == TrackType::Bus) continue;
                                    if (r == TrackRole::Bass) setEffect (t, "eq", { { "lowFreq", 100 }, { "lowGain", -2.5f } }, "auto");
                                    else if (r != TrackRole::Kick && r != TrackRole::Drums) setEffect (t, "eq", { { "hpOn", 1 }, { "hpFreq", 120 } }, "auto");
                                }
                            } });

    const float mud = 0.5f * (excess (3) + excess (4));
    if (mud > 3.0f)
        issues.push_back ({ "mud", "Muddy low-mids",
                            "Too much energy between 250 and 500 Hz makes the mix cloudy. Fix: cut around 300 Hz on keys, guitars and pads.",
                            [] (Project& pr)
                            {
                                for (auto& t : pr.tracks)
                                {
                                    const auto r = guessRole (t);
                                    if (r == TrackRole::Keys || r == TrackRole::Guitar || r == TrackRole::Pad || r == TrackRole::Strings)
                                        setEffect (t, "eq", { { "p1Freq", 320 }, { "p1Gain", -3 } }, "auto");
                                }
                            } });

    const float harsh = 0.5f * (excess (6) + excess (7));
    if (harsh > 3.5f)
        issues.push_back ({ "harsh", "Harsh upper mids",
                            "The 2-5 kHz range is forward and will tire listeners. Fix: soften it on the master EQ and de-ess the vocals.",
                            [] (Project& pr)
                            {
                                pr.mastering.params["midFreq"] = 3200;
                                pr.mastering.params["midGain"] = -1.5f;
                                for (auto& t : pr.tracks)
                                    if (isVocalRole (guessRole (t))) setEffect (t, "deesser", { { "threshold", -32 }, { "range", 8 } }, "auto");
                            } });

    const float air = 0.5f * (excess (8) + excess (9));
    if (air < -5.0f)
        issues.push_back ({ "dull", "The mix sounds dull",
                            "Very little energy above 8 kHz. Fix: add air with a high shelf on the master.",
                            [] (Project& pr) { pr.mastering.params["highShelfGain"] = 2.0f; } });

    if (prof.peakDb > -0.3f)
        issues.push_back ({ "headroom", "The mix is clipping before mastering",
                            juce::String::formatted ("Peaks reach %.1f dBFS. Fix: every fader down 4 dB so the master has room to work.", (double) prof.peakDb),
                            [] (Project& pr) { for (auto& t : pr.tracks) if (t.type != TrackType::Bus) t.volumeDb -= 4.0f; } });

    if (prof.stereoCorrelation < 0.0f)
        issues.push_back ({ "mono", "Parts of the mix cancel in mono",
                            "The left and right channels are out of phase, so the song will lose instruments on mono phone speakers. "
                            "Fix: narrow the stereo wideners.",
                            [] (Project& pr)
                            {
                                pr.mastering.params["width"] = 0.9f;
                                for (auto& t : pr.tracks)
                                    for (auto& e : t.effects) if (e.type == "widener") e.params["width"] = 1.0f;
                            } });

    return issues;
}

ReferenceComparison compareToReference (const juce::AudioBuffer<float>& mix, const juce::AudioBuffer<float>& reference, double sr)
{
    ReferenceComparison c;
    auto m = analysis::analyseSpectrum (mix, sr);
    auto r = analysis::analyseSpectrum (reference, sr);
    auto mt = m.tilt(), rt = r.tilt();
    for (int b = 0; b < analysis::SpectrumProfile::numBands; ++b)
        c.bandDifferenceDb[(size_t) b] = mt[(size_t) b] - rt[(size_t) b];

    const auto ml = LoudnessMeter::measure (mix, sr), rl = LoudnessMeter::measure (reference, sr);
    c.loudnessDifferenceLu = ml.integratedLufs - rl.integratedLufs;
    c.matchGainDb = c.loudnessDifferenceLu;
    c.widthDifference = m.stereoWidth - r.stereoWidth;

    const float lowDiff = 0.5f * (c.bandDifferenceDb[1] + c.bandDifferenceDb[2]);
    const float midDiff = 0.5f * (c.bandDifferenceDb[5] + c.bandDifferenceDb[6]);
    const float highDiff = 0.5f * (c.bandDifferenceDb[8] + c.bandDifferenceDb[9]);
    auto describe = [&] (float d, const char* range)
    {
        if (std::abs (d) < 1.5f) return;
        c.notes.add (juce::String::formatted ("Your mix has %.1f dB %s %s than the reference.", (double) std::abs (d), d > 0 ? "more" : "less", range));
    };
    describe (lowDiff, "bass");
    describe (midDiff, "midrange");
    describe (highDiff, "top end (air)");
    if (std::abs (c.loudnessDifferenceLu) >= 1.0f)
        c.notes.add (juce::String::formatted ("Your mix is %.1f LU %s than the reference (the A/B is level-matched).",
                                              (double) std::abs (c.loudnessDifferenceLu), c.loudnessDifferenceLu > 0 ? "louder" : "quieter"));
    if (c.widthDifference < -0.1f) c.notes.add ("The reference sounds wider.");
    else if (c.widthDifference > 0.1f) c.notes.add ("Your mix is wider than the reference.");
    if (c.notes.isEmpty()) c.notes.add ("Tone and loudness are close to the reference.");
    return c;
}

} // namespace studio::mixing

//==============================================================================
namespace studio::dsp
{

ListeningCheckEffect::ListeningCheckEffect()
{
    addParam ({ "mode", "Listen as", 0, 4, 0, "", modeNames() });
}

juce::StringArray ListeningCheckEffect::modeNames() { return { "Studio", "Phone speaker", "Car", "Earbuds", "Mono" }; }

void ListeningCheckEffect::prepare (double sr, int block, int ch)
{
    sampleRate = sr; maxBlock = block; channels = ch;
    lastMode = -1;
    reset();
}

void ListeningCheckEffect::reset()
{
    for (auto& c : filters) for (auto& f : c) f.reset();
    env = 0;
}

void ListeningCheckEffect::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>*)
{
    const int mode = juce::jlimit (0, 4, (int) std::round (p (0)));
    if (mode == Off) return;

    if (mode != lastMode)
    {
        lastMode = mode;
        for (auto& c : filters)
        {
            for (auto& f : c) f.setCoefficients (Biquad::Type::AllPass, sampleRate, 1000, 0.1); // neutral placeholder
            switch (mode)
            {
                case Phone:   // tiny speaker: no bass, peaky mids, rolled-off top
                    c[0].setCoefficients (Biquad::Type::HighPass, sampleRate, 450, 0.9);
                    c[1].setCoefficients (Biquad::Type::HighPass, sampleRate, 450, 0.9);
                    c[2].setCoefficients (Biquad::Type::Peak, sampleRate, 2500, 1.2, 5);
                    c[3].setCoefficients (Biquad::Type::LowPass, sampleRate, 8000, 0.7);
                    break;
                case Car:     // cabin gain in the lows, dip in the low mids, softer top
                    c[0].setCoefficients (Biquad::Type::LowShelf, sampleRate, 90, 0.7, 6);
                    c[1].setCoefficients (Biquad::Type::Peak, sampleRate, 350, 1.0, -3);
                    c[2].setCoefficients (Biquad::Type::HighShelf, sampleRate, 7000, 0.7, -4);
                    c[3].setCoefficients (Biquad::Type::HighPass, sampleRate, 30, 0.7);
                    break;
                case Earbuds: // small drivers sealed in the ear: some bass, bright presence
                    c[0].setCoefficients (Biquad::Type::HighPass, sampleRate, 60, 0.7);
                    c[1].setCoefficients (Biquad::Type::LowShelf, sampleRate, 120, 0.7, 3);
                    c[2].setCoefficients (Biquad::Type::Peak, sampleRate, 3500, 1.0, 3);
                    c[3].setCoefficients (Biquad::Type::HighShelf, sampleRate, 10000, 0.7, -2);
                    break;
                default: break;
            }
        }
        reset();
    }

    const int nch = juce::jmin (2, buffer.getNumChannels());
    const bool sumMono = mode == Mono || mode == Phone;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float l = buffer.getSample (0, i);
        float r = nch > 1 ? buffer.getSample (1, i) : l;
        if (sumMono) l = r = 0.5f * (l + r);
        if (mode != Mono)
        {
            for (auto& f : filters[0]) l = f.process (l);
            for (auto& f : filters[1]) r = f.process (r);
        }
        if (mode == Phone)
        {
            // Small speakers compress and distort when pushed.
            env = std::max (std::abs (l), env * 0.9995f);
            const float g = env > 0.3f ? 0.3f / env : 1.0f;
            l = std::tanh (l * g * 1.5f);
            r = l;
        }
        buffer.setSample (0, i, l);
        if (nch > 1) buffer.setSample (1, i, r);
    }
}

} // namespace studio::dsp
