#include "TempoMap.h"
#include <algorithm>

namespace studio
{

TempoMap::TempoMap (const Project& p) : TempoMap (p.bpm, p.tempoEvents) {}

TempoMap::TempoMap (double bpm, std::vector<TempoEvent> changes)
{
    std::sort (changes.begin(), changes.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });

    segments.clear();
    segments.push_back ({ 0.0, 0.0, juce::jlimit (20.0, 400.0, bpm) });

    for (auto& c : changes)
    {
        if (c.beat <= 0)
        {
            segments.front().bpm = juce::jlimit (20.0, 400.0, c.bpm);
            continue;
        }
        auto& last = segments.back();
        double secs = last.startSeconds + (c.beat - last.startBeat) * 60.0 / last.bpm;
        segments.push_back ({ c.beat, secs, juce::jlimit (20.0, 400.0, c.bpm) });
    }
}

double TempoMap::beatsToSeconds (double beat) const noexcept
{
    const Segment* s = &segments.front();
    for (auto& seg : segments)
    {
        if (seg.startBeat <= beat) s = &seg;
        else break;
    }
    return s->startSeconds + (beat - s->startBeat) * 60.0 / s->bpm;
}

double TempoMap::secondsToBeats (double seconds) const noexcept
{
    const Segment* s = &segments.front();
    for (auto& seg : segments)
    {
        if (seg.startSeconds <= seconds) s = &seg;
        else break;
    }
    return s->startBeat + (seconds - s->startSeconds) * s->bpm / 60.0;
}

double TempoMap::bpmAt (double beat) const noexcept
{
    double bpm = segments.front().bpm;
    for (auto& seg : segments)
    {
        if (seg.startBeat <= beat) bpm = seg.bpm;
        else break;
    }
    return bpm;
}

} // namespace studio
