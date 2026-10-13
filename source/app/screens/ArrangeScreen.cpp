#include "ArrangeScreen.h"
#include "core/arrange/Arrangement.h"
#include "core/beatgen/BeatGenerator.h"
#include "core/instruments/InstrumentLibrary.h"
#include "core/model/TempoMap.h"
#include "core/mixing/MixTools.h"
#include "core/dsp/TimeStretch.h"
#include "core/engine/ClipCache.h"

namespace studio::ui
{

//==============================================================================
/** Ruler, section strip, chord strip, track headers and lanes in one scrolling view. */
class TimelineView : public juce::Component, private juce::Timer, private juce::ScrollBar::Listener
{
public:
    static constexpr int headerWidth = 230, rulerH = 26, sectionH = 30, chordH = 22, laneH = 76, autoH = 54;

    explicit TimelineView (Session& s) : session (s)
    {
        thumbCache = std::make_unique<juce::AudioThumbnailCache> (64);
        formats.registerBasicFormats();
        addAndMakeVisible (hScroll);
        addAndMakeVisible (vScroll);
        hScroll.addListener (this);
        vScroll.addListener (this);
        hScroll.setAutoHide (false);
        vScroll.setAutoHide (false);
        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    double pixelsPerBeat = 18.0;
    double scrollBeat = 0;
    int scrollY = 0;
    double gridBeats = 1.0;
    bool snap = true;
    bool showAutomation = false;

    void zoom (double factor)
    {
        const double centreBeat = xToBeat (headerWidth + (getWidth() - headerWidth) / 2);
        pixelsPerBeat = juce::jlimit (2.0, 400.0, pixelsPerBeat * factor);
        scrollBeat = juce::jmax (0.0, centreBeat - (getWidth() - headerWidth) / 2 / pixelsPerBeat);
        updateScrollbars();
        repaint();
    }

    void rebuildHeaders()
    {
        headers.clear();
        for (auto& t : session.project().tracks)
        {
            auto* h = headers.add (new TrackHeader (session, t.id));
            addAndMakeVisible (h);
        }
        layoutHeaders();
        updateScrollbars();
        repaint();
    }

    void refreshHeaders()
    {
        for (auto* h : headers) h->refresh();
        repaint();
    }

    double snapBeat (double b) const
    {
        if (! snap || juce::ModifierKeys::currentModifiers.isAltDown()) return std::max (0.0, b);
        return std::max (0.0, std::round (b / gridBeats) * gridBeats);
    }

    double xToBeat (int x) const { return scrollBeat + (x - headerWidth) / pixelsPerBeat; }
    int beatToX (double b) const { return headerWidth + (int) std::round ((b - scrollBeat) * pixelsPerBeat); }
    int lanesTop() const { return rulerH + sectionH + chordH; }

    int laneHeight (const Track& t) const { return laneH + (showAutomation && t.type != TrackType::Bus ? autoH : 0); }

    int trackY (int index) const
    {
        int y = lanesTop() - scrollY;
        auto& tracks = session.project().tracks;
        for (int i = 0; i < index && i < (int) tracks.size(); ++i) y += laneHeight (tracks[(size_t) i]);
        return y;
    }

    int trackAtY (int y) const
    {
        auto& tracks = session.project().tracks;
        for (int i = 0; i < (int) tracks.size(); ++i)
            if (y >= trackY (i) && y < trackY (i) + laneHeight (tracks[(size_t) i])) return i;
        return -1;
    }

    int contentHeight() const
    {
        int h = 0;
        for (auto& t : session.project().tracks) h += laneHeight (t);
        return h + 80;
    }

    void resized() override
    {
        hScroll.setBounds (headerWidth, getHeight() - 14, getWidth() - headerWidth - 14, 14);
        vScroll.setBounds (getWidth() - 14, lanesTop(), 14, getHeight() - lanesTop() - 14);
        layoutHeaders();
        updateScrollbars();
    }

    void layoutHeaders()
    {
        auto& tracks = session.project().tracks;
        for (int i = 0; i < headers.size() && i < (int) tracks.size(); ++i)
        {
            const int y = trackY (i);
            headers[i]->setBounds (0, y, headerWidth, laneH);
            headers[i]->setVisible (y + laneH > lanesTop() && y < getHeight());
        }
    }

    void updateScrollbars()
    {
        const double total = juce::jmax (64.0, session.project().lengthInBeats() + 32.0);
        hScroll.setRangeLimits (0, total);
        hScroll.setCurrentRange (scrollBeat, (getWidth() - headerWidth) / pixelsPerBeat, juce::dontSendNotification);
        vScroll.setRangeLimits (0, juce::jmax (contentHeight(), getHeight()));
        vScroll.setCurrentRange (scrollY, getHeight() - lanesTop() - 14, juce::dontSendNotification);
    }

    void scrollBarMoved (juce::ScrollBar* bar, double start) override
    {
        if (bar == &hScroll) scrollBeat = start;
        else scrollY = (int) start;
        layoutHeaders();
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown()) { zoom (w.deltaY > 0 ? 1.15 : 1.0 / 1.15); return; }
        if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
            scrollBeat = juce::jmax (0.0, scrollBeat - (w.deltaX != 0 ? w.deltaX : w.deltaY) * 200.0 / pixelsPerBeat);
        else
            scrollY = juce::jlimit (0, juce::jmax (0, contentHeight() - getHeight() + lanesTop()), scrollY - (int) (w.deltaY * 200));
        layoutHeaders();
        updateScrollbars();
        repaint();
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        const auto& p = session.project();
        g.fillAll (colours::background);
        const int w = getWidth();
        const double bpb = p.beatsPerBar();
        auto lanes = juce::Rectangle<int> (headerWidth, lanesTop(), w - headerWidth, getHeight() - lanesTop());

        // Grid lines
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (lanes);
            const double startBeat = std::floor (scrollBeat);
            for (double b = startBeat; beatToX (b) < w; b += 1.0)
            {
                const bool bar = std::fmod (b, bpb) < 1.0e-6;
                if (! bar && pixelsPerBeat < 8) continue;
                g.setColour (bar ? colours::line : colours::line.withAlpha (0.4f));
                g.drawVerticalLine (beatToX (b), (float) lanes.getY(), (float) lanes.getBottom());
            }
        }

        // Lanes and clips
        for (int i = 0; i < (int) p.tracks.size(); ++i)
        {
            const auto& t = p.tracks[(size_t) i];
            const int y = trackY (i);
            const int lh = laneHeight (t);
            if (y + lh < lanesTop() || y > getHeight()) continue;
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (lanes);
            g.setColour (t.id == session.selectedTrackId ? colours::panelRaised.withAlpha (0.6f) : colours::panel.withAlpha (0.35f));
            g.fillRect (headerWidth, y, w - headerWidth, laneH - 1);
            g.setColour (colours::line);
            g.drawHorizontalLine (y + lh - 1, (float) headerWidth, (float) w);

            for (auto& c : t.clips)
                paintClip (g, t, c, juce::Rectangle<int> (beatToX (c.startBeat), y + 3, juce::jmax (2, (int) (c.lengthBeats * pixelsPerBeat)), laneH - 7));

            if (showAutomation && t.type != TrackType::Bus)
                paintAutomation (g, t, juce::Rectangle<int> (headerWidth, y + laneH, w - headerWidth, autoH - 2));
        }

        // Ruler
        auto ruler = juce::Rectangle<int> (headerWidth, 0, w - headerWidth, rulerH);
        g.setColour (colours::panel);
        g.fillRect (0, 0, w, lanesTop());
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        const int barStep = pixelsPerBeat * bpb < 30 ? 4 : (pixelsPerBeat * bpb < 60 ? 2 : 1);
        for (double b = std::floor (scrollBeat / bpb) * bpb; beatToX (b) < w; b += bpb)
        {
            const int bar = (int) std::round (b / bpb);
            if (bar % barStep != 0) continue;
            const int x = beatToX (b);
            if (x < headerWidth) continue;
            g.drawVerticalLine (x, (float) (rulerH - 8), (float) rulerH);
            g.drawText (juce::String (bar + 1), x + 3, 2, 40, rulerH - 6, juce::Justification::centredLeft);
        }
        // Loop region
        if (loopEnd > loopStart)
        {
            g.setColour (colours::accent.withAlpha (session.engine().isLooping() ? 0.35f : 0.15f));
            g.fillRect (juce::Rectangle<int> (beatToX (loopStart), 0, beatToX (loopEnd) - beatToX (loopStart), 8).getIntersection (ruler));
        }
        // Markers, comments, tempo and key changes
        for (auto& m : p.markers) paintFlag (g, m.beat, m.name, colours::accentWarm);
        for (auto& c : p.comments) paintFlag (g, c.beat, juce::String::fromUTF8 ("\xf0\x9f\x92\xac ") + c.author, c.resolved ? colours::textDim : colours::accent);
        for (auto& te : p.tempoEvents) paintFlag (g, te.beat, juce::String (te.bpm, 0) + " BPM", colours::textDim);
        for (auto& ke : p.keyEvents) paintFlag (g, ke.beat, ke.key.toString(), colours::textDim);

        // Sections
        auto secStrip = juce::Rectangle<int> (headerWidth, rulerH, w - headerWidth, sectionH);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (secStrip);
            for (auto& sec : p.sections)
            {
                auto r = juce::Rectangle<int> (beatToX (sec.startBeat), rulerH + 2, (int) (sec.lengthBeats * pixelsPerBeat), sectionH - 4);
                auto col = sectionColour (sec.type);
                g.setColour (col.withAlpha (sec.id == session.selectedSectionId ? 0.95f : 0.65f));
                g.fillRoundedRectangle (r.toFloat().reduced (1, 0), 4.0f);
                // Energy bar
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.fillRect (r.getX() + 3, r.getBottom() - 4, (int) ((r.getWidth() - 6) * sec.energy), 2);
                g.setColour (juce::Colours::white);
                g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                g.drawText (arrange::sectionLabel (p, sec), r.reduced (6, 0), juce::Justification::centredLeft, true);
            }
        }

        // Chords
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (juce::Rectangle<int> (headerWidth, rulerH + sectionH, w - headerWidth, chordH));
            g.setFont (juce::FontOptions (12.0f));
            juce::String last;
            for (auto& c : p.chords)
            {
                const int x = beatToX (c.beat);
                if (x > w) break;
                if (x + (int) (c.lengthBeats * pixelsPerBeat) < headerWidth) continue;
                g.setColour (colours::line);
                g.drawVerticalLine (x, (float) (rulerH + sectionH + 3), (float) (lanesTop() - 3));
                if (c.chord != last || pixelsPerBeat * c.lengthBeats > 40)
                {
                    g.setColour (colours::text);
                    g.drawText (c.chord, x + 3, rulerH + sectionH, juce::jmax (30, (int) (c.lengthBeats * pixelsPerBeat) - 4), chordH, juce::Justification::centredLeft);
                }
                last = c.chord;
            }
        }

        // Header column background above the track headers
        g.setColour (colours::panelRaised);
        g.fillRect (0, 0, headerWidth, lanesTop());
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Bars", 10, 0, headerWidth - 20, rulerH, juce::Justification::centredLeft);
        g.drawText ("Sections", 10, rulerH, headerWidth - 20, sectionH, juce::Justification::centredLeft);
        g.drawText ("Chords", 10, rulerH + sectionH, headerWidth - 20, chordH, juce::Justification::centredLeft);
        g.setColour (colours::line);
        g.drawHorizontalLine (lanesTop() - 1, 0, (float) w);

        // Playhead
        const int px = beatToX (session.engine().getPositionBeats());
        if (px >= headerWidth && px < w)
        {
            g.setColour (session.engine().isRecording() ? colours::record : colours::text);
            g.drawVerticalLine (px, 0, (float) getHeight());
        }

        if (p.tracks.empty())
        {
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (16.0f));
            g.drawText ("No tracks yet. Add a track, import a beat, or go to the Beat lab.", lanes, juce::Justification::centred);
        }
    }

    static juce::Colour sectionColour (SectionType t)
    {
        switch (t)
        {
            case SectionType::Intro:        return juce::Colour (0xff5b6b8c);
            case SectionType::Verse:        return juce::Colour (0xff3d7bd9);
            case SectionType::PreChorus:    return juce::Colour (0xff8a5cd6);
            case SectionType::Chorus:       return juce::Colour (0xffd98a2b);
            case SectionType::Bridge:       return juce::Colour (0xff2ba3a3);
            case SectionType::Vamp:         return juce::Colour (0xffd9562b);
            case SectionType::Instrumental: return juce::Colour (0xff6b8c3d);
            case SectionType::Outro:        return juce::Colour (0xff5b6b8c);
        }
        return colours::accent;
    }

    void paintFlag (juce::Graphics& g, double beat, const juce::String& text, juce::Colour c)
    {
        const int x = beatToX (beat);
        if (x < headerWidth || x > getWidth()) return;
        g.setColour (c);
        g.fillRect (x, 12, 2, rulerH - 12);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (text, x + 4, 12, 140, rulerH - 12, juce::Justification::centredLeft);
    }

    void paintClip (juce::Graphics& g, const Track& t, const Clip& c, juce::Rectangle<int> r)
    {
        if (r.getRight() < headerWidth || r.getX() > getWidth()) return;
        const bool selected = c.id == session.selectedClipId;
        auto col = juce::Colour (t.colour);
        g.setColour (col.withAlpha (selected ? 0.55f : 0.32f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (selected ? colours::text : col);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, selected ? 2.0f : 1.0f);

        auto body = r.reduced (2, 14).withTrimmedTop (2);
        if (c.isMidi && ! c.notes.empty())
        {
            int lo = 127, hi = 0;
            for (auto& n : c.notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
            const float range = (float) juce::jmax (12, hi - lo + 1);
            g.setColour (col.brighter (0.6f));
            for (auto& n : c.notes)
            {
                if (n.startBeat >= c.lengthBeats) continue;
                const float x = (float) r.getX() + (float) (n.startBeat * pixelsPerBeat);
                const float nw = juce::jmax (1.5f, (float) (n.lengthBeats * pixelsPerBeat) - 1.0f);
                const float y = (float) body.getBottom() - (float) (n.pitch - lo + 1) / range * (float) body.getHeight();
                g.fillRect (x, y, nw, juce::jmax (1.5f, (float) body.getHeight() / range));
            }
        }
        else if (! c.isMidi)
        {
            if (auto* th = thumbnailFor (c.audioFile))
            {
                if (th->getTotalLength() > 0)
                {
                    TempoMap tm (session.project());
                    const double secs = (tm.beatsToSeconds (c.endBeat()) - tm.beatsToSeconds (c.startBeat)) / c.stretchRatio;
                    g.setColour (col.brighter (0.5f));
                    th->drawChannels (g, body, c.sourceOffsetSeconds, c.sourceOffsetSeconds + secs, juce::Decibels::decibelsToGain (c.gainDb));
                }
            }
            // Fades
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            const float fi = (float) (c.fadeInBeats * pixelsPerBeat), fo = (float) (c.fadeOutBeats * pixelsPerBeat);
            if (fi > 1)
            {
                juce::Path p;
                p.addTriangle ((float) r.getX(), (float) r.getY(), (float) r.getX() + fi, (float) r.getY(), (float) r.getX(), (float) r.getBottom());
                g.fillPath (p);
            }
            if (fo > 1)
            {
                juce::Path p;
                p.addTriangle ((float) r.getRight(), (float) r.getY(), (float) r.getRight() - fo, (float) r.getY(), (float) r.getRight(), (float) r.getBottom());
                g.fillPath (p);
            }
        }

        g.setColour (colours::text);
        g.setFont (juce::FontOptions (11.0f));
        juce::String label = c.name.isNotEmpty() ? c.name : (c.isMidi ? juce::String ("MIDI") : juce::File (c.audioFile).getFileNameWithoutExtension());
        if (c.stretchRatio != 1.0 || c.pitchSemitones != 0)
            label << "  (" << (c.pitchSemitones != 0 ? juce::String (c.pitchSemitones, 0) + " st " : juce::String()) << juce::String (100.0 / c.stretchRatio, 0) << "%)";
        g.drawText (label, r.reduced (5, 1).removeFromTop (13), juce::Justification::centredLeft, true);
    }

    void paintAutomation (juce::Graphics& g, const Track& t, juce::Rectangle<int> r)
    {
        g.setColour (colours::panel);
        g.fillRect (r);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (automationParam == "pan" ? "Pan automation" : "Volume automation (dB)", r.reduced (4, 2), juce::Justification::topLeft);
        const AutomationLane* lane = nullptr;
        for (auto& l : t.automation) if (l.parameter == automationParam) lane = &l;
        const float fallback = automationParam == "pan" ? t.pan : 0.0f;
        juce::Path path;
        const int steps = r.getWidth() / 4;
        for (int i = 0; i <= steps; ++i)
        {
            const int x = r.getX() + i * 4;
            const float v = lane != nullptr ? lane->valueAt (xToBeat (x), fallback) : fallback;
            const float y = valueToY (v, r);
            if (i == 0) path.startNewSubPath ((float) x, y); else path.lineTo ((float) x, y);
        }
        g.setColour (colours::accentWarm);
        g.strokePath (path, juce::PathStrokeType (1.5f));
        if (lane != nullptr)
            for (auto& pt : lane->points)
                g.fillEllipse ((float) beatToX (pt.beat) - 3.5f, valueToY (pt.value, r) - 3.5f, 7.0f, 7.0f);
    }

    juce::String automationParam = "volume";
    float valueToY (float v, juce::Rectangle<int> r) const
    {
        const float norm = automationParam == "pan" ? (v + 1.0f) / 2.0f : juce::jmap (juce::jlimit (-40.0f, 6.0f, v), -40.0f, 6.0f, 0.0f, 1.0f);
        return (float) r.getBottom() - 4 - norm * (float) (r.getHeight() - 14);
    }
    float yToValue (int y, juce::Rectangle<int> r) const
    {
        const float norm = juce::jlimit (0.0f, 1.0f, (float) (r.getBottom() - 4 - y) / (float) (r.getHeight() - 14));
        return automationParam == "pan" ? norm * 2.0f - 1.0f : juce::jmap (norm, 0.0f, 1.0f, -40.0f, 6.0f);
    }

    juce::AudioThumbnail* thumbnailFor (const juce::String& path)
    {
        auto file = store::resolve (session.getFolder(), path);
        auto key = file.getFullPathName();
        if (auto it = thumbs.find (key); it != thumbs.end()) return it->second.get();
        auto th = std::make_unique<juce::AudioThumbnail> (512, formats, *thumbCache);
        th->setSource (new juce::FileInputSource (file));
        th->addChangeListener (&repainter);
        auto* raw = th.get();
        thumbs[key] = std::move (th);
        return raw;
    }

    //==========================================================================
    enum class Drag { None, Move, TrimStart, TrimEnd, FadeIn, FadeOut, Section, SectionResize, Loop, Automation };
    Drag drag = Drag::None;
    juce::String dragTrack, dragClip, dragSection;
    double dragStartBeat = 0, clipOrigStart = 0, clipOrigLength = 0, clipOrigOffset = 0, clipOrigFade = 0;
    double loopStart = 0, loopEnd = 0;
    int automationPoint = -1;

    struct Hit { int track = -1; const Clip* clip = nullptr; bool trimStart = false, trimEnd = false, fadeIn = false, fadeOut = false; };

    Hit hitTest (juce::Point<int> pos) const
    {
        Hit h;
        if (pos.y < lanesTop() || pos.x < headerWidth) return h;
        h.track = trackAtY (pos.y);
        if (h.track < 0) return h;
        auto& t = session.project().tracks[(size_t) h.track];
        const int y = trackY (h.track);
        if (pos.y > y + laneH) return h; // automation lane
        for (auto& c : t.clips)
        {
            auto r = juce::Rectangle<int> (beatToX (c.startBeat), y + 3, juce::jmax (2, (int) (c.lengthBeats * pixelsPerBeat)), laneH - 7);
            if (! r.contains (pos)) continue;
            h.clip = &c;
            const bool top = pos.y < r.getY() + 12;
            h.trimStart = pos.x < r.getX() + 6;
            h.trimEnd = pos.x > r.getRight() - 6;
            h.fadeIn = ! c.isMidi && top && pos.x < r.getX() + 16 && ! h.trimStart;
            h.fadeOut = ! c.isMidi && top && pos.x > r.getRight() - 16 && ! h.trimEnd;
        }
        return h;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto h = hitTest (e.getPosition());
        if (h.trimStart || h.trimEnd) setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (h.fadeIn || h.fadeOut) setMouseCursor (juce::MouseCursor::CrosshairCursor);
        else if (h.clip != nullptr) setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        else if (e.y >= rulerH && e.y < rulerH + sectionH && sectionEdgeAt (e.x).isNotEmpty()) setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    juce::String sectionAt (int x) const
    {
        const double b = xToBeat (x);
        for (auto& s : session.project().sections) if (b >= s.startBeat && b < s.endBeat()) return s.id;
        return {};
    }

    juce::String sectionEdgeAt (int x) const
    {
        for (auto& s : session.project().sections) if (std::abs (beatToX (s.endBeat()) - x) < 5) return s.id;
        return {};
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto& p = session.project();
        dragStartBeat = xToBeat (e.x);
        drag = Drag::None;

        if (e.x < headerWidth) return;

        if (e.y < rulerH)
        {
            if (e.mods.isPopupMenu()) { rulerMenu (dragStartBeat); return; }
            drag = Drag::Loop;
            loopStart = loopEnd = snapBeat (dragStartBeat);
            session.engine().setPositionBeats (snapBeat (dragStartBeat));
            repaint();
            return;
        }

        if (e.y < rulerH + sectionH)
        {
            if (auto edge = sectionEdgeAt (e.x); edge.isNotEmpty() && ! e.mods.isPopupMenu())
            {
                drag = Drag::SectionResize;
                dragSection = edge;
                return;
            }
            auto id = sectionAt (e.x);
            session.selectedSectionId = id;
            if (e.mods.isPopupMenu()) { if (id.isNotEmpty()) sectionMenu (id); else addSectionHere (dragStartBeat); return; }
            if (id.isNotEmpty()) { drag = Drag::Section; dragSection = id; }
            repaint();
            return;
        }

        if (e.y < lanesTop()) return; // chord strip

        // Automation lane?
        const int ti = trackAtY (e.y);
        if (ti >= 0 && showAutomation && e.y > trackY (ti) + laneH)
        {
            auto& t = p.tracks[(size_t) ti];
            auto r = juce::Rectangle<int> (headerWidth, trackY (ti) + laneH, getWidth() - headerWidth, autoH - 2);
            dragTrack = t.id;
            automationPoint = -1;
            const AutomationLane* lane = nullptr;
            for (auto& l : t.automation) if (l.parameter == automationParam) lane = &l;
            if (lane != nullptr)
                for (int k = 0; k < (int) lane->points.size(); ++k)
                    if (std::abs (beatToX (lane->points[(size_t) k].beat) - e.x) < 6) automationPoint = k;
            if (automationPoint < 0)
            {
                const double beat = snapBeat (dragStartBeat);
                const float v = yToValue (e.y, r);
                const auto param = automationParam;
                session.perform ("Add automation point", [&] (Project& pr)
                {
                    auto& l = pr.findTrack (t.id)->automationFor (param);
                    l.points.push_back ({ beat, v });
                    std::sort (l.points.begin(), l.points.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
                    for (int k = 0; k < (int) l.points.size(); ++k) if (l.points[(size_t) k].beat == beat) automationPoint = k;
                });
            }
            drag = Drag::Automation;
            return;
        }

        auto h = hitTest (e.getPosition());
        if (h.track >= 0)
            session.select (p.tracks[(size_t) h.track].id, h.clip != nullptr ? h.clip->id : juce::String());

        if (h.clip != nullptr)
        {
            if (e.mods.isPopupMenu()) { clipMenu (p.tracks[(size_t) h.track].id, h.clip->id); return; }
            dragTrack = p.tracks[(size_t) h.track].id;
            dragClip = h.clip->id;
            clipOrigStart = h.clip->startBeat;
            clipOrigLength = h.clip->lengthBeats;
            clipOrigOffset = h.clip->sourceOffsetSeconds;
            drag = h.fadeIn ? Drag::FadeIn : h.fadeOut ? Drag::FadeOut : h.trimStart ? Drag::TrimStart : h.trimEnd ? Drag::TrimEnd : Drag::Move;
            clipOrigFade = h.fadeIn ? h.clip->fadeInBeats : h.clip->fadeOutBeats;
        }
        else
        {
            session.engine().setPositionBeats (snapBeat (dragStartBeat));
            session.selectedClipId.clear();
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const double beat = xToBeat (e.x);
        const double delta = beat - dragStartBeat;
        auto& p = const_cast<Project&> (session.project()); // live preview; committed on mouseUp

        switch (drag)
        {
            case Drag::Loop:
                loopEnd = snapBeat (beat);
                break;
            case Drag::Move:
            case Drag::TrimStart:
            case Drag::TrimEnd:
            case Drag::FadeIn:
            case Drag::FadeOut:
            {
                auto* t = p.findTrack (dragTrack);
                auto* c = t != nullptr ? t->findClip (dragClip) : nullptr;
                if (c == nullptr) break;
                if (drag == Drag::Move) c->startBeat = snapBeat (clipOrigStart + delta);
                else if (drag == Drag::TrimEnd) c->lengthBeats = juce::jmax (gridBeats * 0.25, snapBeat (clipOrigStart + clipOrigLength + delta) - c->startBeat);
                else if (drag == Drag::TrimStart)
                {
                    const double newStart = juce::jmin (clipOrigStart + clipOrigLength - 0.25, snapBeat (clipOrigStart + delta));
                    const double d = newStart - clipOrigStart;
                    c->startBeat = newStart;
                    c->lengthBeats = clipOrigLength - d;
                    if (c->isMidi)
                    {
                        // Notes stay where they were in time.
                        for (auto& n : c->notes) n.startBeat -= (d - (c->startBeat - newStart));
                    }
                    else
                    {
                        TempoMap tm (p);
                        c->sourceOffsetSeconds = juce::jmax (0.0, clipOrigOffset + (tm.beatsToSeconds (newStart) - tm.beatsToSeconds (clipOrigStart)) / c->stretchRatio);
                    }
                }
                else if (drag == Drag::FadeIn) c->fadeInBeats = juce::jlimit (0.0, c->lengthBeats / 2, clipOrigFade + delta);
                else if (drag == Drag::FadeOut) c->fadeOutBeats = juce::jlimit (0.0, c->lengthBeats / 2, clipOrigFade - delta);
                break;
            }
            case Drag::SectionResize:
                break;
            case Drag::Section:
                break;
            case Drag::Automation:
            {
                const int ti = [&] { for (int i = 0; i < (int) p.tracks.size(); ++i) if (p.tracks[(size_t) i].id == dragTrack) return i; return -1; }();
                if (ti < 0 || automationPoint < 0) break;
                auto r = juce::Rectangle<int> (headerWidth, trackY (ti) + laneH, getWidth() - headerWidth, autoH - 2);
                auto& l = p.tracks[(size_t) ti].automationFor (automationParam);
                if (automationPoint < (int) l.points.size())
                {
                    l.points[(size_t) automationPoint].beat = snapBeat (beat);
                    l.points[(size_t) automationPoint].value = yToValue (e.y, r);
                }
                break;
            }
            case Drag::None: break;
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const double beat = xToBeat (e.x);
        switch (drag)
        {
            case Drag::Loop:
                if (std::abs (loopEnd - loopStart) >= gridBeats)
                {
                    if (loopEnd < loopStart) std::swap (loopStart, loopEnd);
                    session.engine().setLoop (true, loopStart, loopEnd);
                    session.recordSettings.punchInBeat = loopStart;
                    session.recordSettings.punchOutBeat = loopEnd;
                    session.status ("Loop " + juce::String (loopStart / session.project().beatsPerBar() + 1, 1) + " - "
                                    + juce::String (loopEnd / session.project().beatsPerBar() + 1, 1) + ". Press L to toggle looping.");
                }
                else loopStart = loopEnd = 0;
                break;
            case Drag::Move: case Drag::TrimStart: case Drag::TrimEnd: case Drag::FadeIn: case Drag::FadeOut: case Drag::Automation:
            {
                // Moving to another track of the same type.
                if (drag == Drag::Move)
                {
                    const int ti = trackAtY (e.y);
                    const auto& tracks = session.project().tracks;
                    if (ti >= 0 && tracks[(size_t) ti].id != dragTrack)
                    {
                        auto* src = session.project().findTrack (dragTrack);
                        if (src != nullptr && tracks[(size_t) ti].type == src->type)
                        {
                            const auto targetId = tracks[(size_t) ti].id;
                            const auto clipId = dragClip, from = dragTrack;
                            session.perform ("Move clip to track", [=] (Project& pr)
                            {
                                auto* a = pr.findTrack (from);
                                auto* b = pr.findTrack (targetId);
                                if (a == nullptr || b == nullptr) return;
                                for (auto it = a->clips.begin(); it != a->clips.end(); ++it)
                                    if (it->id == clipId) { b->clips.push_back (*it); a->clips.erase (it); break; }
                            });
                            session.select (targetId, clipId);
                            break;
                        }
                    }
                }
                session.perform (drag == Drag::Move ? "Move clip" : drag == Drag::Automation ? "Edit automation" : "Edit clip", [] (Project&) {});
                break;
            }
            case Drag::Section:
            {
                const auto target = sectionAt (e.x);
                if (target.isNotEmpty() && target != dragSection)
                {
                    int index = 0;
                    auto sorted = session.project().sections;
                    std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
                    for (int i = 0; i < (int) sorted.size(); ++i) if (sorted[(size_t) i].id == target) index = i;
                    const auto id = dragSection;
                    session.perform ("Move section", [=] (Project& pr) { arrange::moveSection (pr, id, index); });
                }
                break;
            }
            case Drag::SectionResize:
            {
                const auto id = dragSection;
                if (auto* s = session.project().findSection (id) ? const_cast<Project&> (session.project()).findSection (id) : nullptr)
                {
                    const double bpb = session.project().beatsPerBar();
                    const double newLen = juce::jmax (bpb, std::round ((beat - s->startBeat) / bpb) * bpb);
                    if (std::abs (newLen - s->lengthBeats) > 0.01)
                        session.perform ("Resize section", [=] (Project& pr) { arrange::resizeSection (pr, id, newLen); });
                }
                break;
            }
            case Drag::None: break;
        }
        drag = Drag::None;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto h = hitTest (e.getPosition());
        const auto& p = session.project();
        if (h.clip != nullptr && h.clip->isMidi)
        {
            session.select (p.tracks[(size_t) h.track].id, h.clip->id);
            if (onOpenPianoRoll) onOpenPianoRoll();
            return;
        }
        if (showAutomation)
        {
            const int ti = trackAtY (e.y);
            if (ti >= 0 && e.y > trackY (ti) + laneH && automationPoint >= 0)
            {
                const auto id = p.tracks[(size_t) ti].id;
                const int k = automationPoint;
                const auto param = automationParam;
                session.perform ("Delete automation point", [=] (Project& pr)
                {
                    auto& l = pr.findTrack (id)->automationFor (param);
                    if (k < (int) l.points.size()) l.points.erase (l.points.begin() + k);
                });
                return;
            }
        }
        if (h.track >= 0 && h.clip == nullptr && p.tracks[(size_t) h.track].type == TrackType::Midi)
        {
            const auto id = p.tracks[(size_t) h.track].id;
            const double start = std::floor (xToBeat (e.x) / p.beatsPerBar()) * p.beatsPerBar();
            const double len = p.beatsPerBar() * 4;
            juce::String newId = makeId ("clip");
            session.perform ("New MIDI clip", [=] (Project& pr)
            {
                Clip c;
                c.id = newId;
                c.isMidi = true;
                c.startBeat = start;
                c.lengthBeats = len;
                c.name = "MIDI";
                pr.findTrack (id)->clips.push_back (c);
            });
            session.select (id, newId);
        }
    }

    std::function<void()> onOpenPianoRoll;

    void clipMenu (const juce::String& trackId, const juce::String& clipId)
    {
        juce::PopupMenu m;
        m.addItem (1, "Split at playhead (S)");
        m.addItem (2, "Duplicate (Ctrl+D)");
        m.addItem (3, "Delete");
        m.addSeparator();
        auto* c = const_cast<Project&> (session.project()).findTrack (trackId)->findClip (clipId);
        if (c != nullptr && ! c->isMidi)
        {
            m.addItem (10, "Time-stretch to song tempo...");
            m.addItem (11, "Pitch shift...");
            m.addItem (12, "Gain +3 dB");
            m.addItem (13, "Gain -3 dB");
            m.addItem (14, "Warp: fix timing to the grid");
        }
        else m.addItem (20, "Open in piano roll");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, trackId, clipId] (int r)
        {
            if (r == 1) splitSelected();
            else if (r == 2) duplicateSelected();
            else if (r == 3) deleteSelected();
            else if (r == 12 || r == 13)
                session.perform ("Clip gain", [=] (Project& p) { if (auto* c = p.findTrack (trackId)->findClip (clipId)) c->gainDb += r == 12 ? 3.0f : -3.0f; });
            else if (r == 20 && onOpenPianoRoll) onOpenPianoRoll();
            else if (r == 10 || r == 11) stretchDialog (trackId, clipId, r == 11);
            else if (r == 14) warpClip (trackId, clipId);
        });
    }

    void stretchDialog (const juce::String& trackId, const juce::String& clipId, bool pitch)
    {
        auto* w = new juce::AlertWindow (pitch ? "Pitch shift" : "Time-stretch", pitch ? "Semitones (-12 to +12):" : "Original tempo of the audio (BPM):",
                                         juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("v", pitch ? "0" : juce::String (session.project().bpm, 0));
        w->addButton ("Apply", 1);
        w->addButton ("Cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, trackId, clipId, pitch] (int r)
        {
            if (r != 1) return;
            const double v = w->getTextEditorContents ("v").getDoubleValue();
            session.perform (pitch ? "Pitch shift clip" : "Stretch clip", [=] (Project& p)
            {
                auto* c = p.findTrack (trackId)->findClip (clipId);
                if (c == nullptr) return;
                if (pitch) c->pitchSemitones = juce::jlimit (-12.0, 12.0, v);
                else if (v > 20)
                {
                    const double ratio = v / p.bpm;
                    c->lengthBeats = c->lengthBeats * c->stretchRatio / ratio * (p.bpm / p.bpm);
                    c->stretchRatio = ratio;
                }
            });
        }), true);
    }

    void warpClip (const juce::String& trackId, const juce::String& clipId);

    void rulerMenu (double beat)
    {
        juce::PopupMenu m;
        m.addItem (1, "Add marker here");
        m.addItem (2, "Add comment here");
        m.addItem (3, "Clear loop");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, beat] (int r)
        {
            if (r == 1) promptText ("Marker name", "Marker", [this, beat] (juce::String name) { session.perform ("Add marker", [=] (Project& p) { p.markers.push_back ({ snapBeat (beat), name }); }); });
            if (r == 2) promptText ("Comment", "", [this, beat] (juce::String text)
            {
                session.perform ("Add comment", [=] (Project& p)
                {
                    p.comments.push_back ({ makeId ("cmt"), snapBeat (beat), juce::SystemStats::getFullUserName(), text,
                                            juce::Time::getCurrentTime().toISO8601 (true), false });
                });
            });
            if (r == 3) { loopStart = loopEnd = 0; session.engine().setLoop (false, 0, 0); repaint(); }
        });
    }

    void promptText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done)
    {
        auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("t", initial);
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r) { if (r == 1) done (w->getTextEditorContents ("t")); }), true);
    }

    void addSectionHere (double beat)
    {
        juce::PopupMenu m;
        int id = 1;
        for (auto t : { SectionType::Intro, SectionType::Verse, SectionType::PreChorus, SectionType::Chorus, SectionType::Bridge,
                        SectionType::Vamp, SectionType::Instrumental, SectionType::Outro })
            m.addItem (id++, "Add " + toString (t));
        m.showMenuAsync (juce::PopupMenu::Options(), [this, beat] (int r)
        {
            if (r <= 0) return;
            const auto type = (SectionType) (r - 1);
            session.perform ("Add section", [=] (Project& p)
            {
                const double bpb = p.beatsPerBar();
                double start = std::floor (beat / bpb) * bpb;
                // Append after the last section if clicked past the end.
                double end = 0;
                for (auto& s : p.sections) end = std::max (end, s.endBeat());
                if (start >= end) start = end;
                Section s;
                s.id = makeId ("sec");
                s.type = type;
                s.name = toString (type);
                s.startBeat = start;
                s.lengthBeats = (type == SectionType::Intro || type == SectionType::Outro ? 4 : 8) * bpb;
                s.energy = type == SectionType::Chorus || type == SectionType::Vamp ? 0.8f : 0.5f;
                if (start < end) arrange::insertTime (p, start, s.lengthBeats);
                p.sections.push_back (s);
                arrange::sortSections (p);
            });
        });
    }

    void sectionMenu (const juce::String& id)
    {
        juce::PopupMenu m, typeMenu, energy;
        m.addItem (1, "Duplicate");
        m.addItem (2, "Delete (removes its content)");
        m.addItem (3, "Rename...");
        int k = 10;
        for (auto t : { SectionType::Intro, SectionType::Verse, SectionType::PreChorus, SectionType::Chorus, SectionType::Bridge,
                        SectionType::Vamp, SectionType::Instrumental, SectionType::Outro })
            typeMenu.addItem (k++, toString (t));
        m.addSubMenu ("Type", typeMenu);
        for (int e = 1; e <= 5; ++e) energy.addItem (30 + e, juce::String (e * 20) + "%");
        m.addSubMenu ("Energy", energy);
        m.addSeparator();
        m.addItem (4, "Vamp builder: repeat with rising energy...");
        m.addItem (5, "Loop this section");
        m.addItem (6, "Regenerate generated parts here");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, id] (int r)
        {
            if (r == 1) session.perform ("Duplicate section", [id] (Project& p) { arrange::duplicateSection (p, id); });
            else if (r == 2) session.perform ("Delete section", [id] (Project& p) { arrange::deleteSection (p, id); });
            else if (r == 3) promptText ("Section name", session.project().findSection (id) ? arrange::sectionLabel (session.project(), *const_cast<Project&> (session.project()).findSection (id)) : juce::String(),
                                         [this, id] (juce::String n) { session.perform ("Rename section", [=] (Project& p) { if (auto* s = p.findSection (id)) s->name = n; }); });
            else if (r >= 10 && r < 18) session.perform ("Section type", [id, r] (Project& p) { if (auto* s = p.findSection (id)) { s->type = (SectionType) (r - 10); s->name = toString (s->type); } });
            else if (r > 30 && r <= 35) session.perform ("Section energy", [id, r] (Project& p) { if (auto* s = p.findSection (id)) s->energy = (float) (r - 30) * 0.2f; });
            else if (r == 4) vampDialog (id);
            else if (r == 5)
                if (auto* s = const_cast<Project&> (session.project()).findSection (id))
                {
                    loopStart = s->startBeat; loopEnd = s->endBeat();
                    session.engine().setLoop (true, loopStart, loopEnd);
                    repaint();
                }
            if (r == 6)
                session.perform ("Regenerate parts", [] (Project& p) { for (auto& t : p.tracks) if (t.generator.part.isNotEmpty()) beatgen::BeatGenerator::renderPart (p, t); });
        });
    }

    void vampDialog (const juce::String& id)
    {
        auto* w = new juce::AlertWindow ("Vamp builder", "Repeats the section with rising energy - common in praise songs.", juce::MessageBoxIconType::NoIcon);
        w->addComboBox ("repeats", { "2", "3", "4", "6", "8" }, "Repeats");
        w->getComboBoxComponent ("repeats")->setSelectedItemIndex (1);
        w->addComboBox ("lift", { "No key change", "Lift the last repeat by a semitone", "Lift the last repeat by a tone" }, "Key");
        w->getComboBoxComponent ("lift")->setSelectedItemIndex (0);
        w->addButton ("Build vamp", 1);
        w->addButton ("Cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, id] (int r)
        {
            if (r != 1) return;
            arrange::VampOptions o;
            o.repeats = w->getComboBoxComponent ("repeats")->getText().getIntValue();
            const int lift = w->getComboBoxComponent ("lift")->getSelectedItemIndex();
            o.keyLiftOnLast = lift > 0;
            o.liftSemitones = lift == 2 ? 2 : 1;
            session.perform ("Vamp builder", [=] (Project& p) { arrange::buildVamp (p, id, o); });
        }), true);
    }

    void splitSelected()
    {
        const double at = session.engine().getPositionBeats();
        const auto trackId = session.selectedTrackId, clipId = session.selectedClipId;
        session.perform ("Split", [=] (Project& p)
        {
            if (clipId.isNotEmpty())
            {
                if (auto* t = p.findTrack (trackId)) arrange::splitClip (p, *t, clipId, at);
            }
            else arrange::splitAt (p, at);
        });
    }

    void duplicateSelected()
    {
        const auto trackId = session.selectedTrackId, clipId = session.selectedClipId;
        if (clipId.isEmpty()) return;
        auto newId = makeId ("clip");
        session.perform ("Duplicate clip", [=] (Project& p)
        {
            auto* t = p.findTrack (trackId);
            if (auto* c = t != nullptr ? t->findClip (clipId) : nullptr)
            {
                Clip copy = *c;
                copy.id = newId;
                copy.startBeat = c->endBeat();
                t->clips.push_back (copy);
            }
        });
        session.select (trackId, newId);
    }

    void deleteSelected()
    {
        const auto trackId = session.selectedTrackId, clipId = session.selectedClipId;
        if (clipId.isEmpty()) return;
        session.perform ("Delete clip", [=] (Project& p)
        {
            if (auto* t = p.findTrack (trackId))
                t->clips.erase (std::remove_if (t->clips.begin(), t->clips.end(), [&] (auto& c) { return c.id == clipId; }), t->clips.end());
        });
        session.selectedClipId.clear();
    }

private:
    /** Name, record/mute/solo, volume and instrument for one track. */
    struct TrackHeader : public juce::Component
    {
        TrackHeader (Session& s, juce::String trackId) : session (s), id (std::move (trackId))
        {
            name.setEditable (false, true);
            name.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            name.onTextChange = [this] { auto n = name.getText(); session.perform ("Rename track", [this, n] (Project& p) { if (auto* t = p.findTrack (id)) t->name = n; }); };
            addAndMakeVisible (name);
            for (auto* b : { &rec, &mute, &solo }) { b->setClickingTogglesState (true); addAndMakeVisible (b); }
            rec.setColour (juce::TextButton::buttonOnColourId, colours::record);
            solo.setColour (juce::TextButton::buttonOnColourId, colours::accentWarm);
            rec.setTooltip ("Arm for recording");
            mute.setTooltip ("Mute");
            solo.setTooltip ("Solo");
            rec.onClick = [this] { const bool a = rec.getToggleState(); session.perform (a ? "Arm track" : "Disarm track", [this, a] (Project& p) { if (auto* t = p.findTrack (id)) { t->armed = a; t->monitorInput = a; } }); };
            mute.onClick = [this] { session.setTrackMute (id, mute.getToggleState()); };
            solo.onClick = [this] { session.setTrackSolo (id, solo.getToggleState()); };
            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange (-60, 12);
            volume.setDoubleClickReturnValue (true, 0);
            volume.onValueChange = [this] { session.setTrackVolume (id, (float) volume.getValue(), false); };
            volume.onDragEnd = [this] { session.setTrackVolume (id, (float) volume.getValue(), true); };
            addAndMakeVisible (volume);
            info.setFont (juce::FontOptions (11.0f));
            info.setColour (juce::Label::textColourId, colours::textDim);
            info.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (info);
            meter.source = trackMeterSource (session.engine(), id);
            addAndMakeVisible (meter);
            refresh();
        }

        void refresh()
        {
            auto* t = session.project().findTrack (id);
            if (t == nullptr) return;
            name.setText (t->name, juce::dontSendNotification);
            rec.setToggleState (t->armed, juce::dontSendNotification);
            rec.setVisible (t->type == TrackType::Audio);
            mute.setToggleState (t->mute, juce::dontSendNotification);
            solo.setToggleState (t->solo, juce::dontSendNotification);
            volume.setValue (t->volumeDb, juce::dontSendNotification);
            juce::String i;
            if (t->type == TrackType::Midi)
            {
                auto* inf = instruments::find (t->instrument.id);
                i = inf != nullptr ? inf->name : t->instrument.id;
            }
            else if (t->type == TrackType::Bus) i = "Bus";
            else i = t->vocalChain.enabled ? "Audio - " + t->vocalChain.preset : juce::String ("Audio");
            info.setText (i, juce::dontSendNotification);
            colour = juce::Colour (t->colour);
            selected = session.selectedTrackId == id;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (selected ? colours::panelRaised.brighter (0.05f) : colours::panelRaised);
            g.fillRect (getLocalBounds().withTrimmedBottom (1));
            g.setColour (colour);
            g.fillRect (0, 0, 4, getHeight() - 1);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            session.select (id);
            if (e.mods.isPopupMenu()) menu();
        }

        void menu()
        {
            juce::PopupMenu m, colourMenu, route;
            m.addItem (1, "Rename...");
            m.addItem (2, "Delete track");
            m.addItem (3, "Duplicate track");
            static const juce::uint32 palette[] { 0xff4f8cff, 0xffff6b6b, 0xffffc145, 0xff4cd4a0, 0xffb07cff, 0xff45c8ff, 0xffff8f45, 0xffe86bd1 };
            for (int i = 0; i < 8; ++i) colourMenu.addColouredItem (20 + i, "Colour " + juce::String (i + 1), juce::Colour (palette[i]));
            m.addSubMenu ("Colour", colourMenu);
            route.addItem (100, "Master");
            int k = 101;
            std::vector<juce::String> busIds;
            for (auto& b : session.project().tracks)
                if (b.type == TrackType::Bus && b.id != id) { route.addItem (k++, b.name); busIds.push_back (b.id); }
            m.addSubMenu ("Output", route);
            m.addItem (4, "Folder...");
            m.showMenuAsync (juce::PopupMenu::Options(), [this, busIds] (int r)
            {
                if (r == 1) name.showEditor();
                else if (r == 2) { auto tid = id; session.perform ("Delete track", [tid] (Project& p) { p.removeTrack (tid); }); }
                else if (r == 3)
                {
                    auto tid = id;
                    session.perform ("Duplicate track", [tid] (Project& p)
                    {
                        if (auto* t = p.findTrack (tid))
                        {
                            Track copy = *t;
                            copy.id = makeId ("trk");
                            copy.name << " copy";
                            for (auto& c : copy.clips) c.id = makeId ("clip");
                            copy.generator.part.clear();
                            p.tracks.push_back (copy);
                        }
                    });
                }
                else if (r >= 20 && r < 28)
                {
                    static const juce::uint32 pal[] { 0xff4f8cff, 0xffff6b6b, 0xffffc145, 0xff4cd4a0, 0xffb07cff, 0xff45c8ff, 0xffff8f45, 0xffe86bd1 };
                    auto tid = id; auto col = pal[r - 20];
                    session.perform ("Track colour", [=] (Project& p) { if (auto* t = p.findTrack (tid)) t->colour = col; });
                }
                else if (r >= 100)
                {
                    auto tid = id;
                    auto bus = r == 100 ? juce::String() : busIds[(size_t) (r - 101)];
                    session.perform ("Route track", [=] (Project& p) { if (auto* t = p.findTrack (tid)) t->outputBusId = bus; });
                }
            });
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 4).withTrimmedLeft (4);
            meter.setBounds (r.removeFromRight (10));
            r.removeFromRight (4);
            auto top = r.removeFromTop (22);
            auto buttons = top.removeFromRight (84);
            name.setBounds (top);
            rec.setBounds (buttons.removeFromLeft (28).reduced (1));
            mute.setBounds (buttons.removeFromLeft (28).reduced (1));
            solo.setBounds (buttons.removeFromLeft (28).reduced (1));
            info.setBounds (r.removeFromTop (18));
            volume.setBounds (r.removeFromTop (22));
        }

        Session& session;
        juce::String id;
        juce::Label name, info;
        juce::TextButton rec { "R" }, mute { "M" }, solo { "S" };
        juce::Slider volume;
        LevelMeter meter { true };
        juce::Colour colour;
        bool selected = false;
    };

    struct Repainter : public juce::ChangeListener
    {
        explicit Repainter (TimelineView& o) : owner (o) {}
        void changeListenerCallback (juce::ChangeBroadcaster*) override { owner.repaint(); }
        TimelineView& owner;
    } repainter { *this };

    void timerCallback() override
    {
        // Follow the playhead while playing.
        if (session.engine().isPlaying())
        {
            const int px = beatToX (session.engine().getPositionBeats());
            if (px > getWidth() - 60 || px < headerWidth)
            {
                scrollBeat = juce::jmax (0.0, session.engine().getPositionBeats() - 2.0);
                updateScrollbars();
                layoutHeaders();
            }
            repaint();
        }
        else if (std::abs (lastPos - session.engine().getPositionBeats()) > 1.0e-6)
            repaint();
        lastPos = session.engine().getPositionBeats();
    }

    Session& session;
    juce::OwnedArray<TrackHeader> headers;
    juce::ScrollBar hScroll { false }, vScroll { true };
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::AudioThumbnailCache> thumbCache;
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbs;
    double lastPos = -1;
};

void TimelineView::warpClip (const juce::String& trackId, const juce::String& clipId)
{
    // Warp: detect onsets in the clip and pull each one to the nearest 16th.
    auto* c = session.project().findTrack (trackId) ? const_cast<Project&> (session.project()).findTrack (trackId)->findClip (clipId) : nullptr;
    if (c == nullptr || c->isMidi) return;
    juce::AudioBuffer<float> audio;
    double rate = 0;
    if (! ClipCache::readFile (store::resolve (session.getFolder(), c->audioFile), audio, rate)) return;

    TempoMap tm (session.project());
    const double clipStartSec = tm.beatsToSeconds (c->startBeat);
    const int hop = (int) (0.01 * rate);
    std::vector<double> src { 0.0 }, dst { 0.0 };
    float prev = 0;
    double lastOnset = -1;
    const int startSample = (int) (c->sourceOffsetSeconds * rate);
    for (int i = startSample; i + hop < audio.getNumSamples(); i += hop)
    {
        float e = 0;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch) e = std::max (e, audio.getRMSLevel (ch, i, hop));
        const float db = juce::Decibels::gainToDecibels (e, -80.0f);
        const double t = (i - startSample) / rate;
        if (db - prev > 9.0f && db > -40.0f && t - lastOnset > 0.12)
        {
            const double beat = tm.secondsToBeats (clipStartSec + t);
            const double target = std::round (beat * 4.0) / 4.0;
            const double targetT = tm.beatsToSeconds (target) - clipStartSec;
            if (std::abs (targetT - t) < 0.08 && targetT > dst.back() + 0.02 && t > src.back() + 0.02)
            {
                src.push_back (t);
                dst.push_back (targetT);
            }
            lastOnset = t;
        }
        prev = db;
    }
    const double total = (audio.getNumSamples() - startSample) / rate;
    src.push_back (total);
    dst.push_back (dst.back() + (total - src[src.size() - 2]));
    if (src.size() <= 2) { session.status ("No clear notes to warp in this clip."); return; }

    juce::AudioBuffer<float> trimmed (audio.getNumChannels(), audio.getNumSamples() - startSample);
    for (int ch = 0; ch < audio.getNumChannels(); ++ch) trimmed.copyFrom (ch, 0, audio, ch, startSample, trimmed.getNumSamples());
    auto warped = dsp::timestretch::warp (trimmed, rate, src, dst);
    auto out = store::audioFolder (session.getFolder()).getChildFile (juce::File (c->audioFile).getFileNameWithoutExtension() + "_warped.wav").getNonexistentSibling();
    writeAudioFile (out, warped, rate, 32);
    const auto rel = store::relativePath (session.getFolder(), out);
    const int moved = (int) src.size() - 2;
    session.perform ("Warp clip", [=] (Project& p)
    {
        if (auto* cc = p.findTrack (trackId)->findClip (clipId)) { cc->audioFile = rel; cc->sourceOffsetSeconds = 0; }
    });
    session.status ("Warped " + juce::String (moved) + " notes onto the grid. Undo restores the original.");
}

//==============================================================================
ArrangeScreen::ArrangeScreen (Session& s) : Screen (s)
{
    timeline = std::make_unique<TimelineView> (s);
    timeline->onOpenPianoRoll = [this] { if (navigate) navigate ("pianoroll"); };
    addAndMakeVisible (*timeline);

    for (auto* b : { &addTrack, &importButton, &addSection, &split, &duplicate, &del, &zoomOut, &zoomIn, &snapButton, &tempoKey, &snapshot,
                     &assistantToggle, &automationToggle })
        addAndMakeVisible (b);
    addTrack.onClick = [this] { addTrackMenu(); };
    importButton.onClick = [this] { importAudio(); };
    addSection.onClick = [this] { addSectionMenu(); };
    split.onClick = [this] { timeline->splitSelected(); };
    duplicate.onClick = [this] { timeline->duplicateSelected(); };
    del.onClick = [this] { timeline->deleteSelected(); };
    zoomIn.onClick = [this] { timeline->zoom (1.4); };
    zoomOut.onClick = [this] { timeline->zoom (1.0 / 1.4); };
    makeToggle (snapButton, "Snap", true);
    snapButton.onClick = [this] { timeline->snap = snapButton.getToggleState(); };
    tempoKey.onClick = [this] { tempoKeyDialog(); };
    snapshot.setTooltip ("Save a named snapshot (e.g. \"before the bridge rewrite\")");
    snapshot.onClick = [this]
    {
        timeline->promptText ("Snapshot name", "before ", [this] (juce::String n)
        {
            auto r = store::saveSnapshot (session.project(), session.getFolder(), n);
            session.status (r.wasOk() ? "Snapshot saved: " + n : r.getErrorMessage());
        });
    };
    makeToggle (assistantToggle, "Assistant", true);
    assistantToggle.onClick = [this] { resized(); };
    makeToggle (automationToggle, "Automation", false);
    automationToggle.onClick = [this]
    {
        timeline->showAutomation = automationToggle.getToggleState();
        timeline->rebuildHeaders();
    };

    grid.addItemList ({ "1 bar", "1/2", "1/4 (beat)", "1/8", "1/16" }, 1);
    grid.setSelectedId (3, juce::dontSendNotification);
    grid.onChange = [this]
    {
        static const double values[] { 4.0, 2.0, 1.0, 0.5, 0.25 };
        timeline->gridBeats = values[grid.getSelectedId() - 1] * (grid.getSelectedId() == 1 ? session.project().beatsPerBar() / 4.0 : 1.0);
    };
    grid.setTooltip ("Grid for snapping (hold Alt to move freely)");
    addAndMakeVisible (grid);

    assistantTitle.setText ("Arrangement assistant", juce::dontSendNotification);
    styleHeading (assistantTitle, 15);
    assistantPanel.addAndMakeVisible (assistantTitle);
    issuesView.setViewedComponent (&issues, false);
    issuesView.setScrollBarsShown (true, false);
    assistantPanel.addAndMakeVisible (issuesView);
    addAndMakeVisible (assistantPanel);
    setWantsKeyboardFocus (true);
}

ArrangeScreen::~ArrangeScreen() = default;

void ArrangeScreen::refresh (bool structural)
{
    if (structural)
    {
        timeline->rebuildHeaders();
        updateAssistant();
    }
    else
        timeline->refreshHeaders();
}

void ArrangeScreen::updateAssistant()
{
    std::vector<IssueList::Item> items;
    for (auto& i : arrange::analyse (session.project()))
    {
        IssueList::Item it;
        it.level = i.severity == arrange::Issue::Severity::Warning ? 1 : 0;
        it.title = i.severity == arrange::Issue::Severity::Warning ? "Check this" : "Note";
        it.detail = i.message;
        if (i.sectionId.isNotEmpty())
        {
            auto id = i.sectionId;
            it.actionLabel = "Show";
            it.action = [this, id] { session.selectedSectionId = id; if (auto* s = const_cast<Project&> (session.project()).findSection (id)) session.engine().setPositionBeats (s->startBeat); timeline->repaint(); };
        }
        items.push_back (it);
    }
    if (session.project().sections.empty())
    {
        IssueList::Item it;
        it.level = 1;
        it.title = "Suggest a structure";
        it.detail = "Lay out intro, verses, choruses and an outro for this style and a 3:30 song.";
        it.actionLabel = "Suggest";
        it.action = [this]
        {
            session.perform ("Suggest structure", [] (Project& p)
            {
                p.sections = arrange::suggestStructure (p.style.isNotEmpty() ? p.style : juce::String ("worship_ballad"), 210, p.bpm, p.beatsPerBar());
            });
        };
        items.insert (items.begin(), it);
    }
    issues.setItems (items);
    issues.setSize (juce::jmax (100, issuesView.getWidth() - 10), issues.preferredHeight());
}

void ArrangeScreen::addTrackMenu()
{
    juce::PopupMenu m, inst;
    m.addItem (1, "Audio track (vocal, instrument)");
    m.addItem (2, "Audio track (vocal with vocal chain)");
    m.addItem (4, "Bus (group)");
    for (auto& fam : instruments::families())
    {
        juce::PopupMenu sub;
        int k = 0;
        for (auto& i : instruments::catalogue())
        {
            ++k;
            if (i.family == fam) sub.addItem (1000 + k, i.name);
        }
        inst.addSubMenu (fam, sub);
    }
    m.addSubMenu ("Instrument track", inst);
    m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
    {
        if (r <= 0) return;
        juce::String newId;
        session.perform ("Add track", [&] (Project& p)
        {
            if (r == 1 || r == 2)
            {
                auto& t = p.addTrack (TrackType::Audio, r == 2 ? "Vocal" : "Audio", r == 2 ? TrackRole::LeadVocal : TrackRole::Unknown);
                t.vocalChain.enabled = r == 2;
                newId = t.id;
            }
            else if (r == 4) newId = p.addTrack (TrackType::Bus, "Bus").id;
            else if (r >= 1000)
            {
                auto& info = instruments::catalogue()[(size_t) (r - 1001)];
                auto& t = p.addTrack (TrackType::Midi, info.name, info.role);
                t.instrument.id = info.id;
                newId = t.id;
            }
        });
        if (newId.isNotEmpty()) session.select (newId);
    });
}

void ArrangeScreen::addSectionMenu()
{
    timeline->addSectionHere (session.project().lengthInBeats() + 1000); // past the end: appends
}

void ArrangeScreen::importAudio()
{
    chooser = std::make_unique<juce::FileChooser> ("Import audio (a beat, stems from another DAW, a recording)", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
    {
        auto files = fc.getResults();
        if (files.isEmpty()) return;
        const double at = session.engine().getPositionBeats();
        juce::StringArray errors;
        session.perform ("Import audio", [&] (Project& p)
        {
            for (auto& f : files)
            {
                auto r = store::importAudio (p, session.getFolder(), f, std::floor (at));
                if (r.failed()) errors.add (r.getErrorMessage());
            }
        });
        if (! errors.isEmpty()) tell ("Some files could not be imported", errors.joinIntoString ("\n"));
    });
}

void ArrangeScreen::tempoKeyDialog()
{
    auto* w = new juce::AlertWindow ("Tempo and key track", "Add a tempo or key change at the playhead (e.g. a final-chorus lift).", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("bpm", juce::String (session.project().bpm, 1), "Tempo from here (BPM)");
    juce::StringArray keys;
    for (int pc = 0; pc < 12; ++pc) { keys.add (theory::pitchClassName (pc) + " major"); keys.add (theory::pitchClassName (pc) + " minor"); }
    w->addComboBox ("key", keys, "Key from here");
    w->getComboBoxComponent ("key")->setText (session.project().keyAt (session.engine().getPositionBeats()).toString());
    w->addButton ("Add at playhead", 1);
    w->addButton ("Set song tempo/key", 2);
    w->addButton ("Clear changes", 3);
    w->addButton ("Cancel", 0);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
    {
        if (r == 0) return;
        const double bpm = juce::jlimit (30.0, 300.0, w->getTextEditorContents ("bpm").getDoubleValue());
        const auto key = Key::fromString (w->getComboBoxComponent ("key")->getText());
        const double at = std::round (session.engine().getPositionBeats());
        session.perform ("Tempo/key change", [=] (Project& p)
        {
            if (r == 3) { p.tempoEvents.clear(); p.keyEvents.clear(); return; }
            if (r == 2 || at <= 0) { p.bpm = bpm; p.key = key; return; }
            if (std::abs (bpm - p.bpm) > 0.01) p.tempoEvents.push_back ({ at, bpm });
            if (! (key == p.keyAt (at))) p.keyEvents.push_back ({ at, key });
        });
    }), true);
}

bool ArrangeScreen::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == 'S' && ! k.getModifiers().isCommandDown()) { timeline->splitSelected(); return true; }
    if (k.getKeyCode() == 'D' && k.getModifiers().isCommandDown()) { timeline->duplicateSelected(); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { timeline->deleteSelected(); return true; }
    if (k.getKeyCode() == '=' || k.getKeyCode() == '+') { timeline->zoom (1.4); return true; }
    if (k.getKeyCode() == '-') { timeline->zoom (1.0 / 1.4); return true; }
    return false;
}

void ArrangeScreen::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    if (assistantPanel.isVisible())
        paintPanel (g, assistantPanel.getBounds().toFloat());
}

void ArrangeScreen::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto bar = r.removeFromTop (36);
    for (auto* b : { &addTrack, &importButton, &addSection })
    {
        b->setBounds (bar.removeFromLeft (b == &importButton ? 110 : 90).reduced (2));
    }
    bar.removeFromLeft (10);
    for (auto* b : { &split, &duplicate, &del }) b->setBounds (bar.removeFromLeft (84).reduced (2));
    bar.removeFromLeft (10);
    zoomOut.setBounds (bar.removeFromLeft (34).reduced (2));
    zoomIn.setBounds (bar.removeFromLeft (34).reduced (2));
    snapButton.setBounds (bar.removeFromLeft (64).reduced (2));
    grid.setBounds (bar.removeFromLeft (110).reduced (2));
    bar.removeFromLeft (10);
    tempoKey.setBounds (bar.removeFromLeft (104).reduced (2));
    automationToggle.setBounds (bar.removeFromLeft (100).reduced (2));
    assistantToggle.setBounds (bar.removeFromRight (100).reduced (2));
    snapshot.setBounds (bar.removeFromRight (96).reduced (2));
    r.removeFromTop (6);

    assistantPanel.setVisible (assistantToggle.getToggleState());
    if (assistantPanel.isVisible())
    {
        auto side = r.removeFromRight (300);
        r.removeFromRight (8);
        assistantPanel.setBounds (side);
        auto inner = assistantPanel.getLocalBounds().reduced (10);
        assistantTitle.setBounds (inner.removeFromTop (24));
        issuesView.setBounds (inner);
        issues.setSize (juce::jmax (100, inner.getWidth() - 10), issues.preferredHeight());
    }
    timeline->setBounds (r);
}

} // namespace studio::ui
