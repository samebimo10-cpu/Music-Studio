#include "Styles.h"
#include "../instruments/InstrumentLibrary.h"

namespace studio::beatgen
{

using namespace studio::instruments::gm;

int Groove::bars() const
{
    int maxLen = 16;
    for (auto& [n, s] : lanes) maxLen = std::max (maxLen, s.length());
    return (maxLen + 15) / 16;
}

juce::StringArray moods() { return { "joyful", "reflective", "intense" }; }

juce::StringArray phase2Styles() { return { "worship_ballad", "nigerian_praise", "afrobeats", "gospel_rnb" }; }

const std::vector<StyleDef>& styles()
{
    static const std::vector<StyleDef> list = []
    {
        std::vector<StyleDef> v;

        // ---------------------------------------------------------------- Worship ballad
        {
            StyleDef s;
            s.id = "worship_ballad"; s.name = "Worship ballad";
            s.description = "Slow, spacious worship: piano, pads, soft drums that build.";
            s.bpmMin = 60; s.bpmMax = 80; s.bpmDefault = 70;
            s.drumKit = "drums_gospel"; s.bass = "electric_bass"; s.keys = "grand_piano"; s.guitar = "clean_electric_guitar";
            s.percussion = ""; s.pad = "worship_pad";
            s.bassStyle = BassStyle::Ballad; s.keysStyle = KeysStyle::BalladArp; s.guitarStyle = GuitarStyle::AmbientSwell; s.percStyle = PercStyle::Light;
            s.intro.lanes = { { ride, "x.......x......." } };
            s.verse = {
                Groove { { { kick, "X.........x....." }, { rim, "........x......." }, { closedHat, "x...x...x...x..." } } },
                Groove { { { kick, "X......x..x....." }, { rim, "........x......." }, { ride, "x.o.x.o.x.o.x.o." } } },
            };
            s.chorus = {
                Groove { { { kick, "X.....x...x....." }, { snare, "........X......." }, { closedHat, "x.o.x.o.x.o.x.o." }, { crash, "" } } },
                Groove { { { kick, "X.....x.X.......X.....x...x....." }, { snare, "....X.......X.......X.......X..." }, { ride, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x." } } },
            };
            s.harmonicRhythm = 4;
            s.progressions["joyful"] = { { "I", "V", "vi", "IV" }, { "I", "IV", "vi", "V" }, { "IV", "I", "V", "vi" } };
            s.progressions["reflective"] = { { "vi", "IV", "I", "V" }, { "I", "iii", "IV", "V" }, { "IV", "V", "iii", "vi" } };
            s.progressions["intense"] = { { "vi", "IV", "I", "V" }, { "IV", "V", "vi", "vi" }, { "vi", "V", "IV", "IV" } };
            s.defaultKey = "D major";
            v.push_back (s);
        }

        // ---------------------------------------------------------------- Contemporary worship
        {
            StyleDef s;
            s.id = "contemporary_worship"; s.name = "Contemporary worship";
            s.description = "Driving 8ths, ambient guitars, big choruses.";
            s.bpmMin = 70; s.bpmMax = 90; s.bpmDefault = 76;
            s.drumKit = "drums_rock"; s.bass = "electric_bass"; s.keys = "grand_piano"; s.guitar = "clean_electric_guitar";
            s.pad = "worship_pad";
            s.bassStyle = BassStyle::Driving; s.keysStyle = KeysStyle::Sustained; s.guitarStyle = GuitarStyle::AmbientSwell; s.percStyle = PercStyle::Light;
            s.intro.lanes = { { kick, "X.......X......." } };
            s.verse = {
                Groove { { { kick, "X.......X.x....." }, { snare, "....x.......x..." }, { closedHat, "x.x.x.x.x.x.x.x." } } },
                Groove { { { kick, "X.......X......." }, { rim, "....x.......x..." }, { closedHat, "x.x.x.x.x.x.x.x." } } },
            };
            s.chorus = {
                Groove { { { kick, "X.x.X.x.X.x.X.x." }, { snare, "....X.......X..." }, { ride, "x.x.x.x.x.x.x.x." }, { lowTom, "............?.?." } } },
                Groove { { { kick, "X...X...X...X..." }, { snare, "....X.......X..." }, { openHat, "..x...x...x...x." }, { closedHat, "x...x...x...x..." } } },
            };
            s.harmonicRhythm = 4;
            s.progressions["joyful"] = { { "I", "V", "vi", "IV" }, { "IV", "I", "V", "V" }, { "I", "IV", "I", "V" } };
            s.progressions["reflective"] = { { "vi", "IV", "I", "V" }, { "IV", "vi", "V", "V" } };
            s.progressions["intense"] = { { "vi", "IV", "I", "V" }, { "vi", "V", "IV", "V" } };
            s.defaultKey = "A major";
            v.push_back (s);
        }

        // ---------------------------------------------------------------- Nigerian praise
        {
            StyleDef s;
            s.id = "nigerian_praise"; s.name = "Nigerian praise (highlife/makossa feel)";
            s.description = "Joyful, danceable praise: busy bass, highlife guitar, shekere and talking drum.";
            s.bpmMin = 110; s.bpmMax = 135; s.bpmDefault = 124;
            s.drumKit = "drums_gospel"; s.bass = "electric_bass"; s.keys = "hammond_organ"; s.guitar = "clean_electric_guitar";
            s.percussion = "african_percussion"; s.pad = "";
            s.bassStyle = BassStyle::Makossa; s.keysStyle = KeysStyle::OffbeatStabs; s.guitarStyle = GuitarStyle::HighlifeArp; s.percStyle = PercStyle::Praise;
            s.intro.lanes = { { kick, "X...x...X...x..." }, { closedHat, "x.x.x.x.x.x.x.x." } };
            s.verse = {
                Groove { { { kick, "X...x...X...x..." }, { snare, "....X..g....X.g." }, { closedHat, "x.x.x.x.x.x.x.x." }, { openHat, "..o...o...o...o." } } },
                Groove { { { kick, "X..x..x.X..x..x." }, { snare, "....X.......X..." }, { closedHat, "xxxxxxxxxxxxxxxx" } } },
            };
            s.chorus = {
                Groove { { { kick, "X...X...X...X..." }, { snare, "....X..g....X.gg" }, { openHat, "..x...x...x...x." }, { closedHat, "x.o.x.o.x.o.x.o." } } },
                Groove { { { kick, "X..xX...X..xX..." }, { snare, "....X.......X..." }, { ride, "x.x.x.x.x.x.x.x." }, { clap, "....x.......x..." } } },
            };
            s.harmonicRhythm = 2;
            s.progressions["joyful"] = { { "I", "IV", "V", "I" }, { "I", "vi", "ii", "V" }, { "I", "IV", "I", "V" } };
            s.progressions["reflective"] = { { "I", "vi", "IV", "V" }, { "vi", "ii", "V", "I" } };
            s.progressions["intense"] = { { "I", "IV", "V", "IV" }, { "vi", "IV", "V", "I" } };
            s.defaultKey = "Bb major";
            v.push_back (s);
        }

        // ---------------------------------------------------------------- Afrobeats
        {
            StyleDef s;
            s.id = "afrobeats"; s.name = "Afrobeats";
            s.description = "Syncopated kick, rim and shaker, chopped keys, muted guitar.";
            s.bpmMin = 100; s.bpmMax = 115; s.bpmDefault = 106;
            s.swing = 0.12f;
            s.drumKit = "drums_afro"; s.bass = "synth_bass"; s.keys = "rhodes"; s.guitar = "muted_guitar";
            s.percussion = "african_percussion"; s.pad = "";
            s.bassStyle = BassStyle::Afrobeats; s.keysStyle = KeysStyle::AfroChops; s.guitarStyle = GuitarStyle::MutedSkank; s.percStyle = PercStyle::Afro;
            s.intro.lanes = { { shaker, "x.x.x.x.x.x.x.x." }, { rim, "......x.......x." } };
            s.verse = {
                Groove { { { kick, "X..x..x...x....." }, { rim, "....x..x....x..." }, { shaker, "xoxoxoxoxoxoxoxo" }, { closedHat, "..x...x...x...x." } } },
                Groove { { { kick, "X.....x...x..x.." }, { clap, "....x.......x..." }, { shaker, "x.xox.xox.xox.xo" } } },
            };
            s.chorus = {
                Groove { { { kick, "X..x..x...x.x..." }, { clap, "....x.......x..." }, { rim, "...x..x....x..x." }, { shaker, "xoxoxoxoxoxoxoxo" }, { closedHat, "x.x.x.x.x.x.x.x." } } },
                Groove { { { kick, "X..x..x.X..x..x." }, { snare, "....x.......x..." }, { shaker, "xxxxxxxxxxxxxxxx" }, { openHat, "......x.......x." } } },
            };
            s.harmonicRhythm = 4;
            s.extendedChords = true;
            s.progressions["joyful"] = { { "I", "vi", "IV", "V" }, { "ii", "V", "I", "vi" }, { "IV", "V", "iii", "vi" } };
            s.progressions["reflective"] = { { "vi", "IV", "I", "V" }, { "ii", "iii", "IV", "IV" } };
            s.progressions["intense"] = { { "vi", "V", "IV", "V" }, { "vi", "IV", "ii", "V" } };
            s.defaultKey = "F# minor";
            v.push_back (s);
        }

        // ---------------------------------------------------------------- Amapiano
        {
            StyleDef s;
            s.id = "amapiano"; s.name = "Amapiano";
            s.description = "Log drum bass, shakers, soft four-on-the-floor and jazzy keys.";
            s.bpmMin = 110; s.bpmMax = 115; s.bpmDefault = 112;
            s.swing = 0.08f;
            s.drumKit = "drums_amapiano"; s.bass = "log_drum"; s.keys = "rhodes"; s.guitar = "";
            s.percussion = "african_percussion"; s.pad = "worship_pad";
            s.bassStyle = BassStyle::LogDrum; s.keysStyle = KeysStyle::AmapianoKeys; s.guitarStyle = GuitarStyle::None; s.percStyle = PercStyle::Amapiano;
            s.intro.lanes = { { shaker, "xoxoxoxoxoxoxoxo" } };
            s.verse = {
                Groove { { { kick, "x...x...x...x..." }, { clap, "....o.......o..." }, { shaker, "xoxoxoxoxoxoxoxo" }, { closedHat, "..o...o...o...o." } } },
                Groove { { { kick, "x...x...x...x..." }, { rim, "..o..o....o..o.." }, { shaker, "xoxoxoxoxoxoxoxo" } } },
            };
            s.chorus = {
                Groove { { { kick, "x...x...x...x..." }, { clap, "....x.......x..." }, { shaker, "xoxoxoxoxoxoxoxo" }, { openHat, "..o...o...o...o." }, { rim, "...o..o....o..o." } } },
                Groove { { { kick, "x...x...x...x..." }, { snare, "....o.......o..g" }, { shaker, "xxxxxxxxxxxxxxxx" }, { openHat, "..x...x...x...x." } } },
            };
            s.harmonicRhythm = 8;
            s.extendedChords = true;
            s.progressions["joyful"] = { { "ii", "V", "I", "vi" }, { "IV", "iii", "vi", "ii" } };
            s.progressions["reflective"] = { { "vi", "ii", "V", "I" }, { "IV", "iii", "ii", "I" } };
            s.progressions["intense"] = { { "vi", "IV", "ii", "iii" }, { "ii", "iii", "vi", "vi" } };
            s.defaultKey = "A minor";
            v.push_back (s);
        }

        // ---------------------------------------------------------------- Highlife
        {
            StyleDef s;
            s.id = "highlife"; s.name = "Highlife";
            s.description = "Bell pattern, interlocking guitars and a bouncing bass.";
            s.bpmMin = 110; s.bpmMax = 130; s.bpmDefault = 120;
            s.drumKit = "drums_afro"; s.bass = "electric_bass"; s.keys = "upright_piano"; s.guitar = "clean_electric_guitar";
            s.percussion = "african_percussion";
            s.bassStyle = BassStyle::Makossa; s.keysStyle = KeysStyle::OffbeatStabs; s.guitarStyle = GuitarStyle::HighlifeArp; s.percStyle = PercStyle::Highlife;
            s.intro.lanes = { { agogoHigh, "x.x.xx.x.x.x.x.." } };
            s.verse = {
                Groove { { { kick, "X.....x.X.....x." }, { snare, "....x.......x..." }, { closedHat, "x.x.x.x.x.x.x.x." } } },
                Groove { { { kick, "X..x....X..x...." }, { rim, "....x.......x..." }, { closedHat, "x.xxx.xxx.xxx.xx" } } },
            };
            s.chorus = {
                Groove { { { kick, "X.....x.X.....x." }, { snare, "....X.......X..." }, { ride, "x.x.x.x.x.x.x.x." }, { openHat, "..o.......o....." } } },
                Groove { { { kick, "X..x..x.X..x..x." }, { snare, "....X.......X..g" }, { closedHat, "xxxxxxxxxxxxxxxx" } } },
            };
            s.harmonicRhythm = 4;
            s.progressions["joyful"] = { { "I", "IV", "I", "V" }, { "I", "V", "IV", "V" }, { "I", "IV", "V", "V" } };
            s.progressions["reflective"] = { { "I", "vi", "IV", "V" } };
            s.progressions["intense"] = { { "I", "IV", "V", "IV" } };
            s.defaultKey = "G major";
            v.push_back (s);
        }

        // ---------------------------------------------------------------- Gospel R&B
        {
            StyleDef s;
            s.id = "gospel_rnb"; s.name = "Gospel R&B and soul";
            s.description = "Swung 16ths, Rhodes and organ comping, walking bass, ghost-note snare.";
            s.bpmMin = 65; s.bpmMax = 95; s.bpmDefault = 78;
            s.swing = 0.22f;
            s.drumKit = "drums_gospel"; s.bass = "electric_bass"; s.keys = "rhodes"; s.guitar = ""; s.pad = "hammond_organ";
            s.bassStyle = BassStyle::GospelWalk; s.keysStyle = KeysStyle::GospelComp; s.guitarStyle = GuitarStyle::None; s.percStyle = PercStyle::None;
            s.intro.lanes = { { closedHat, "x.x.x.x.x.x.x.x." } };
            s.verse = {
                Groove { { { kick, "X......x..X....." }, { snare, "....X..g.g..X..g" }, { closedHat, "x.xxx.xxx.xxx.xx" } } },
                Groove { { { kick, "X.....x...x..x.." }, { snare, "....X.g.....X.g." }, { closedHat, "xoxoxoxoxoxoxoxo" } } },
            };
            s.chorus = {
                Groove { { { kick, "X.x....x..X..x.." }, { snare, "....X..g.g..X.gg" }, { closedHat, "xoxoxoxoxoxoxoxo" }, { openHat, "..............x." } } },
                Groove { { { kick, "X......x..X....." }, { snare, "....X.......X..." }, { clap, "....x.......x..." }, { ride, "x.x.x.x.x.x.x.x." } } },
            };
            s.harmonicRhythm = 2;
            s.extendedChords = true;
            s.progressions["joyful"] = { { "IV", "iii", "vi", "ii", "V", "V", "I", "I" }, { "I", "vi", "ii", "V" } };
            s.progressions["reflective"] = { { "IV", "V", "iii", "vi", "ii", "V", "I", "I" }, { "vi", "ii", "V", "I" } };
            s.progressions["intense"] = { { "ii", "V", "iii", "vi", "ii", "V", "I", "I" } };
            s.defaultKey = "Ab major";
            v.push_back (s);
        }

        // ---------------------------------------------------------------- Hip hop and trap
        {
            StyleDef s;
            s.id = "hiphop_trap"; s.name = "Hip hop and trap";
            s.description = "808s, rolling hats, half-time snare.";
            s.bpmMin = 70; s.bpmMax = 90; s.bpmDefault = 75; s.halfTimeAvailable = true;
            s.drumKit = "drums_trap"; s.bass = "bass_808"; s.keys = "grand_piano"; s.guitar = ""; s.pad = "ambient_swell";
            s.bassStyle = BassStyle::Trap808; s.keysStyle = KeysStyle::BalladArp; s.guitarStyle = GuitarStyle::None; s.percStyle = PercStyle::None;
            s.intro.lanes = { { closedHat, "x.x.x.x.x.x.x.x." } };
            s.verse = {
                Groove { { { kick, "X......x..X....." }, { clap, "........X......." }, { closedHat, "x.x.x.x.xxx.x.xx" } } },
                Groove { { { kick, "X..x......X..x.." }, { snare, "........X......." }, { closedHat, "xxxxxxxxx.x.xxxx" } } },
            };
            s.chorus = {
                Groove { { { kick, "X......x..X..x.." }, { clap, "........X......." }, { snare, "........x......." }, { closedHat, "xxxxxxxxxxxxxxxx" }, { openHat, "..............o." } } },
                Groove { { { kick, "X..x...x..X....." }, { clap, "........X......." }, { closedHat, "x.xxx.xxxxxxx.xx" } } },
            };
            s.harmonicRhythm = 8;
            s.progressions["joyful"] = { { "vi", "IV", "I", "V" } };
            s.progressions["reflective"] = { { "vi", "IV", "ii", "iii" }, { "vi", "iii", "IV", "IV" } };
            s.progressions["intense"] = { { "vi", "vi", "IV", "V" }, { "vi", "ii", "IV", "V" } };
            s.defaultKey = "C minor";
            v.push_back (s);
        }

        // ---------------------------------------------------------------- Reggae and dancehall
        {
            StyleDef s;
            s.id = "reggae_dancehall"; s.name = "Reggae and dancehall";
            s.description = "One-drop drums, offbeat skank, melodic bass.";
            s.bpmMin = 70; s.bpmMax = 100; s.bpmDefault = 80;
            s.swing = 0.15f;
            s.drumKit = "drums_electronic"; s.bass = "electric_bass"; s.keys = "hammond_organ"; s.guitar = "muted_guitar";
            s.bassStyle = BassStyle::OneDrop; s.keysStyle = KeysStyle::OffbeatStabs; s.guitarStyle = GuitarStyle::MutedSkank; s.percStyle = PercStyle::None;
            s.intro.lanes = { { closedHat, "x.x.x.x.x.x.x.x." } };
            s.verse = {
                Groove { { { kick, "........X......." }, { rim, "........X......." }, { closedHat, "x.x.x.x.x.x.x.x." } } },
                Groove { { { kick, "X..X..X.X..X..X." }, { snare, "....X.......X..." }, { closedHat, "x.x.x.x.x.x.x.x." } } }, // dancehall
            };
            s.chorus = {
                Groove { { { kick, "....X.......X..." }, { snare, "........X......." }, { closedHat, "x.xox.xox.xox.xo" }, { openHat, "......o.......o." } } },
                Groove { { { kick, "X..X..X.X..X..X." }, { snare, "....X.......X..." }, { closedHat, "xxxxxxxxxxxxxxxx" } } },
            };
            s.harmonicRhythm = 4;
            s.progressions["joyful"] = { { "I", "IV", "V", "IV" }, { "I", "V", "vi", "IV" } };
            s.progressions["reflective"] = { { "ii", "V", "I", "I" }, { "vi", "IV", "I", "V" } };
            s.progressions["intense"] = { { "vi", "V", "IV", "V" } };
            s.defaultKey = "G major";
            v.push_back (s);
        }
        return v;
    }();
    return list;
}

const StyleDef* findStyle (const juce::String& id)
{
    for (auto& s : styles())
        if (s.id == id) return &s;
    return nullptr;
}

} // namespace studio::beatgen
