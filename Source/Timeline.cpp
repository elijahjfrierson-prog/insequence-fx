#include "App.h"
#include <set>

namespace isfx
{
Timeline::Timeline (AppState& s) : st (s) { setWantsKeyboardFocus (false); }

int Timeline::trackY (int i) const
{
    int y = kRuler - vScroll;
    const auto& tr = st.project.seq().tracks;
    for (int k = 0; k < i && k < (int) tr.size(); ++k) y += tr[(size_t) k].height;
    return y;
}

int Timeline::trackAt (int y) const
{
    const auto& tr = st.project.seq().tracks;
    if (y < kRuler) return -1;
    int yy = kRuler - vScroll;
    for (int k = 0; k < (int) tr.size(); ++k) { if (y >= yy && y < yy + tr[(size_t) k].height) return k; yy += tr[(size_t) k].height; }
    return y < kRuler ? -1 : (int) tr.size() - 1;
}

const Clip* Timeline::clipAt (juce::Point<int> p, int& edge) const
{
    edge = 0;
    int tr = trackAt (p.y);
    if (tr < 0 || p.x < kHeader) return nullptr;
    const auto& s = st.project.seq();
    for (auto& c : s.clips)
    {
        if (c.track != tr) continue;
        int x0 = secToX (ticksToSec (c.start)), x1 = secToX (ticksToSec (c.end()));
        if (p.x >= x0 && p.x <= x1)
        {
            int w = std::min (7, (x1 - x0) / 3);
            if (p.x - x0 <= w) edge = -1; else if (x1 - p.x <= w) edge = 1;
            return &c;
        }
    }
    return nullptr;
}

void Timeline::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto& p = st.project; const auto& s = p.seq();
    g.fillAll (bg);
    const int W = getWidth(), H = getHeight();
    const double t0 = scrollSec, t1 = xToSec (W);

    // track lanes
    for (int i = 0; i < (int) s.tracks.size(); ++i)
    {
        auto& tr = s.tracks[(size_t) i];
        int y = trackY (i), h = tr.height;
        g.setColour (i % 2 ? panel : panel.darker (0.15f)); g.fillRect (kHeader, y, W - kHeader, h);
        g.setColour (line); g.drawHorizontalLine (y + h - 1, 0, (float) W);
    }

    // beat grid
    if (p.song.valid && st.showGrid)
    {
        double beat = p.song.beatSec(), pxBeat = beat * pxPerSec;
        int per = pxBeat > 160 ? 4 : pxBeat > 60 ? 2 : 1;
        if (pxBeat < 6) per = 0;
        juce::int64 n0 = (juce::int64) std::floor (p.song.beatAt (secToTicks (t0)) * std::max (1, per)) - 1;
        juce::int64 n1 = (juce::int64) std::ceil (p.song.beatAt (secToTicks (t1)) * std::max (1, per)) + 1;
        for (juce::int64 n = std::max<juce::int64> (n0, 0); n <= n1; ++n)
        {
            int perB = std::max (1, per);
            Ticks t = p.song.subdivTime (n, perB);
            int x = secToX (ticksToSec (t));
            bool isBeat = n % perB == 0, isBar = isBeat && (n / perB) % p.song.beatsPerBar == 0;
            if (! isBeat && per == 0) continue;
            if (! isBar && pxBeat * p.song.beatsPerBar < 24) continue;
            g.setColour (isBar ? line.brighter (0.5f) : isBeat ? line.brighter (0.1f) : line.withAlpha (0.5f));
            g.drawVerticalLine (x, (float) kRuler, (float) H);
        }
    }

    // range
    if (st.rangeB > st.rangeA)
    {
        int a = secToX (ticksToSec (st.rangeA)), b = secToX (ticksToSec (st.rangeB));
        g.setColour (accent2.withAlpha (0.12f)); g.fillRect (a, 0, b - a, H);
        g.setColour (accent2.withAlpha (0.7f)); g.drawVerticalLine (a, 0, (float) H); g.drawVerticalLine (b, 0, (float) H);
    }

    // clips
    for (auto& c : s.clips)
    {
        if (c.track < 0 || c.track >= (int) s.tracks.size()) continue;
        auto& tr = s.tracks[(size_t) c.track];
        int x0 = secToX (ticksToSec (c.start)), x1 = secToX (ticksToSec (c.end()));
        if (x1 < kHeader || x0 > W) continue;
        juce::Rectangle<int> r (x0, trackY (c.track) + 2, std::max (2, x1 - x0), tr.height - 5);
        if (r.getBottom() < 0 || r.getY() > getHeight()) continue;
        const MediaItem* m = p.findMedia (c.mediaId);
        bool audio = tr.kind == TrackKind::Audio, sel = st.selected.contains (c.id);
        auto base = audio ? juce::Colour (0xff1f6f5c) : juce::Colour (0xff3a3f7a);
        if (m && m->missing) base = bad.darker (0.6f);
        static const juce::uint32 labels[] { 0, 0xff8e6bd6, 0xff4f7fe0, 0xff2fb3a8, 0xffb38be0, 0xff3aa0d8, 0xff4c9a4c, 0xffd85c8a, 0xffe0963a };
        if (c.label > 0 && c.label < 9) base = juce::Colour (labels[c.label]).darker (0.35f);
        if (tr.hidden || tr.mute || ! c.enabled) base = base.withSaturation (0.1f).darker (0.4f);
        g.setColour (base); g.fillRoundedRectangle (r.toFloat(), 3);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (r);
        if (! audio && m)
        {
            auto it = st.thumbs.find (m->id);
            if (it != st.thumbs.end() && it->second.isValid() && r.getWidth() > 24)
            {
                auto& im = it->second;
                int tw = im.getWidth() * r.getHeight() / std::max (1, im.getHeight());
                for (int x = r.getX(); x < r.getRight(); x += tw * 4) g.drawImage (im, x, r.getY(), tw, r.getHeight(), 0, 0, im.getWidth(), im.getHeight());
                g.setColour (base.withAlpha (0.35f)); g.fillRect (r);
            }
        }
        if (m)
        {
            auto wit = st.waves.find (m->id);
            if (wit != st.waves.end() && (audio || r.getHeight() > 30))
            {
                auto& wf = wit->second;
                g.setColour (audio ? good.withAlpha (0.8f) : juce::Colours::white.withAlpha (0.25f));
                float mid = r.getCentreY() + (audio ? 0.0f : r.getHeight() * 0.25f), amp = r.getHeight() * (audio ? 0.45f : 0.2f);
                for (int x = std::max (r.getX(), kHeader); x < std::min (r.getRight(), W); ++x)
                {
                    double ts = ticksToSec (c.srcAt (secToTicks (xToSec (x))));
                    double te = ticksToSec (c.srcAt (secToTicks (xToSec (x + 1))));
                    int b0 = (int) (ts * wf.bucketsPerSec), b1 = std::max (b0 + 1, (int) (te * wf.bucketsPerSec));
                    float pk = 0;
                    for (int b = std::max (0, b0); b < b1 && b < (int) wf.peaks.size(); ++b) pk = std::max (pk, wf.peaks[(size_t) b]);
                    g.drawVerticalLine (x, mid - pk * amp, mid + pk * amp);
                }
            }
        }
        if (c.transIn != TransitionType::Cut && c.transLen > 0)
        {   // transition span: drag its right edge to make it longer/shorter
            const int xe = std::min (r.getRight(), secToX (ticksToSec (c.start + c.transLen)));
            g.setColour (juce::Colours::white.withAlpha (0.13f)); g.fillRect (r.getX(), r.getY(), std::max (0, xe - r.getX()), r.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.7f)); g.fillRect (xe - 1, r.getY() + 2, 2, r.getHeight() - 4);
        }
        g.setColour (text); g.setFont (11.0f);
        juce::String label = c.name.isNotEmpty() ? c.name : (m ? m->name : juce::String ("(missing)"));
        if (! c.enabled) label = "[off] " + label;
        if (c.keep) label = "[keep] " + label;
        if (c.ramped()) label << "  ramp";
        else if (c.speed == 0.0) label << "  hold";
        else if (c.speed != 1.0) label << "  " << juce::String (c.speed * 100, 0) << "%";
        g.drawText (label, r.reduced (4, 2), juce::Justification::topLeft, true);
        {   // CapCut-style badges: how many effects / keyframes / transition on this clip
            static const juce::StringArray meta { "scale", "posX", "posY", "rotation", "reframe", "maskShape", "maskW", "maskH",
                                                  "maskFeather", "trAmt", "trDir", "anchorX", "anchorY", "slowMove" };
            int nfx = 0, nkf = 0;
            for (auto& [k, v] : c.fx.p) if (! meta.contains (k) && v != 0.0f) ++nfx;
            for (auto& [k, ks] : c.keys) nkf += (int) ks.size();
            juce::StringArray bs;
            if (nfx > 0) bs.add ("fx " + juce::String (nfx));
            if (nkf > 0) bs.add (juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x86 ")) + juce::String (nkf));
            if (c.transIn != TransitionType::Cut) bs.add ("tr");
            if (c.opacity < 1.0f) bs.add (juce::String (juce::roundToInt (c.opacity * 100)) + "%");
            if (r.getHeight() >= 26)
            {
                g.setFont (9.5f);
                int bx = r.getX() + 4; const int by = r.getBottom() - 14;
                for (auto& b : bs)
                {
                    const int bw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), b) + 8;
                    if (bx + bw > r.getRight() - 2) break;
                    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle ((float) bx, (float) by, (float) bw, 12.0f, 6.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.9f)); g.drawText (b, bx, by, bw, 12, juce::Justification::centred);
                    bx += bw + 3;
                }
            }
        }
        if (m && m->kind == MediaKind::Video && m->sync.status != SyncInfo::None)
        {
            auto col = m->sync.status == SyncInfo::Synced || m->sync.status == SyncInfo::Manual ? good : m->sync.status == SyncInfo::Uncertain ? warn : bad;
            g.setColour (col); g.fillEllipse ((float) r.getRight() - 10, (float) r.getY() + 3, 6, 6);
        }
        if (sel) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3, 2); }
        if (c.locked) { g.setColour (dim); g.drawText ("L", r.reduced (3), juce::Justification::bottomRight); }
    }

    // effect ribbons: each effect / adjustment layer draws a thin band in its colour across every clip it covers (and on itself)
    {
        int lane = 0;
        for (auto& e : s.clips)
        {
            if (e.mediaId.isNotEmpty() || ! e.enabled || e.track < 0 || e.track >= (int) s.tracks.size() || s.tracks[(size_t) e.track].hidden) continue;
            const auto col = juce::Colour::fromHSV ((float) (e.name.hashCode() & 0xff) / 255.0f, 0.65f, 0.95f, 0.95f);
            const int off = 2 + (lane++ % 3) * 4;
            for (auto& c : s.clips)
            {
                if (c.track < 0 || c.track >= (int) s.tracks.size() || s.tracks[(size_t) c.track].kind != TrackKind::Video) continue;
                if (c.mediaId.isEmpty() && &c != &e) continue;
                const Ticks a0 = std::max (c.start, e.start), b0 = std::min (c.end(), e.end());
                if (b0 <= a0) continue;
                const int xa = std::max (kHeader, secToX (ticksToSec (a0))), xb = std::min (W, secToX (ticksToSec (b0)));
                if (xb <= xa) continue;
                g.setColour (col); g.fillRect (xa, trackY (c.track) + off, xb - xa, 3);
            }
        }
    }

    // transition dots on every cut: white = empty, black = has a transition. Click one to pick a transition.
    for (auto& d : cutDots())
    {
        if (d.x < kHeader || d.x > W) continue;
        auto e = juce::Rectangle<float> (7.0f, 7.0f).withCentre (d.centre.toFloat());
        g.setColour (d.full ? juce::Colours::black : juce::Colours::white); g.fillEllipse (e);
        g.setColour (d.full ? juce::Colours::white : juce::Colours::black.withAlpha (0.6f)); g.drawEllipse (e, 1.0f);
    }

    // ruler
    g.setColour (panel2); g.fillRect (0, 0, W, kRuler);
    g.setColour (dim); g.setFont (10.0f);
    if (p.song.valid && st.snap < SnapMode::Sec1)
    {
        double barSec = p.song.beatSec() * p.song.beatsPerBar;
        int step = 1; while (barSec * step * pxPerSec < 48) step *= 2;
        int b0 = std::max (0, (int) std::floor (p.song.beatAt (secToTicks (t0)) / p.song.beatsPerBar));
        for (int b = b0 - b0 % step; ; b += step)
        {
            double sec = ticksToSec (p.song.gridTime ((double) b * p.song.beatsPerBar));
            if (sec > t1) break;
            int x = secToX (sec);
            g.setColour (dim); g.drawVerticalLine (x, 14, (float) kRuler);
            g.drawText (juce::String (b + 1), x + 2, 1, 40, 12, juce::Justification::left);
        }
        // detected drum hits
        for (auto& e : p.song.events)
        {
            double sec = ticksToSec (e.time);
            if (sec < t0 || sec > t1) continue;
            int x = secToX (sec);
            g.setColour (e.type == EventType::Kick ? accent : e.type == EventType::Snare ? warn : accent2.withAlpha (0.6f));
            int y = e.type == EventType::Kick ? 22 : e.type == EventType::Snare ? 18 : 14;
            g.fillRect (x, y, 1, 4);
        }
    }
    else
    {
        double step = 60;
        for (double c : { 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0 })
            if (c * pxPerSec >= 60) { step = c; break; }
        for (double sec = std::floor (t0 / step) * step; sec < t1; sec += step)
        {
            int x = secToX (sec);
            g.drawVerticalLine (x, 16, (float) kRuler);
            const auto ms = (juce::int64) std::llround (sec * 1000);
            juce::String txt = juce::String (ms / 60000) + ":" + juce::String ((ms / 1000) % 60).paddedLeft ('0', 2);
            if (step < 1) txt << "." << juce::String (ms % 1000).paddedLeft ('0', 3);
            g.drawText (txt, x + 2, 2, 70, 12, juce::Justification::left);
        }
    }

    // headers
    g.setColour (panel2); g.fillRect (0, kRuler, kHeader, H - kRuler);
    for (int i = 0; i < (int) s.tracks.size(); ++i)
    {
        auto& tr = s.tracks[(size_t) i];
        int y = trackY (i);
        g.setColour (text); g.setFont (12.0f);
        g.drawText (tr.name, 6, y + 4, kHeader - 10, 14, juce::Justification::left);
        // Premiere-style toggles. Video: eye (visible) + lock. Audio: speaker (mute) + S (solo) + lock.
        const bool audio = tr.kind == TrackKind::Audio;
        for (int k = 0; k < (audio ? 3 : 2); ++k)
        {
            auto b = juce::Rectangle<int> (6 + k * 22, y + 22, 18, 16).toFloat();
            const int kind = audio ? k + 1 : (k == 0 ? 0 : 3);   // 0 eye, 1 speaker, 2 solo, 3 lock
            const bool active = kind == 0 ? ! tr.hidden : kind == 1 ? ! tr.mute : kind == 2 ? tr.solo : tr.locked;
            const bool warn = (kind == 0 && tr.hidden) || (kind == 1 && tr.mute);
            g.setColour (warn ? juce::Colour (0xff5a1e24) : (kind >= 2 && active) ? accent : line); g.fillRoundedRectangle (b, 3);
            g.setColour (active || warn ? juce::Colours::white : dim);
            auto c = b.getCentre();
            if (kind == 0)
            {
                juce::Path eye; eye.startNewSubPath (c.x - 6, c.y); eye.quadraticTo (c.x, c.y - 6, c.x + 6, c.y); eye.quadraticTo (c.x, c.y + 6, c.x - 6, c.y);
                g.strokePath (eye, juce::PathStrokeType (1.3f)); g.fillEllipse (c.x - 2, c.y - 2, 4, 4);
            }
            else if (kind == 1)
            {
                juce::Path sp; sp.addRectangle (c.x - 5, c.y - 2, 3, 4);
                sp.addTriangle (c.x - 2, c.y - 2, c.x + 2, c.y - 5.5f, c.x + 2, c.y + 5.5f); sp.addTriangle (c.x - 2, c.y - 2, c.x + 2, c.y + 5.5f, c.x - 2, c.y + 2);
                g.fillPath (sp);
                if (! tr.mute) g.drawLine (c.x + 4, c.y - 3, c.x + 4, c.y + 3, 1.2f);
            }
            else if (kind == 2) { g.setFont (10.0f); g.drawText ("S", b, juce::Justification::centred); }
            else
            {
                g.drawRect (juce::Rectangle<float> (c.x - 4, c.y - 1, 8, 6), 1.2f);
                juce::Path sh; sh.addCentredArc (c.x, c.y - 1, 2.6f, 3.0f, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
                g.strokePath (sh, juce::PathStrokeType (1.2f));
            }
            if (warn) g.drawLine (b.getX() + 3, b.getBottom() - 3, b.getRight() - 3, b.getY() + 3, 1.4f);
        }
        g.setColour (line); g.drawHorizontalLine (y + tr.height - 1, 0, (float) kHeader);
    }
    g.setColour (line); g.drawVerticalLine (kHeader - 1, 0, (float) H);
    g.setColour (panel2); g.fillRect (0, 0, kHeader, kRuler);
    g.setColour (dim); g.setFont (10.0f);
    g.drawText (p.song.valid ? juce::String (p.song.bpm, 2) + " BPM" : juce::String ("no song"), 4, 0, kHeader - 6, kRuler, juce::Justification::centredLeft);

    if (drag == Drag::Move && dragOut)
    {
        g.setColour (juce::Colours::red.withAlpha (0.18f)); g.fillRect (getLocalBounds());
        g.setColour (juce::Colours::white); g.setFont (16.0f);
        g.drawText ("Release to pull " + juce::String (st.selected.size()) + " clip(s) out   -   hold Alt to close the gap",
                    getLocalBounds().removeFromTop (kRuler + 40), juce::Justification::centred);
    }
    if (snapGuide >= 0 && drag != Drag::None && ! dragOut)
    {
        int gx = secToX (ticksToSec (snapGuide));
        g.setColour (accent2); g.drawVerticalLine (gx, (float) kRuler, (float) H);
        g.fillEllipse ((float) gx - 3, (float) kRuler - 3, 6, 6);
    }

    // playhead
    int px = secToX (ticksToSec (st.playhead));
    if (px >= kHeader)
    {
        g.setColour (accent); g.drawVerticalLine (px, 0, (float) H);
        juce::Path tri;   // grab handle: drag to scrub
        tri.addRoundedRectangle ((float) px - 6, 1, 12, 12, 2.5f);
        tri.addTriangle ((float) px - 6, 12, (float) px + 6, 12, (float) px, 19);
        g.fillPath (tri);
    }
    if (drag == Drag::Box && ! box.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRect (box);
        g.setColour (juce::Colours::white.withAlpha (0.6f)); g.drawRect (box, 1);
    }
}

Ticks Timeline::magnetSnap (Ticks t, Ticks len)
{
    snapGuide = -1;
    if (! st.snapOn) return t;
    const Ticks thr = secToTicks (10.0 / pxPerSec);
    std::vector<Ticks> cands { 0, st.playhead };
    for (auto& c : st.project.seq().clips)
        if (c.id != dragClip && ! st.selected.contains (c.id)) { cands.push_back (c.start); cands.push_back (c.end()); }
    Ticks best = -1, bestD = thr + 1, guide = -1;
    for (auto cand : cands)
    {
        if (std::abs (cand - t) < bestD) { bestD = std::abs (cand - t); best = cand; guide = cand; }
        if (len > 0 && std::abs (cand - (t + len)) < bestD) { bestD = std::abs (cand - (t + len)); best = cand - len; guide = cand; }
    }
    if (best >= 0 && bestD <= thr) { snapGuide = guide; return best; }
    Ticks g = st.snapT (t);
    snapGuide = g;
    return g;
}

void Timeline::mouseMove (const juce::MouseEvent& e)
{
    int edge = 0; auto* c = clipAt (e.getPosition(), edge);
    if (tool == Tool::Cut) setMouseCursor (juce::MouseCursor::IBeamCursor);
    else if (tool == Tool::Select && c && edge != 0) setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (tool == Tool::Paint) setMouseCursor (juce::MouseCursor::CrosshairCursor);
    else setMouseCursor (juce::MouseCursor::NormalCursor);
    if (c)
    {
        auto* m = st.project.findMedia (c->mediaId);
        juce::String tip;
        if (m) { tip << m->name; if (m->sync.status != SyncInfo::None) tip << "\nSync: " << m->sync.note << " (" << juce::String (m->sync.confidence * 100, 0) << "%)"; }
        setTooltip (tip);
    }
    else setTooltip ("E select/move/trim, P paint, D delete, C cut. Drag in the ruler to select a range. Ctrl+wheel zooms.");
}

void Timeline::addAdjustmentLayer (Ticks at)
{
    const auto& song = st.project.song;
    const Ticks len = song.valid ? secToTicks (song.beatSec() * song.beatsPerBar * 4) : secToTicks (8.0);
    st.edit ("New Adjustment Layer");
    FxStack fx;   // empty: a neutral layer must not override sequence settings until effects are added
    auto id = addEffectClip (st.project.seq(), fx, "Adjustment Layer", magnetSnap (at, 0), len);
    if (id.isNotEmpty()) st.selected = { id };
    if (onEdited) onEdited();
    repaint();
}

std::vector<Timeline::CutDot> Timeline::cutDots() const
{
    const auto& s = st.project.seq();
    std::vector<CutDot> out;
    const Ticks tol = secToTicks (0.02);
    for (auto& c : s.clips)
    {
        if (c.track < 0 || c.track >= (int) s.tracks.size() || c.start <= 0) continue;
        bool adj = c.transIn != TransitionType::Cut;
        for (auto& o : s.clips)
            if (! adj && &o != &c && o.track == c.track && std::abs (o.end() - c.start) <= tol) adj = true;
        if (! adj) continue;
        const auto& tr = s.tracks[(size_t) c.track];
        const int x = secToX (ticksToSec (c.start));
        out.push_back ({ c.id, x, { x, trackY (c.track) + 2 + (tr.height - 5) / 2 }, c.transIn != TransitionType::Cut });
    }
    return out;
}

void Timeline::mouseDown (const juce::MouseEvent& e)
{
    auto& s = st.project.seq();
    auto pos = e.getPosition();
    Ticks t = secToTicks (std::max (0.0, xToSec (pos.x)));
    drag = Drag::None;
    beforeDrag = st.project;
    dragStart = t;

    if (pos.x >= kHeader && pos.y >= kRuler)
        for (auto& d : cutDots())
            if (d.centre.getDistanceFrom (pos) <= 6)
            {
                st.selected = { d.clipId };
                repaint();
                clipMenu (d.clipId);
                return;
            }

    if (pos.x < kHeader && pos.y >= kRuler)   // track header buttons
    {
        int tr = trackAt (pos.y); if (tr < 0) return;
        int k = (pos.x - 6) / 22; int y = trackY (tr);
        const bool audio = s.tracks[(size_t) tr].kind == TrackKind::Audio;
        if (e.mods.isPopupMenu())
        {   // FL-style: reorder tracks; clips travel with their track
            auto canSwap = [&] (int o) { return o >= 0 && o < (int) s.tracks.size() && s.tracks[(size_t) o].kind == s.tracks[(size_t) tr].kind; };
            juce::PopupMenu m;
            m.addItem (1, "Move track up", canSwap (tr - 1));
            m.addItem (2, "Move track down", canSwap (tr + 1));
            if (audio) { m.addSeparator(); m.addItem (3, "Plug-ins..."); }
            m.showMenuAsync (juce::PopupMenu::Options(), [this, tr, canSwap] (int r)
            {
                auto& sq = st.project.seq();
                if (r == 3) { if (onTrackPlugins) onTrackPlugins (tr); return; }
                if (r != 1 && r != 2) return;
                const int o = r == 1 ? tr - 1 : tr + 1;
                if (o < 0 || o >= (int) sq.tracks.size() || sq.tracks[(size_t) o].kind != sq.tracks[(size_t) tr].kind) return;
                st.edit ("Move track");
                std::swap (sq.tracks[(size_t) tr], sq.tracks[(size_t) o]);
                for (auto& c : sq.clips) { if (c.track == tr) c.track = o; else if (c.track == o) c.track = tr; }
                sq.sortClips();
                if (onEdited) onEdited();
                repaint();
            });
            return;
        }
        if (pos.y >= y + 22 && pos.y < y + 38 && k >= 0 && k < (audio ? 3 : 2))
        {
            st.edit ("Track toggle");
            auto& T = s.tracks[(size_t) tr];
            const int kind = audio ? k + 1 : (k == 0 ? 0 : 3);
            if (kind == 0) T.hidden = ! T.hidden;
            else if (kind == 1) T.mute = ! T.mute;
            else if (kind == 2) T.solo = ! T.solo;
            else T.locked = ! T.locked;
            if (onEdited) onEdited();
        }
        return;
    }
    if (pos.x >= kHeader && std::abs (pos.x - secToX (ticksToSec (st.playhead))) <= 6 && ! e.mods.isPopupMenu())
    {
        int ed = 0;
        if (pos.y < kRuler || clipAt (pos, ed) == nullptr || ed == 0)
        {
            drag = Drag::Scrub; repaint(); return;   // grabbed the playhead
        }
    }
    int edge0 = 0;
    const bool patternSel = pos.y >= kRuler && pos.x >= kHeader && e.mods.isCommandDown() && ! e.mods.isPopupMenu() && clipAt (pos, edge0) == nullptr;
    if (pos.y < kRuler || patternSel)
    {
        drag = Drag::Range;
        st.rangeA = st.rangeB = st.snapT (t);
        if (onSeek) onSeek (st.snapT (t));
        repaint(); return;
    }

    int edge = 0; const Clip* c = clipAt (pos, edge);
    if (e.mods.isPopupMenu())
    {
        if (c) { clipMenu (c->id); return; }
        juce::PopupMenu m;
        m.addItem (1, "New Adjustment Layer here (affects everything below)");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, t] (int r)
        {
            if (r != 1) return;
            addAdjustmentLayer (t);
        });
        return;
    }
    int tr = trackAt (pos.y);
    bool trackLocked = tr >= 0 && tr < (int) s.tracks.size() && s.tracks[(size_t) tr].locked;

    if (tool == Tool::Select && c && c->transIn != TransitionType::Cut && c->transLen > 0 && ! c->locked && ! trackLocked
        && std::abs (pos.x - secToX (ticksToSec (c->start + c->transLen))) <= 5)
    {
        drag = Drag::TransLen; dragClip = c->id; dragOrig = c->transLen; st.selected = { c->id };
        if (onEdited) onEdited();
        return;
    }
    switch (tool)
    {
        case Tool::Select:
            if (c && st.project.songId.isNotEmpty() && c->mediaId == st.project.songId && edge == 0 && ! trackLocked)
            {
                // drag the song: synced footage, grid and drum hits follow it (Alt = move the song alone)
                st.selected = { c->id }; dragClip = c->id; dragOrig = c->start; dragTrack = c->track; drag = Drag::Song;
                if (onEdited) onEdited();
                break;
            }
            if (c)
            {
                if (e.mods.isShiftDown() || e.mods.isCommandDown()) { if (st.selected.contains (c->id)) st.selected.removeString (c->id); else st.selected.add (c->id); }
                else if (! st.selected.contains (c->id)) { st.selected = { c->id }; }
                if (c->group != 0)
                    for (auto& o : s.clips) if (o.group == c->group) st.selected.addIfNotAlreadyThere (o.id);
                dragClip = c->id; dragOrig = edge < 0 ? c->start : edge > 0 ? c->end() : c->start; dragTrack = c->track;
                drag = c->locked || trackLocked ? Drag::None : edge < 0 ? Drag::TrimL : edge > 0 ? Drag::TrimR : Drag::Move;
            }
            else
            {   // empty space: click seeks, drag draws a selection box (Shift/Cmd adds to the selection)
                boxBase = e.mods.isShiftDown() || e.mods.isCommandDown() ? st.selected : juce::StringArray();
                st.selected = boxBase; drag = Drag::Box; box = { pos.x, pos.y, 0, 0 };
                if (onSeek) onSeek (t);
            }
            if (onEdited) onEdited();
            break;
        case Tool::Paint:
            if (st.selected.size() > 1 && ! c && tr >= 0 && ! trackLocked)
            {   // stamp the whole selected pattern, its track layout kept relative to the clicked track
                auto pat = copyClips (s, st.selected);
                Ticks first = pat.front().start, last = 0; int top = pat.front().track;
                for (auto& k : pat) { first = std::min (first, k.start); last = std::max (last, k.end()); top = std::min (top, k.track); }
                const Ticks at = st.snapT (t);
                if (e.mouseWasDraggedSinceMouseDown() && paintNext >= 0 && at < paintNext) break;
                for (auto& k : pat) k.track += tr - top;
                pat.erase (std::remove_if (pat.begin(), pat.end(), [&] (const Clip& k) { return k.track < 0 || k.track >= (int) s.tracks.size()
                                               || s.tracks[(size_t) k.track].kind != s.tracks[(size_t) (k.track - (tr - top))].kind; }), pat.end());
                if (pat.empty()) break;
                st.edit ("Paint pattern");
                pasteClips (s, pat, at);
                paintNext = at + (last - first);
                if (onEdited) onEdited();
            }
            else if (! c && tr >= 0 && ! trackLocked)
            {
                juce::String tid = st.selected.isEmpty() ? juce::String() : st.selected[0];
                if (tid.isNotEmpty())
                {
                    st.edit ("Paint");
                    auto id = paintClip (s, tid, st.snapT (t), tr);
                    if (id.isEmpty()) st.history.undo (st.project);
                    if (onEdited) onEdited();
                }
            }
            else if (c && ! e.mouseWasDraggedSinceMouseDown()) { st.selected = { c->id }; if (onEdited) onEdited(); }
            break;
        case Tool::Delete:
            if (c && ! c->locked && ! trackLocked)
            {
                st.edit ("Delete");
                deleteClips (s, { c->id }, st.ripple);
                st.selected.removeString (c->id);
                if (onEdited) onEdited();
            }
            break;
        case Tool::Cut:
            if (c && ! c->locked && ! trackLocked)
            {
                st.edit ("Cut");
                splitAt (s, st.snapT (t), { c->id });
                if (onEdited) onEdited();
            }
            break;
    }
}

void Timeline::mouseDrag (const juce::MouseEvent& e)
{
    auto pos = e.getPosition();
    Ticks t = secToTicks (std::max (0.0, xToSec (pos.x)));
    if (pos.x > getWidth() - 20) { scrollSec += 4 / pxPerSec * 10; }
    switch (drag)
    {
        case Drag::Range:
        {
            Ticks a = st.snapT (dragStart), b = st.snapT (t);
            st.rangeA = std::min (a, b); st.rangeB = std::max (a, b);
            repaint(); break;
        }
        case Drag::Box:
        {
            box = juce::Rectangle<int>::leftTopRightBottom (std::min (e.getMouseDownX(), e.x), std::min (e.getMouseDownY(), e.y),
                                                            std::max (e.getMouseDownX(), e.x), std::max (e.getMouseDownY(), e.y));
            st.selected = boxBase;
            const auto& sq = st.project.seq();
            for (auto& c : sq.clips)
            {
                if (c.track < 0 || c.track >= (int) sq.tracks.size() || (st.project.songId.isNotEmpty() && c.mediaId == st.project.songId)) continue;
                const int y0 = trackY (c.track);
                juce::Rectangle<int> r (secToX (ticksToSec (c.start)), y0, std::max (1, secToX (ticksToSec (c.end())) - secToX (ticksToSec (c.start))), sq.tracks[(size_t) c.track].height);
                if (r.intersects (box)) st.selected.addIfNotAlreadyThere (c.id);
            }
            if (onEdited) onEdited();
            repaint();
            break;
        }
        case Drag::Scrub: if (onSeek) onSeek (e.mods.isAltDown() ? t : st.snapT (t)); break;
        case Drag::Song:
        {
            st.project = beforeDrag;
            Ticks delta = magnetSnap (dragOrig + (t - dragStart), 0) - dragOrig;
            shiftSong (st.project, delta, ! e.mods.isAltDown());
            if (onEdited) onEdited();
            break;
        }
        case Drag::Move:
        {
            st.project = beforeDrag;
            {
                int bottom = trackY ((int) st.project.seq().tracks.size());
                dragOut = pos.y > juce::jmin (getHeight(), bottom + 24) || pos.y < 0 || pos.x < 0 || pos.x > getWidth();
            }
            if (dragOut) { if (onEdited) onEdited(); repaint(); break; }
            const auto* dc = beforeDrag.seq().findClip (dragClip);
            Ticks delta = magnetSnap (dragOrig + (t - dragStart), dc != nullptr ? dc->length : 0) - dragOrig;
            if (dragOrig + delta < 0) delta = -dragOrig;
            int td = trackAt (pos.y) - dragTrack;
            auto& sq = st.project.seq();
            int target = trackAt (pos.y);
            if (! moveClips (sq, st.selected, delta, td))
            {
                bool videoClip = dragTrack >= 0 && dragTrack < (int) sq.tracks.size() && sq.tracks[(size_t) dragTrack].kind == TrackKind::Video;
                if (videoClip && target >= 0 && target < (int) sq.tracks.size() && sq.tracks[(size_t) target].kind == TrackKind::Audio)
                {
                    int nt = insertVideoTrack (sq);       // dragged past the video layers: new layer on top
                    if (! moveClips (sq, st.selected, delta, nt - dragTrack)) moveClips (sq, st.selected, delta, 0);
                }
                else moveClips (sq, st.selected, delta, 0);
            }
            if (onEdited) onEdited();
            break;
        }
        case Drag::TrimL: case Drag::TrimR:
        {
            st.project = beforeDrag;
            trimClip (st.project.seq(), st.project, dragClip, drag == Drag::TrimL, magnetSnap (dragOrig + (t - dragStart), 0));
            if (onEdited) onEdited();
            break;
        }
        case Drag::TransLen:
            if (auto* k = st.project.seq().findClip (dragClip))
            {
                const Ticks fr = secToTicks (1.0 / std::max (1.0, st.project.fps));
                k->transLen = juce::jlimit (fr, std::max (fr, k->length), dragOrig + (t - dragStart));
                if (onStatus) onStatus ("Transition " + juce::String (ticksToSec (k->transLen), 2) + " s");
                if (onEdited) onEdited();
            }
            break;
        case Drag::None: break;
    }
    if (tool == Tool::Delete || tool == Tool::Paint) mouseDown (e);   // drag-paint / drag-delete
}

void Timeline::mouseUp (const juce::MouseEvent& e)
{
    snapGuide = -1;
    bool pulled = false;
    if (drag == Drag::Move && dragOut)
    {
        st.project = beforeDrag;
        const bool ripple = e.mods.isAltDown() || st.ripple;
        const int filled = liftClips (st.project, st.project.seq(), st.selected, ripple, (juce::uint32) juce::Time::currentTimeMillis());
        st.selected.clear(); pulled = true;
        if (onStatus) onStatus (ripple ? juce::String ("Extracted (gap closed)") : filled > 0 ? "Pulled out, " + juce::String (filled) + " gap(s) filled from synced takes" : juce::String ("Pulled out (gap left)"));
        if (onEdited) onEdited();
    }
    else if (drag == Drag::Move)
    {
        // one shot dropped onto another: they trade slots (rearrange). Otherwise the clip stays exactly where it was dropped
        juce::String target;
        if (st.selected.size() == 1)
            if (auto* mv = st.project.seq().findClip (st.selected[0]))
            {
                const Ticks mid = mv->start + mv->length / 2;
                for (auto& o : beforeDrag.seq().clips)
                    if (o.id != mv->id && o.track == mv->track && o.mediaId.isNotEmpty() && ! o.locked && o.start <= mid && mid < o.end()) { target = o.id; break; }
            }
        if (target.isNotEmpty())
        {
            const auto id = st.selected[0];
            st.project = beforeDrag;
            swapClipSlots (st.project.seq(), st.project, id, target);
            if (onStatus) onStatus ("Swapped shots");
        }
        else settleClips (st.project.seq(), st.selected);
        if (onEdited) onEdited();
    }
    if (drag == Drag::Move || drag == Drag::TrimL || drag == Drag::TrimR || drag == Drag::Song)
    {
        if (toVar (beforeDrag).toString() != toVar (st.project).toString())
        {
            st.history.push (beforeDrag, pulled ? "Pull out" : drag == Drag::Move ? "Move" : drag == Drag::Song ? "Move song" : "Trim");
            st.dirty = true;
        }
    }
    if (drag == Drag::TransLen && toVar (beforeDrag).toString() != toVar (st.project).toString()) { st.history.push (beforeDrag, "Transition length"); st.dirty = true; }
    if (drag == Drag::Range && st.rangeB - st.rangeA < secToTicks (0.01)) st.rangeA = st.rangeB = 0;
    else if (drag == Drag::Range) selectRangeClips();
    if (drag == Drag::Box && onStatus && st.selected.size() > 1) onStatus (juce::String (st.selected.size()) + " clips selected. Right-click one: Re-cut, Slow Move, effects, delete.");
    box = {};
    drag = Drag::None; dragOut = false;
    repaint();
}

void Timeline::selectAll()
{
    st.selected.clear();
    const auto& sq = st.project.seq();
    for (auto& c : sq.clips)
        if (! c.locked && (st.project.songId.isEmpty() || c.mediaId != st.project.songId)) st.selected.add (c.id);
    if (onStatus) onStatus (juce::String (st.selected.size()) + " clips selected");
    if (onEdited) onEdited();
    repaint();
}

void Timeline::selectRangeClips()
{
    st.selected.clear();
    for (auto& c : st.project.seq().clips)
        if (c.start < st.rangeB && c.end() > st.rangeA && ! c.locked && (st.project.songId.isEmpty() || c.mediaId != st.project.songId)) st.selected.add (c.id);
    if (onStatus && ! st.selected.isEmpty())
        onStatus (juce::String (st.selected.size()) + " clips in the highlighted range. Right-click one: Re-cut, copy, delete, effects. Esc clears.");
    if (onEdited) onEdited();
}

void Timeline::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y >= kRuler || e.x < kHeader) return;
    const auto& g = st.project.song;
    const double beat = g.valid ? g.beatSec() : 0.5, bar = beat * (g.valid ? g.beatsPerBar : 4);
    const double off = g.valid ? ticksToSec (g.gridOffset) : 0.0;
    const double x = std::floor ((std::max (0.0, xToSec (e.x)) - off) / bar);
    st.rangeA = secToTicks (std::max (0.0, off + x * bar)); st.rangeB = secToTicks (off + (x + 1) * bar);
    selectRangeClips();
    repaint();
}

void Timeline::zoomBy (float f, int anchorX)
{
    double anchor = xToSec (anchorX);
    pxPerSec = juce::jlimit (1.5, 6000.0, pxPerSec * f);   // 6000 px/s = ~100 px per frame at 60 fps
    scrollSec = std::max (0.0, anchor - (anchorX - kHeader) / pxPerSec);
    repaint();
}

void Timeline::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isAltDown()) { zoomBy (std::pow (1.15f, w.deltaY * 6), e.x); return; }
    if (e.mods.isShiftDown() || e.x < kHeader)
    {
        int total = 0; for (auto& tk : st.project.seq().tracks) total += tk.height;
        vScroll = juce::jlimit (0, std::max (0, total - (getHeight() - kRuler) + 20), vScroll - (int) std::lround (w.deltaY * 240));
        repaint(); return;
    }
    // trackpad: two-finger up/down scrolls the tracks, left/right scrolls time (a plain mouse wheel still scrolls time)
    if (w.isSmooth && std::abs (w.deltaY) > std::abs (w.deltaX))
    {
        int total = 0; for (auto& tk : st.project.seq().tracks) total += tk.height;
        vScroll = juce::jlimit (0, std::max (0, total - (getHeight() - kRuler) + 20), vScroll - (int) std::lround (w.deltaY * 240));
        repaint(); return;
    }
    float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    scrollSec = std::max (0.0, scrollSec - d * 300 / pxPerSec);
    repaint();
}

void Timeline::mouseMagnify (const juce::MouseEvent& e, float s) { zoomBy (s, e.x); }

void Timeline::ensureVisible (Ticks t)
{
    double sec = ticksToSec (t), vis = (getWidth() - kHeader) / pxPerSec;
    if (sec < scrollSec || sec > scrollSec + vis * 0.92) scrollSec = std::max (0.0, sec - vis * 0.1);
}

void Timeline::itemDropped (const SourceDetails& d)
{
    auto desc = d.description.toString();
    const Ticks beat = st.project.song.valid ? secToTicks (st.project.song.beatSec()) : secToTicks (0.5);
    if (desc.startsWith ("transition:"))
    {
        auto ty = (TransitionType) desc.fromFirstOccurrenceOf (":", false, false).getIntValue();
        int tr = trackAt (d.localPosition.y);
        Ticks at = secToTicks (std::max (0.0, xToSec (d.localPosition.x)));
        Ticks reach = secToTicks (40.0 / pxPerSec) + beat;
        Sequence probe = st.project.seq();
        if (setTransitionAtCut (probe, tr, at, ty, beat / 2, reach))
        {
            st.edit ("Add transition");
            setTransitionAtCut (st.project.seq(), tr, at, ty, beat / 2, reach);
            if (onEdited) onEdited();
        }
        return;
    }
    if (desc.startsWith ("look:"))
    {
        auto name = desc.fromFirstOccurrenceOf (":", false, false);
        for (auto& l : lookLibrary())
            if (l.name == name)
            {
                FxStack fx; applyLook (fx, l, 1.0f);
                int edge = 0; const Clip* under = clipAt (d.localPosition, edge);
                Ticks at = under ? under->start : magnetSnap (secToTicks (std::max (0.0, xToSec (d.localPosition.x))), 0);
                Ticks len = under ? under->length : beat * st.project.song.beatsPerBar * 2;
                st.edit ("Add effect");
                auto id = addEffectClip (st.project.seq(), fx, l.name, at, len);
                if (id.isNotEmpty()) st.selected = { id };
                if (onEdited) onEdited();
                break;
            }
        return;
    }
    if (desc.startsWith ("preset:"))
    {
        UserPreset up; juce::String err;
        if (! readPresetFile (juce::File (desc.fromFirstOccurrenceOf ("preset:", false, false)), up, err)) { if (onStatus) onStatus (err); return; }
        int edge = 0; const Clip* under = clipAt (d.localPosition, edge);
        st.edit ("Apply preset");
        if (under != nullptr && under->mediaId.isNotEmpty())
        {
            if (auto* c = st.project.seq().findClip (under->id)) { applyUserPreset (*c, up); st.selected = { c->id }; }
        }
        else
        {
            Ticks at = under ? under->start : magnetSnap (secToTicks (std::max (0.0, xToSec (d.localPosition.x))), 0);
            Ticks len = under ? under->length : (up.length > 0 ? up.length : beat * st.project.song.beatsPerBar * 2);
            auto id = addEffectClip (st.project.seq(), up.fx, up.name.fromLastOccurrenceOf (" / ", false, false), at, len);
            if (auto* c = st.project.seq().findClip (id)) { auto fx = c->fx; applyUserPreset (*c, up); c->fx = fx; for (auto& [k, v] : up.fx.p) c->fx.set (k, v); st.selected = { id }; }
        }
        if (onEdited) onEdited();
        return;
    }
    if (desc.startsWith ("lut:"))
    {
        const juce::File f (desc.fromFirstOccurrenceOf ("lut:", false, false));
        FxStack fx; fx.lutPath = f.getFullPathName();
        int edge = 0; const Clip* under = clipAt (d.localPosition, edge);
        Ticks at = under ? under->start : magnetSnap (secToTicks (std::max (0.0, xToSec (d.localPosition.x))), 0);
        Ticks len = under ? under->length : beat * st.project.song.beatsPerBar * 2;
        st.edit ("Add LUT");
        auto id = addEffectClip (st.project.seq(), fx, "LUT " + f.getFileNameWithoutExtension(), at, len);
        if (id.isNotEmpty()) st.selected = { id };
        if (onEdited) onEdited();
        return;
    }
    if (desc.startsWith ("sfx:"))
    {
        const juce::File f (desc.fromFirstOccurrenceOf ("sfx:", false, false));
        Ticks at = magnetSnap (secToTicks (std::max (0.0, xToSec (d.localPosition.x))), 0);
        if (onSfxDropped) onSfxDropped (f, trackAt (d.localPosition.y), at);
        return;
    }
    if (! desc.startsWith ("media:")) return;
    int tr = trackAt (d.localPosition.y);
    st.edit ("Add clip");
    dragClip = {};
    Ticks at = magnetSnap (secToTicks (std::max (0.0, xToSec (d.localPosition.x))), 0);
    snapGuide = -1;
    // dropped in between existing clips: fill one bar (or up to the next edit) instead of wiping the rest of the track
    Ticks maxLen = 0;
    const auto& sq = st.project.seq();
    for (auto& c : sq.clips)
        if (c.track == tr && c.end() > at)
        {
            Ticks room = c.start > at ? c.start - at : 0;
            Ticks bar = st.project.song.valid ? secToTicks (st.project.song.beatSec() * st.project.song.beatsPerBar) : secToTicks (2.0);
            Ticks len = room > secToTicks (1.0 / st.project.fps) ? room : bar;
            maxLen = maxLen == 0 ? len : std::min (maxLen, len);
        }
    auto id = addMediaClip (st.project, desc.fromFirstOccurrenceOf ("media:", false, false), tr, at, maxLen);
    if (id.isNotEmpty()) st.selected = { id };
    if (onEdited) onEdited();
}

void Timeline::filesDropped (const juce::StringArray& paths, int x, int y)
{
    juce::Array<juce::File> files;
    for (auto& f : paths) files.add (juce::File (f));
    dragClip = {};
    Ticks at = magnetSnap (secToTicks (std::max (0.0, xToSec (x))), 0);
    snapGuide = -1;
    if (onFilesDropped) onFilesDropped (files, trackAt (y), at);
}

void Timeline::clipMenu (const juce::String& id)
{
    auto* c = st.project.seq().findClip (id);
    if (c == nullptr) return;
    if (! st.selected.contains (id)) st.selected = { id };   // right-click inside a selection keeps it
    if (onEdited) onEdited();
    bool video = st.project.seq().tracks[(size_t) c->track].kind == TrackKind::Video;
    juce::PopupMenu cut;
    auto names = autoCutNames();
    for (int i = 0; i < names.size(); ++i) cut.addItem (100 + i, names[i] + (st.selected.size() > 1 ? "  (" + juce::String (st.selected.size()) + " clips)" : juce::String()));
    cut.addSeparator();
    cut.addItem (99, "Switch camera angles on each cut", video, switchAngles && video);
    const bool lk = c->locked;
    const bool many = st.selected.size() > 1;
    juce::PopupMenu m, trans, scale, labelM;
    auto tn = transitionNames();
    for (int i = 1; i < tn.size(); ++i) trans.addItem (300 + i, tn[i], ! lk, (int) c->transIn == i);
    trans.addSeparator(); trans.addItem (300, "Remove transition", ! lk && c->transIn != TransitionType::Cut);
    int rf = (int) std::lround (c->fx.get ("reframe", 0.0f));
    scale.addItem (40, "Set to Frame Size (fill)", video && ! lk, rf == 0);
    scale.addItem (41, "Scale to Frame Size (fit)", video && ! lk, rf == 1);
    scale.addItem (42, "Fit with blurred background", video && ! lk, rf == 2);
    const char* ln[] { "None", "Violet", "Iris", "Caribbean", "Lavender", "Cerulean", "Forest", "Rose", "Mango" };
    for (int i = 0; i < 9; ++i) labelM.addItem (200 + i, ln[i], true, c->label == i);
    m.addItem (10, "Cut  (Ctrl+X)", ! lk);
    m.addItem (11, "Copy  (Ctrl+C)");
    m.addItem (12, "Paste at playhead  (Ctrl+V)", ! st.clipboard.empty());
    m.addItem (13, "Paste Attributes  (Ctrl+Alt+V)", st.clipboard.size() == 1 && ! lk);
    m.addItem (14, "Duplicate", ! lk);
    m.addSeparator();
    m.addItem (5, "Delete", ! lk);
    m.addItem (6, "Ripple Delete  (Shift+Del)", ! lk);
    m.addItem (47, "Pull out of edit (synced take fills the gap)", ! lk);
    m.addItem (2, "Split at playhead  (Ctrl+K)", ! lk && st.playhead > c->start && st.playhead < c->end());
    m.addSubMenu ("Re-cut (Auto Cut)", cut, ! lk);
    m.addSeparator();
    m.addItem (20, juce::String (c->enabled ? "Disable" : "Enable") + "  (Shift+E)", ! lk);
    m.addItem (21, "Group  (G)", many && ! lk);
    m.addItem (22, "Ungroup", c->group != 0);
    m.addSeparator();
    m.addItem (30, "Speed/Duration...  (Ctrl+R)", ! lk);
    m.addItem (3, "Reset speed to 100%", ! lk && c->speed != 1.0);
    {
        const bool hasMedia = c->mediaId.isNotEmpty();
        m.addItem (34, "Reverse clip", ! lk && hasMedia, c->reverse);
        juce::PopupMenu slow;
        const auto* md = st.project.findMedia (c->mediaId);
        const double srcFps = md != nullptr && md->fps > 0 ? md->fps : 0.0, tlFps = st.project.fps;
        if (srcFps > tlFps + 1)
            slow.addItem (35, "Clean slow-mo: every real frame (" + juce::String (juce::roundToInt (srcFps)) + " fps -> "
                              + juce::String (juce::roundToInt (tlFps)) + " fps = " + juce::String (juce::roundToInt (100 * tlFps / srcFps)) + "%)", ! lk);
        slow.addItem (39, "75%", ! lk); slow.addItem (36, "50%", ! lk); slow.addItem (38, "33%", ! lk); slow.addItem (37, "25%", ! lk);
        slow.addSeparator(); slow.addItem (30, "Custom speed...", ! lk);
        m.addSubMenu ("Slow motion", slow, ! lk && hasMedia);
        juce::PopupMenu ramp; auto rn = speedRampNames();
        for (int i = 0; i < rn.size(); ++i) ramp.addItem (470 + i, rn[i], ! lk);
        ramp.addSeparator(); ramp.addItem (469, "Remove speed ramp", ! lk && c->ramped());
        m.addSubMenu ("Speed ramp", ramp, ! lk && hasMedia && video);
        juce::PopupMenu fi;
        fi.addItem (480, "Off (nearest frame)", ! lk, c->frameInterp == 0);
        fi.addItem (481, "Frame Blending", ! lk, c->frameInterp == 1);
        fi.addItem (482, "Optical Flow (paused + export; blends while playing)", ! lk, c->frameInterp == 2);
        m.addSubMenu ("Frame interpolation", fi, ! lk && hasMedia && video);
        const int nSel = st.selected.contains (c->id) ? st.selected.size() : 1;
        m.addItem (43, nSel > 1 ? "Sync " + juce::String (nSel) + " clips to audio" : juce::String ("Sync clip to audio"),
                   hasMedia && md != nullptr && md->kind != MediaKind::Image);
    }
    {
        const int nSel = st.selected.contains (c->id) ? st.selected.size() : 1;
        m.addItem (44, nSel > 1 ? "Randomize " + juce::String (nSel) + " shots" : juce::String ("Randomize shot"), video && ! lk && ! c->keep);
        m.addItem (45, "Keep (pin, Randomize skips it)", video, c->keep);
        m.addItem (46, "Randomize all unpinned shots on this track", video && ! lk);
    }
    {
        juce::PopupMenu mv;
        mv.addItem (490, "Slow Push In (grow 8%, eased)", video && ! lk);
        mv.addItem (491, "Slow Pull Out (shrink 8%, eased)", video && ! lk);
        mv.addItem (492, "Slow Drift (gentle pan + 6% push)", video && ! lk);
        mv.addSeparator(); mv.addItem (493, "Remove slow move", video && ! lk);
        m.addSubMenu (many ? "Slow Move (" + juce::String (st.selected.size()) + " clips)" : juce::String ("Slow Move"), mv, video && ! lk);
    }
    m.addItem (31, "Add Frame Hold Segment (1 beat)", ! lk && video && st.playhead > c->start && st.playhead < c->end());
    m.addSubMenu ("Transition in", trans, ! lk);
    m.addSubMenu ("Scale / Reframe", scale, video && ! lk);
    juce::PopupMenu lay; auto lns = layoutNames();
    for (int i = 0; i < lns.size(); ++i) lay.addItem (400 + i, lns[i], video && ! lk);
    lay.addSeparator(); lay.addItem (420, "Stack selected clips (top to bottom)", video && ! lk && many);
    m.addSubMenu ("Layout / Split screen", lay, video && ! lk);
    juce::StringArray fxNames;
    {
        juce::PopupMenu fxM;
        for (auto& d : clipParams())
            if (auto it = c->fx.p.find (d.id); it != c->fx.p.end() && it->second != d.def)
            { fxNames.add (d.id); fxM.addItem (5000 + fxNames.size() - 1, juce::String ("Trash  ") + d.name + "  (" + juce::String (it->second, 2) + ")", ! lk); }
        for (auto& [k, ks] : c->keys)
            if (! ks.empty() && ! fxNames.contains (k)) { fxNames.add (k); fxM.addItem (5000 + fxNames.size() - 1, "Trash  " + k + " keyframes", ! lk); }
        m.addSubMenu ("Effects on clip (" + juce::String (fxNames.size()) + ")", fxM, fxNames.size() > 0);
    }
    m.addItem (32, "Remove Effects", ! lk && (! c->fx.p.empty() || ! c->keys.empty() || c->opacity != 1.0f || c->transIn != TransitionType::Cut));
    m.addSeparator();
    m.addItem (33, "Rename...");
    m.addSubMenu (st.selected.size() > 1 ? "Label Color (" + juce::String (st.selected.size()) + " clips)" : juce::String ("Label Color"), labelM);
    m.addItem (4, c->locked ? "Unlock clip  (L)" : "Lock clip  (L)");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, id, fxNames] (int r)
    {
        auto& sq = st.project.seq();
        auto* cl = sq.findClip (id);
        if (cl == nullptr || r == 0) return;
        if (r == 99) { switchAngles = ! switchAngles; juce::MessageManager::callAsync ([this, id] { clipMenu (id); }); return; }
        auto sel = st.selected.contains (id) ? st.selected : juce::StringArray { id };
        const Ticks beat = secToTicks (st.project.song.valid ? st.project.song.beatSec() : 0.5);
        if (r >= 100 && r < 200)
        {
            st.edit ("Auto Cut");
            int n = 0; auto seed = (juce::uint32) juce::Time::currentTimeMillis();
            if (st.rangeB > st.rangeA)   // highlighted pattern: re-cut only inside it
            {
                std::set<int> tracks; for (auto& i : sel) if (auto* k = sq.findClip (i)) tracks.insert (k->track);
                auto inside = [&] (bool strict) { juce::StringArray ids;
                    for (auto& k : sq.clips) if (tracks.count (k.track) && ! k.locked && k.mediaId != st.project.songId
                                                && (strict ? (k.start >= st.rangeA && k.end() <= st.rangeB) : (k.start < st.rangeB && k.end() > st.rangeA))) ids.add (k.id);
                    return ids; };
                if (auto ids = inside (false); ! ids.isEmpty()) splitAt (sq, st.rangeA, ids);
                if (auto ids = inside (false); ! ids.isEmpty()) splitAt (sq, st.rangeB, ids);
                sel = inside (true);
            }
            for (auto& i : sel) n += autoCutClip (st.project, i, (AutoCut) (r - 100), switchAngles, seed++);
            if (n == 0) st.history.undo (st.project);
            st.selected.clear();
        }
        if (r >= 490 && r <= 493)
        {
            st.edit ("Slow move");
            for (auto& i : sel)
                if (auto* k = sq.findClip (i); k && ! k->locked && k->mediaId.isNotEmpty())
                {
                    if (k->fx.get ("slowMove", 0.0f) > 0.0f)   // the slow move owns these keys, even after trims/splits
                        for (auto* id : { "scale", "posX" }) k->keys.erase (id);
                    k->fx.p.erase ("slowMove");
                    if (r == 493) continue;
                    k->keys.erase ("scale"); if (r == 492) k->keys.erase ("posX");   // only replace what this move animates
                    k->fx.set ("slowMove", 1.0f);
                    const float s0 = k->fx.get ("scale", 1.0f);
                    const float a0 = r == 491 ? s0 * 1.08f : s0, a1 = r == 490 ? s0 * 1.08f : r == 492 ? s0 * 1.06f : s0;
                    setKey (*k, "scale", 0, a0, 2); setKey (*k, "scale", k->length, a1, 2);
                    if (r == 492) { setKey (*k, "posX", 0, -0.02f, 2); setKey (*k, "posX", k->length, 0.02f, 2); }
                }
        }
        if (r >= 200 && r < 209) { st.edit ("Label"); for (auto& i : sel) if (auto* k = sq.findClip (i)) k->label = r - 200; }
        if (r >= 300 && r < 400)
        {
            st.edit ("Transition");
            for (auto& i : sel) if (auto* k = sq.findClip (i); k && ! k->locked)
            {
                k->transIn = (TransitionType) (r - 300);
                if (k->transLen == 0 && r > 300) k->transLen = beat / 2;
            }
        }
        if (r >= 400 && r < 412) { st.edit ("Layout"); for (auto& i : sel) if (auto* k = sq.findClip (i)) applyLayout (st.project, *k, r - 400); }
        if (r == 420)
        {
            st.edit ("Stack layout");
            std::vector<Clip*> v; for (auto& i : sel) if (auto* k = sq.findClip (i)) v.push_back (k);
            std::sort (v.begin(), v.end(), [] (const Clip* a, const Clip* b) { return a->track > b->track; });
            static const int two[] { 1, 2 }, three[] { 5, 6, 7 }, four[] { 8, 9, 10, 11 };
            const int* slots = v.size() == 2 ? two : v.size() == 3 ? three : four;
            for (size_t k = 0; k < v.size() && k < 4; ++k) applyLayout (st.project, *v[k], slots[k]);
        }
        if (r >= 40 && r <= 42) { st.edit ("Scale to frame"); for (auto& i : sel) if (auto* k = sq.findClip (i)) k->fx.set ("reframe", (float) (r - 40)); }
        if (r == 10) { st.clipboard = copyClips (sq, sel); st.edit ("Cut"); deleteClips (sq, sel, false); st.selected.clear(); }
        if (r == 11) st.clipboard = copyClips (sq, sel);
        if (r == 12) { st.edit ("Paste"); st.selected = pasteClips (sq, st.clipboard, st.playhead); }
        if (r == 13) { st.edit ("Paste Attributes"); pasteAttributes (sq, st.clipboard.front(), sel); }
        if (r == 14) { st.edit ("Duplicate"); st.selected = duplicateClips (sq, sel); }
        if (r == 2) { st.edit ("Split"); splitAt (sq, st.playhead, { id }); }
        if (r == 3) { st.edit ("Speed"); setClipSpeed (sq, st.project, id, 1.0); }
        if (r >= 469 && r < 480) { st.edit ("Speed ramp"); for (auto& i : sel) applySpeedRamp (sq, st.project, i, r - 470); }
        if (r >= 480 && r <= 482) { st.edit ("Frame interpolation"); for (auto& i : sel) if (auto* k = sq.findClip (i); k && ! k->locked) k->frameInterp = r - 480; }
        if (r == 44 || r == 46)
        {
            juce::StringArray ids = sel;
            if (r == 46) { ids.clear(); for (auto& k : sq.clips) if (k.track == cl->track) ids.add (k.id); }
            st.edit ("Randomize shots");
            if (randomizeShots (st.project, sq, ids, (juce::uint32) juce::Time::currentTimeMillis()) == 0) st.history.undo (st.project);
        }
        if (r == 45) { st.edit ("Keep"); const bool on = ! cl->keep; for (auto& i : sel) if (auto* k = sq.findClip (i)) k->keep = on; }
        if (r == 34) { st.edit ("Reverse"); bool on = ! cl->reverse; for (auto& i : sel) if (auto* k = sq.findClip (i); k && ! k->locked && k->mediaId.isNotEmpty()) k->reverse = on; }
        if (r >= 35 && r <= 39)
        {
            const auto* md = st.project.findMedia (cl->mediaId);
            const double srcFps = md != nullptr && md->fps > 0 ? md->fps : 30.0;
            const double sp = r == 35 ? st.project.fps / srcFps : r == 36 ? 0.5 : r == 37 ? 0.25 : r == 38 ? 1.0 / 3.0 : 0.75;
            st.edit ("Slow motion");
            for (auto& i : sel)
                if (auto* k = sq.findClip (i); k && ! k->locked && k->mediaId.isNotEmpty())
                {
                    if (auto* km = st.project.findMedia (k->mediaId); km != nullptr && km->fps > 0)
                        k->srcIn = quantizeToFrame (k->srcIn, km->fps);   // start on a real frame so every output frame is a real source frame
                    setClipSpeed (sq, st.project, i, sp);
                }
        }
        if (r == 43 && onSyncClips) { auto ids = sel; juce::MessageManager::callAsync ([this, ids] { onSyncClips (ids); }); return; }
        if (r == 4) { st.edit ("Lock"); cl->locked = ! cl->locked; }
        if (r == 5) { st.edit ("Delete"); deleteClips (sq, sel, false); st.selected.clear(); }
        if (r == 6) { st.edit ("Ripple Delete"); deleteClips (sq, sel, true); st.selected.clear(); }
        if (r == 47) { st.edit ("Pull out"); liftClips (st.project, sq, sel, false, (juce::uint32) juce::Time::currentTimeMillis()); st.selected.clear(); }
        if (r == 20) { st.edit ("Enable"); bool on = ! cl->enabled; for (auto& i : sel) if (auto* k = sq.findClip (i)) k->enabled = on; }
        if (r == 21 || r == 22)
        {
            st.edit (r == 21 ? "Group" : "Ungroup");
            int ng = 1; for (auto& k : sq.clips) ng = std::max (ng, k.group + 1);
            int g0 = cl->group;
            for (auto& k : sq.clips) if (r == 21 ? sel.contains (k.id) : (k.group == g0)) k.group = r == 21 ? ng : 0;
        }
        if (r == 30 && onAskText)
            onAskText ("Speed / Duration (%)", juce::String (cl->speed * 100, 0), [this, id] (juce::String v)
            {
                double pc = v.retainCharacters ("0123456789.").getDoubleValue();
                if (pc <= 0) return;
                st.edit ("Speed/Duration"); setClipSpeed (st.project.seq(), st.project, id, pc / 100.0);
                if (onEdited) onEdited();
            });
        if (r == 31) { st.edit ("Frame Hold"); auto h = freezeFrame (sq, id, st.playhead, beat); if (h.isNotEmpty()) st.selected = { h }; }
        if (r >= 5000 && r < 5000 + fxNames.size())
        {
            const auto fid = fxNames[r - 5000];
            st.edit ("Trash effect");
            for (auto& i : sel) if (auto* k = sq.findClip (i)) { k->fx.p.erase (fid); k->keys.erase (fid); }
        }
        if (r == 32) { st.edit ("Remove Effects"); for (auto& i : sel) if (auto* k = sq.findClip (i)) { k->fx = {}; k->keys.clear(); k->opacity = 1; k->transIn = TransitionType::Cut; k->transLen = 0; } }
        if (r == 33 && onAskText)
            onAskText ("Rename clip", cl->name, [this, id] (juce::String v)
            {
                if (auto* k = st.project.seq().findClip (id)) { st.edit ("Rename"); k->name = v; }
                if (onEdited) onEdited();
            });
        if (onEdited) onEdited();
    });
}
void Timeline::clipMenuSpeed (const juce::String& id)
{
    auto* cl = st.project.seq().findClip (id);
    if (cl == nullptr || cl->locked || ! onAskText) return;
    onAskText ("Speed / Duration (%)", juce::String (cl->speed * 100, 0), [this, id] (juce::String v)
    {
        double pc = v.retainCharacters ("0123456789.").getDoubleValue();
        if (pc <= 0) return;
        st.edit ("Speed/Duration"); setClipSpeed (st.project.seq(), st.project, id, pc / 100.0);
        if (onEdited) onEdited();
    });
}
}
