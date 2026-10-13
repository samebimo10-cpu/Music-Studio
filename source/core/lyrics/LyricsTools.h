#pragma once

#include "../model/Project.h"

namespace studio::lyrics
{

/** English syllable estimate (vowel groups with common silent-e and suffix rules). */
int syllableCount (const juce::String& word);
int syllablesInLine (const juce::String& line);

/** Rhyme key: the last stressed-vowel group and what follows ("grace" -> "ace"). */
juce::String rhymeKey (const juce::String& word);

/** Rhymes for a word from the built-in lyric vocabulary plus extra words (e.g. the song's own lyrics). */
juce::StringArray findRhymes (const juce::String& word, const juce::StringArray& extraWords = {}, int maxResults = 30);

/** "[Verse 1]" style headers to sections, and back. */
std::vector<LyricSection> parseSections (const juce::String& text);
juce::String sectionsToText (const std::vector<LyricSection>& sections, bool withChords = false);

/** Plain lyrics (for the release) from the lyric sheet, without chords or headers. */
juce::String plainLyrics (const std::vector<LyricSection>& sections);

/** Chord chart (chords above lyrics) using the project's chord track where lines have no chords yet. */
juce::String chordChart (const Project& p);

/** Spreads lyric lines across the song's sections as a starting point for syncing. */
std::vector<SyncedLyricLine> roughSync (const Project& p);

} // namespace studio::lyrics
