#include "LyricsTools.h"
#include "../model/TempoMap.h"
#include "../arrange/Arrangement.h"

namespace studio::lyrics
{

namespace
{
    bool isVowel (juce::juce_wchar c) { return juce::String ("aeiouy").containsChar (juce::CharacterFunctions::toLowerCase (c)); }

    juce::String clean (const juce::String& w)
    {
        return w.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz'");
    }

    const juce::StringArray& vocabulary()
    {
        static const juce::StringArray words = juce::StringArray::fromTokens (
            "grace place face praise days raise ways amaze gaze phase blaze name same came flame frame shame claim fame proclaim "
            "love above of dove enough glove king sing bring everything wing ring spring thing cling "
            "light night right sight might bright fight flight delight white height ignite tonight life knife strife "
            "heart start part apart art chart depart fire higher desire choir inspire entire wire "
            "joy boy employ destroy enjoy lord word sword adored restored afford poured board "
            "god nod trod faith safe heaven seven given risen driven forgiven living giving "
            "free me see be thee sea he we key tree three victory glory story holy only lonely slowly "
            "throne alone own known stone home shown grown zone shine mine divine line fine sign vine "
            "blood flood stood good wood could should understood cross loss boss across "
            "hand stand land understand command grand sand plan man can began stay way day say pray "
            "away today always praise hallelujah alleluia worthy mercy rain again reign chain gain pain remain "
            "soul whole control goal hold gold told behold old bold peace release cease increase "
            "rise skies eyes surprise prize arise wise lies cry high sky why die fly try by nigh "
            "song strong long belong along wrong prolong gone on dawn upon drawn "
            "dance chance glance romance jesus near here fear clear dear year appear tear "
            "sweet feet meet complete street seat heat beat repeat dream stream redeem esteem "
            "call all fall wall small tall stand power hour flower shower tower our "
            "grateful faithful wonderful beautiful powerful amen again friend end send attend "
            "breath death yes bless confess rest best blessed test chest west",
            " ", "");
        return words;
    }
}

int syllableCount (const juce::String& wordIn)
{
    auto w = clean (wordIn).removeCharacters ("'");
    if (w.isEmpty()) return 0;
    if (w.length() <= 3) return 1;

    int count = 0;
    bool prevVowel = false;
    for (int i = 0; i < w.length(); ++i)
    {
        const bool v = isVowel (w[i]);
        if (v && ! prevVowel) ++count;
        prevVowel = v;
    }
    // Silent e ("grace"), but not "-le" endings ("table") or "be/me".
    if (w.endsWith ("e") && ! w.endsWith ("le") && ! w.endsWith ("ee") && count > 1) --count;
    if (w.endsWith ("es") || w.endsWith ("ed"))
        if (! w.endsWith ("ted") && ! w.endsWith ("ded") && ! w.endsWith ("ses") && ! w.endsWith ("zes") && ! w.endsWith ("ces") && count > 1)
            --count;
    // "-ia", "-io" often split ("glorious", "hallelujah" handled by vowel groups).
    for (auto s : { "ia", "io", "iu", "eo" })
        if (w.contains (s)) ++count;
    return juce::jmax (1, count);
}

int syllablesInLine (const juce::String& line)
{
    int n = 0;
    for (auto& w : juce::StringArray::fromTokens (line, " -", ""))
        n += syllableCount (w);
    return n;
}

juce::String rhymeKey (const juce::String& wordIn)
{
    auto w = clean (wordIn).removeCharacters ("'");
    if (w.isEmpty()) return {};
    // Drop a silent final e for the vowel search, then keep from the last vowel group.
    auto core = w;
    bool silentE = core.length() > 3 && core.endsWith ("e") && ! isVowel (core[core.length() - 2]);
    if (silentE) core = core.dropLastCharacters (1);
    int i = core.length() - 1;
    while (i >= 0 && ! isVowel (core[i])) --i;
    while (i > 0 && isVowel (core[i - 1])) --i;
    if (i < 0) return w;
    auto key = core.substring (i);
    // Normalise common spellings of the same sound.
    key = key.replace ("igh", "i").replace ("y", "i").replace ("ea", "ee").replace ("ai", "a");
    if (silentE) key << "_e";
    return key;
}

juce::StringArray findRhymes (const juce::String& word, const juce::StringArray& extra, int maxResults)
{
    const auto key = rhymeKey (word);
    const auto self = clean (word);
    juce::StringArray out;
    auto consider = [&] (const juce::String& w)
    {
        auto c = clean (w);
        if (c.length() < 2 || c == self || out.contains (c)) return;
        if (rhymeKey (c) == key) out.add (c);
    };
    for (auto& w : extra) consider (w);
    for (auto& w : vocabulary()) consider (w);
    if (out.size() > maxResults) out.removeRange (maxResults, out.size() - maxResults);
    return out;
}

std::vector<LyricSection> parseSections (const juce::String& text)
{
    std::vector<LyricSection> out;
    for (auto& raw : juce::StringArray::fromLines (text))
    {
        auto line = raw.trimEnd();
        if (line.trim().startsWith ("[") && line.trim().endsWith ("]"))
        {
            out.push_back ({ line.trim().substring (1, line.trim().length() - 1), {} });
            continue;
        }
        if (out.empty()) out.push_back ({ "Verse 1", {} });
        if (line.trim().isEmpty() && out.back().lines.empty()) continue;
        out.back().lines.push_back ({ line, {}, {} });
    }
    // Drop trailing empty lines.
    for (auto& s : out)
        while (! s.lines.empty() && s.lines.back().text.trim().isEmpty()) s.lines.pop_back();
    return out;
}

juce::String sectionsToText (const std::vector<LyricSection>& sections, bool withChords)
{
    juce::String s;
    for (auto& sec : sections)
    {
        s << "[" << sec.name << "]\n";
        for (auto& l : sec.lines)
        {
            if (withChords && l.chords.isNotEmpty()) s << l.chords << "\n";
            s << l.text << "\n";
        }
        s << "\n";
    }
    return s.trimEnd();
}

juce::String plainLyrics (const std::vector<LyricSection>& sections)
{
    juce::StringArray lines;
    for (auto& sec : sections)
    {
        for (auto& l : sec.lines) lines.add (l.text);
        lines.add ({});
    }
    return lines.joinIntoString ("\n").trim();
}

juce::String chordChart (const Project& p)
{
    juce::String s;
    s << (p.release.title.isNotEmpty() ? p.release.title : p.name) << "\n"
      << "Key: " << p.key.toString() << "   Tempo: " << juce::String ((int) std::round (p.bpm)) << " BPM\n\n";

    if (! p.lyricSheet.empty())
    {
        s << sectionsToText (p.lyricSheet, true);
        s << "\n\n";
    }

    // Section-by-section chord summary from the chord track.
    for (auto& sec : p.sections)
    {
        s << arrange::sectionLabel (p, sec) << ":  ";
        juce::StringArray chords;
        for (auto& c : p.chords)
            if (c.beat >= sec.startBeat - 1.0e-6 && c.beat < sec.endBeat() - 1.0e-6)
                if (chords.isEmpty() || chords[chords.size() - 1] != c.chord) chords.add (c.chord);
        // Show one cycle of the progression.
        juce::StringArray cycle;
        for (auto& c : chords) { if (cycle.size() >= 2 && cycle[0] == c && cycle.size() % 2 == 0) break; cycle.add (c); }
        s << "| " << cycle.joinIntoString (" | ") << " |\n";
    }
    return s;
}

std::vector<SyncedLyricLine> roughSync (const Project& p)
{
    std::vector<SyncedLyricLine> out;
    TempoMap tm (p);
    // Match lyric sections to song sections by type name, in order.
    std::map<juce::String, std::vector<const Section*>> byType;
    for (auto& s : p.sections) byType[toString (s.type)].push_back (&s);
    std::map<juce::String, size_t> used;

    double fallbackStart = 0;
    for (auto& ls : p.lyricSheet)
    {
        auto type = ls.name.toLowerCase().upToFirstOccurrenceOf (" ", false, false);
        const Section* target = nullptr;
        auto& list = byType[type];
        auto& idx = used[type];
        if (idx < list.size()) target = list[idx++];

        const double start = target != nullptr ? tm.beatsToSeconds (target->startBeat) : fallbackStart;
        const double end = target != nullptr ? tm.beatsToSeconds (target->endBeat()) : start + 4.0 * (double) ls.lines.size();
        int lineCount = 0;
        for (auto& l : ls.lines) if (l.text.trim().isNotEmpty()) ++lineCount;
        int k = 0;
        for (auto& l : ls.lines)
        {
            if (l.text.trim().isEmpty()) continue;
            out.push_back ({ start + (end - start) * k / juce::jmax (1, lineCount), l.text.trim() });
            ++k;
        }
        fallbackStart = end;
    }
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.timeSeconds < b.timeSeconds; });
    return out;
}

} // namespace studio::lyrics
