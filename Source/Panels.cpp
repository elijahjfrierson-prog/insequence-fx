#include "App.h"

namespace isfx
{
// ============================================================ Preview
Preview::Preview (AppState& s) : juce::Thread ("ISFX preview"), st (s)
{
    snap = std::make_shared<Project> (st.project);
    startThread();
}

Preview::~Preview() { signalThreadShouldExit(); wake.signal(); stopThread (4000); }

void Preview::setProject (const Project& p)
{
    auto n = std::make_shared<const Project> (p);
    const juce::ScopedLock sl (lock); snap = n;
}

juce::Rectangle<int> Preview::frameArea() const
{
    auto r = getLocalBounds().reduced (8).withTrimmedBottom (18);
    int W, H; outputSize (st.project, 1080, W, H);
    float a = (float) W / H;
    int w = r.getWidth(), h = (int) (w / a);
    if (h > r.getHeight()) { h = r.getHeight(); w = (int) (h * a); }
    return juce::Rectangle<int> (w, h).withCentre (r.getCentre());
}

void Preview::requestFrame (Ticks t)
{
    auto fa = frameArea();
    // preview renders at most ~540p (export renders at full size through the same renderer)
    double scale = 1.0;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) scale = d->scale;
    int phys = (int) std::round (std::min (fa.getWidth(), fa.getHeight()) * scale);
    // playback renders a light preview from proxy-sized decodes; a paused frame renders sharp at screen resolution
    int sh = live ? juce::jlimit (90, 540, phys) : juce::jlimit (90, 1080, phys);
    int W, H; outputSize (st.project, sh, W, H);
    const juce::ScopedLock sl (lock);
    want = t; lastReq = t; wantW = W; wantH = H; wantDecodeH = live ? std::min (H, 540) : std::min (H, 1080);
    wake.signal();
}

void Preview::run()
{
    std::vector<juce::uint8> px;
    while (! threadShouldExit())
    {
        wake.wait (100);
        Ticks t; int W, H, decH; std::shared_ptr<const Project> p;
        {
            const juce::ScopedLock sl (lock);
            if (want < 0) continue;
            t = want; want = -1; W = wantW; H = wantH; p = snap; decH = wantDecodeH;
        }
        if (W <= 0 || H <= 0 || p == nullptr) continue;
        if (clearReq.exchange (false)) renderer.clearSources();
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        renderer.render (*p, p->seq(), t, W, H, px, decH);
        lastMs = juce::Time::getMillisecondCounterHiRes() - t0;
        juce::Image im (juce::Image::ARGB, W, H, false);
        {
            juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < H; ++y)
            {
                auto* src = px.data() + (size_t) y * W * 4;
                auto* dst = reinterpret_cast<juce::PixelARGB*> (bd.getLinePointer (y));
                for (int x = 0; x < W; ++x, src += 4)
                    dst[x].setARGB (255, src[0], src[1], src[2]);
            }
        }
        { const juce::ScopedLock sl (lock); shown = im; }
        triggerAsyncUpdate();
    }
}

void Preview::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff07080a));
    auto fa = frameArea();
    juce::Image im; { const juce::ScopedLock sl (lock); im = shown; }
    g.setColour (juce::Colours::black); g.fillRect (fa);
    if (im.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (im, fa.toFloat(), juce::RectanglePlacement::stretchToFit);
    }
    g.setColour (theme::line); g.drawRect (fa.expanded (1));
    g.setColour (theme::dim); g.setFont (11.0f);
    double sec = ticksToSec (st.playhead);
    auto fr = ticksToFrame (st.playhead, st.project.fps) % std::max (1, (int) std::lround (st.project.fps));
    juce::String tc = juce::String ((int) sec / 60).paddedLeft ('0', 2) + ":" + juce::String ((int) sec % 60).paddedLeft ('0', 2) + ":" + juce::String ((int) fr).paddedLeft ('0', 2);
    juce::String info = tc;
    if (st.project.song.valid) info << "   bar " << (st.project.song.barAt (st.playhead) + 1) << "   " << juce::String (st.project.song.bpm, 1) << " BPM";
    info << "   " << st.project.aspectW << ":" << st.project.aspectH << "   " << st.project.seq().name;
    g.drawText (info, getLocalBounds().removeFromBottom (20).reduced (10, 0), juce::Justification::centredLeft);
    g.drawText (juce::String (lastMs, 0) + " ms", getLocalBounds().removeFromBottom (20).reduced (10, 0), juce::Justification::centredRight);
    if (st.project.seq().clips.empty())
    {
        g.setColour (theme::dim); g.setFont (15.0f);
        g.drawText ("Import a song and your video takes, then press AUTO SYNC and GENERATE 3 EDITS", fa.reduced (20), juce::Justification::centred, true);
    }
}

// ============================================================ Media panel
MediaPanel::MediaPanel (AppState& s) : st (s)
{
    list.setModel (this); list.setRowHeight (54);
    list.setColour (juce::ListBox::backgroundColourId, theme::panel);
    list.setMultipleSelectionEnabled (true);
    addAndMakeVisible (list); addAndMakeVisible (importBtn); addAndMakeVisible (syncBtn);
    importBtn.setTooltip ("Import songs (MP3/WAV/FLAC/AIFF), video takes (MP4/MOV/ProRes/H.264/H.265), stills and .cube LUTs. Originals are never modified.");
    syncBtn.setTooltip ("Matches each take's own recorded audio to the song and places it in sync. Takes it can't place confidently are flagged orange, never guessed.");
    importBtn.onClick = [this] { if (onImport) onImport(); };
    syncBtn.onClick = [this] { if (onSyncAll) onSyncAll(); };
}

void MediaPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto top = r.removeFromTop (30).withTrimmedTop (2);
    importBtn.setBounds (top.removeFromLeft (top.getWidth() / 2).reduced (2, 0));
    syncBtn.setBounds (top.reduced (2, 0));
    r.removeFromTop (6);
    list.setBounds (r);
}

void MediaPanel::paint (juce::Graphics& g) { g.fillAll (theme::panel); }

int MediaPanel::getNumRows() { return (int) st.project.media.size(); }

void MediaPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    if (row < 0 || row >= getNumRows()) return;
    auto& m = st.project.media[(size_t) row];
    g.fillAll (sel ? theme::accent.withAlpha (0.25f) : row % 2 ? theme::panel : theme::panel2.darker (0.2f));
    juce::Rectangle<int> th (4, 4, 82, h - 8);
    auto it = st.thumbs.find (m.id);
    if (it != st.thumbs.end() && it->second.isValid()) g.drawImage (it->second, th.toFloat(), juce::RectanglePlacement::centred);
    else
    {
        g.setColour (theme::line); g.fillRect (th);
        g.setColour (theme::dim); g.setFont (10.0f);
        g.drawText (m.kind == MediaKind::Audio ? "AUDIO" : m.kind == MediaKind::Image ? "IMAGE" : "VIDEO", th, juce::Justification::centred);
    }
    g.setColour (m.missing ? theme::bad : theme::text); g.setFont (12.0f);
    juce::String nm = m.name; if (m.id == st.project.songId) nm = juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xab ")) + nm;
    g.drawText (nm, 92, 4, w - 96, 16, juce::Justification::left, true);
    g.setColour (theme::dim); g.setFont (10.5f);
    juce::String sub = juce::String (ticksToSec (m.duration), 1) + " s";
    if (m.kind == MediaKind::Video) sub << "  " << m.width << "x" << m.height << "  " << juce::String (m.fps, 2) << " fps";
    if (m.missing) sub = "MISSING - right-click to locate";
    g.drawText (sub, 92, 20, w - 96, 14, juce::Justification::left, true);
    if (m.kind == MediaKind::Video && m.id != st.project.songId)
    {
        juce::String sy; juce::Colour c = theme::dim;
        switch (m.sync.status)
        {
            case SyncInfo::Synced:    sy = "synced " + juce::String (ticksToSec (m.sync.offset), 3) + " s  " + juce::String (juce::roundToInt (m.sync.confidence * 100)) + "%"; c = theme::good; break;
            case SyncInfo::Manual:    sy = "placed manually"; c = theme::good; break;
            case SyncInfo::Uncertain: sy = "uncertain - check placement"; c = theme::warn; break;
            case SyncInfo::Failed:    sy = "not synced: " + m.sync.note; c = theme::bad; break;
            case SyncInfo::None:      sy = m.hasAudio ? "not synced yet" : "no audio (place manually)"; break;
        }
        g.setColour (c); g.drawText (sy, 92, 34, w - 96, 14, juce::Justification::left, true);
    }
}

juce::var MediaPanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty() || rows[0] >= getNumRows()) return {};
    return "media:" + st.project.media[(size_t) rows[0]].id;
}

void MediaPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < getNumRows() && onAddToTimeline) onAddToTimeline (st.project.media[(size_t) row].id);
}

juce::String MediaPanel::getTooltipForRow (int row)
{
    if (row < 0 || row >= getNumRows()) return {};
    auto& m = st.project.media[(size_t) row];
    return m.path + "\nDrag to the timeline or double-click to add at the playhead. Right-click for more.";
}

void MediaPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || row < 0 || row >= getNumRows()) return;
    auto id = st.project.media[(size_t) row].id;
    auto& m = st.project.media[(size_t) row];
    juce::PopupMenu menu;
    menu.addItem (1, "Add to timeline at playhead");
    menu.addItem (2, "Use as the song", m.kind != MediaKind::Image);
    menu.addItem (3, "Re-sync this take", m.kind == MediaKind::Video && m.hasAudio && st.project.song.valid);
    menu.addItem (4, m.missing ? "Locate missing file..." : "Relink to another file...");
    menu.addSeparator();
    menu.addItem (5, "Remove from project");
    menu.showMenuAsync (juce::PopupMenu::Options(), [this, id] (int r)
    {
        if (r == 1 && onAddToTimeline) onAddToTimeline (id);
        if (r == 2 && onSetSong) onSetSong (id);
        if (r == 3 && onResync) onResync (id);
        if (r == 4 && onRelink) onRelink (id);
        if (r == 5 && onRemove) onRemove (id);
    });
}

// ============================================================ Inspector
namespace
{
struct ParamRow : public juce::Component
{
    juce::Label name; juce::Slider slider;
    ParamRow (const juce::String& n, const juce::String& help, double lo, double hi, double v, double step)
    {
        name.setText (n, juce::dontSendNotification); name.setFont (12.0f); name.setTooltip (help);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        slider.setRange (lo, hi, step); slider.setValue (v, juce::dontSendNotification);
        slider.setTooltip (help); slider.setDoubleClickReturnValue (true, v);
        addAndMakeVisible (name); addAndMakeVisible (slider);
    }
    void resized() override { auto r = getLocalBounds(); name.setBounds (r.removeFromLeft (110)); slider.setBounds (r); }
};
struct ComboRow : public juce::Component
{
    juce::Label l; juce::ComboBox cb;
    ComboRow (const juce::String& label, const juce::StringArray& opts, int sel, const juce::String& help)
    {
        l.setText (label, juce::dontSendNotification); l.setFont (12.0f); l.setTooltip (help);
        cb.addItemList (opts, 1); cb.setSelectedItemIndex (sel, juce::dontSendNotification); cb.setTooltip (help);
        addAndMakeVisible (l); addAndMakeVisible (cb);
    }
    void resized() override { auto r = getLocalBounds(); l.setBounds (r.removeFromLeft (110)); cb.setBounds (r.reduced (0, 2)); }
};
struct Heading : public juce::Component
{
    juce::String t; explicit Heading (juce::String s) : t (std::move (s)) {}
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::accent2); g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText (t.toUpperCase(), getLocalBounds().withTrimmedTop (6), juce::Justification::left);
        g.setColour (theme::line); g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    }
};
}

Inspector::Inspector (AppState& s) : st (s)
{
    title.setFont (juce::Font (14.0f, juce::Font::bold));
    addAndMakeVisible (title);
    view.setViewedComponent (&content, false);
    view.setScrollBarsShown (true, false);
    addAndMakeVisible (view);
    rebuild();
}

void Inspector::paint (juce::Graphics& g) { g.fillAll (theme::panel); }

// CapCut-style tone curves: RGB master plus R, G, B; five draggable points per channel.
struct CurveEditor : public juce::Component
{
    CurveEditor (std::function<FxStack&()> s, std::function<void()> b, std::function<void()> e)
        : stack (std::move (s)), begin (std::move (b)), edited (std::move (e)) {}
    static juce::String pid (int c, int i) { return juce::String ("crv") + juce::String::charToString ("MRGB"[c]) + juce::String (i); }
    float val (int c, int i) const { return stack().get (pid (c, i), (float) i / 4.0f); }
    juce::Rectangle<float> plot() const { auto r = getLocalBounds().toFloat().reduced (4); r.removeFromTop (26); return r; }
    float eval (int c, float x) const
    {
        float y[5]; for (int i = 0; i < 5; ++i) y[i] = val (c, i);
        float f = juce::jlimit (0.0f, 1.0f, x) * 4; int i = std::min (3, (int) f); float u = f - (float) i;
        float p1 = y[i], p2 = y[i + 1], p0 = i > 0 ? y[i - 1] : 2 * p1 - p2, p3 = i < 3 ? y[i + 2] : 2 * p2 - p1;
        return juce::jlimit (0.0f, 1.0f, 0.5f * (2 * p1 + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u + (-p0 + 3 * p1 - 3 * p2 + p3) * u * u * u));
    }
    static juce::Colour colourFor (int c)
    {
        return c == 0 ? juce::Colours::white : c == 1 ? juce::Colour (0xffff5a5a) : c == 2 ? juce::Colour (0xff4ade80) : juce::Colour (0xff5aa9ff);
    }
    void paint (juce::Graphics& g) override
    {
        const char* names[4] = { "RGB", "R", "G", "B" };
        for (int c = 0; c < 4; ++c)
        {
            juce::Rectangle<float> tab (4.0f + (float) c * 46.0f, 4.0f, 42.0f, 20.0f);
            g.setColour (c == ch ? colourFor (c).withAlpha (0.25f) : theme::panel2); g.fillRoundedRectangle (tab, 4);
            g.setColour (c == ch ? colourFor (c) : theme::dim); g.setFont (11.0f);
            g.drawText (names[c], tab, juce::Justification::centred);
        }
        g.setColour (theme::dim); g.setFont (10.0f);
        g.drawText ("double-click resets", getLocalBounds().removeFromTop (28).reduced (6, 0), juce::Justification::centredRight);
        auto p = plot();
        g.setColour (juce::Colour (0xff0b0c10)); g.fillRoundedRectangle (p, 4);
        g.setColour (theme::line);
        for (int i = 1; i < 4; ++i)
        {
            g.drawVerticalLine ((int) (p.getX() + p.getWidth() * (float) i / 4), p.getY(), p.getBottom());
            g.drawHorizontalLine ((int) (p.getY() + p.getHeight() * (float) i / 4), p.getX(), p.getRight());
        }
        g.drawLine (p.getX(), p.getBottom(), p.getRight(), p.getY(), 0.6f);
        for (int pass = 0; pass < 2; ++pass)
            for (int c = 0; c < 4; ++c)
            {
                if ((pass == 1) != (c == ch)) continue;
                juce::Path path;
                for (int k = 0; k <= 64; ++k)
                {
                    float x = (float) k / 64, y = eval (c, x);
                    juce::Point<float> pt (p.getX() + x * p.getWidth(), p.getBottom() - y * p.getHeight());
                    if (k == 0) path.startNewSubPath (pt); else path.lineTo (pt);
                }
                g.setColour (colourFor (c).withAlpha (c == ch ? 1.0f : 0.22f));
                g.strokePath (path, juce::PathStrokeType (c == ch ? 1.8f : 1.0f));
            }
        for (int i = 0; i < 5; ++i)
        {
            juce::Point<float> pt (p.getX() + p.getWidth() * (float) i / 4, p.getBottom() - val (ch, i) * p.getHeight());
            g.setColour (juce::Colour (0xff0b0c10)); g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (pt));
            g.setColour (colourFor (ch)); g.drawEllipse (juce::Rectangle<float> (10, 10).withCentre (pt), 1.6f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y < 28) { ch = juce::jlimit (0, 3, (e.x - 4) / 46); dragIdx = -1; repaint(); return; }
        auto p = plot();
        dragIdx = juce::jlimit (0, 4, (int) std::lround ((e.position.x - p.getX()) / p.getWidth() * 4));
        if (begin) begin();
        setFromMouse (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragIdx >= 0) setFromMouse (e); }
    void mouseUp (const juce::MouseEvent&) override { dragIdx = -1; }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y < 28) return;
        if (begin) begin();
        for (int i = 0; i < 5; ++i) stack().set (pid (ch, i), (float) i / 4.0f);
        if (edited) edited();
        repaint();
    }
    void setFromMouse (const juce::MouseEvent& e)
    {
        auto p = plot();
        stack().set (pid (ch, dragIdx), juce::jlimit (0.0f, 1.0f, 1 - (e.position.y - p.getY()) / p.getHeight()));
        if (edited) edited();
        repaint();
    }
    std::function<FxStack&()> stack;
    std::function<void()> begin, edited;
    int ch = 0, dragIdx = -1;
};

void Inspector::resized()
{
    auto r = getLocalBounds().reduced (8);
    title.setBounds (r.removeFromTop (24));
    view.setBounds (r);
    int w = view.getWidth() - view.getScrollBarThickness() - 2, y = 0;
    for (auto* c : items) { int h = dynamic_cast<Heading*> (c) ? 24 : dynamic_cast<CurveEditor*> (c) ? 236 : 26; c->setBounds (0, y, w, h); y += h; }
    content.setSize (w, y + 10);
}

void Inspector::rebuild()
{
    items.clear(); content.removeAllChildren();
    auto& seq = st.project.seq();
    Clip* clip = st.selected.size() == 1 ? seq.findClip (st.selected[0]) : nullptr;
    target = clip ? clip->id : juce::String();
    auto add = [this] (juce::Component* c) { items.add (c); content.addAndMakeVisible (c); };
    auto edited = [this] { st.dirty = true; if (onEdited) onEdited(); };

    auto addCombo = [&] (const juce::String& label, const juce::StringArray& opts, int sel, const juce::String& help, std::function<void (int)> fn)
    {
        auto* row = new ComboRow (label, opts, sel, help);
        row->cb.onChange = [row, fn] { fn (row->cb.getSelectedItemIndex()); };
        add (row);
    };

    auto addParams = [&] (const std::vector<ParamDef>& defs, std::function<FxStack&()> stack)
    {
        juce::String group;
        bool curves = false;
        for (auto& d : defs)
        {
            if (juce::String (d.group) == "Curves")
            {
                if (! curves) { curves = true; group = d.group; add (new Heading ("Curves")); add (new CurveEditor (stack, [this] { st.edit ("Curve"); }, edited)); }
                continue;
            }
            if (juce::String (d.group) != group) { group = d.group; add (new Heading (group)); }
            float v = stack().get (d.id, d.def);
            bool discrete = juce::String (d.id) == "reframe" || juce::String (d.id) == "blend" || juce::String (d.id) == "maskShape" || juce::String (d.id) == "maskInvert";
            auto* r = new ParamRow (d.name, d.help, d.min, d.max, v, discrete ? 1.0 : (d.max - d.min) / 1000.0);
            juce::String id (d.id);
            if (id == "blend") r->slider.textFromValueFunction = [] (double x) { return blendModeNames()[(int) x]; };
            if (id == "reframe") r->slider.textFromValueFunction = [] (double x) { return juce::StringArray { "Fill", "Fit", "Blur" }[(int) x]; };
            if (id == "maskShape") r->slider.textFromValueFunction = [] (double x) { return juce::StringArray { "None", "Ellipse", "Rect" }[(int) x]; };
            r->slider.updateText();
            r->slider.onDragStart = [this, n = juce::String (d.name)] { st.edit (n); };
            r->slider.onValueChange = [this, r, id, stack, edited]
            {
                if (! r->slider.isMouseButtonDown()) st.edit (id);
                stack().set (id, (float) r->slider.getValue()); edited();
            };
            add (r);
        }
    };

    if (clip)
    {
        auto* m = st.project.findMedia (clip->mediaId);
        title.setText ("Clip: " + (m ? m->name : juce::String ("?")), juce::dontSendNotification);
        const juce::String cid = clip->id;
        auto clipRef = [this, cid]() -> Clip& { static Clip dummy; auto* c = st.project.seq().findClip (cid); return c ? *c : dummy; };
        add (new Heading ("Clip"));
        auto mk = [&] (const juce::String& n, const juce::String& help, double lo, double hi, double v, std::function<void (Clip&, double)> set)
        {
            auto* r = new ParamRow (n, help, lo, hi, v, (hi - lo) / 1000.0);
            r->slider.onDragStart = [this, n] { st.edit (n); };
            r->slider.onValueChange = [this, r, set, clipRef, edited, n] { if (! r->slider.isMouseButtonDown()) st.edit (n); set (clipRef(), r->slider.getValue()); edited(); };
            add (r);
        };
        mk ("Opacity", "Clip opacity for compositing over lower tracks.", 0, 1, clip->opacity, [] (Clip& c, double v) { c.opacity = (float) v; });
        mk ("Volume", "Clip audio level (video takes start muted; the song carries the mix).", 0, 2, clip->volume, [] (Clip& c, double v) { c.volume = (float) v; });
        mk ("Speed", "Playback speed. Values below 1 slow motion, above 1 speed up. Negative values are not used; use Reverse.", 0.1, 4, clip->speed, [] (Clip& c, double v) { c.speed = v; });
        addCombo ("Transition in", transitionNames(), (int) clip->transIn, "Transition from the previous clip on this track.",
                  [this, clipRef, edited] (int i) { st.edit ("Transition"); auto& c = clipRef(); c.transIn = (TransitionType) i;
                      if (c.transLen == 0 && i > 0) c.transLen = secToTicks (st.project.song.valid ? st.project.song.beatSec() / 2 : 0.25); edited(); });
        mk ("Transition beats", "Transition length in beats.", 0, 4, st.project.song.valid ? ticksToSec (clip->transLen) / st.project.song.beatSec() : ticksToSec (clip->transLen) * 2,
            [this] (Clip& c, double v) { c.transLen = secToTicks (v * (st.project.song.valid ? st.project.song.beatSec() : 0.5)); });
        mk ("Transition amount", "How hard the transition hits: distance, spin, blur, split, burn glow.", 0, 2, clip->fx.get ("trAmt", 1.0f),
            [] (Clip& c, double v) { c.fx.set ("trAmt", (float) v); });
        addCombo ("Transition direction", { "Left / In", "Right / Out", "Up", "Down" }, juce::jlimit (0, 3, (int) clip->fx.get ("trDir", 0.0f)),
                  "Direction for whip, spin, wipe, mirror and light leak (Zoom: In or Out; Luma Wipe: bright-first or dark-first).",
                  [this, clipRef, edited] (int i) { st.edit ("Transition direction"); clipRef().fx.set ("trDir", (float) i); edited(); });
        juce::StringArray cps { "(choose a preset)" }; std::vector<UserPreset> cpv;
        for (auto& u : loadUserPresets()) if (u.clip) { cpv.push_back (u); cps.add (u.name); }
        addCombo ("Clip preset", cps, 0, "Your saved clip effects (Presets > Save clip effects).",
                  [this, clipRef, edited, cpv] (int i) { if (i <= 0) return; st.edit ("Clip preset"); clipRef().fx = cpv[(size_t) i - 1].fx; edited();
                      juce::MessageManager::callAsync ([this] { rebuild(); resized(); }); });
        addParams (clipParams(), [clipRef]() -> FxStack& { return clipRef().fx; });
    }
    else
    {
        title.setText ("Sequence look: " + seq.name, juce::dontSendNotification);
        add (new Heading ("Look library"));
        juce::StringArray looks { "(choose a look)" };
        for (auto& l : lookLibrary()) looks.add (l.category + " / " + l.name);
        std::vector<UserPreset> ups; for (auto& u : loadUserPresets()) if (! u.clip) { ups.push_back (u); looks.add ("My Presets / " + u.name); }
        addCombo ("Look", looks, 0, "Applies a complete grade + FX stack to the whole sequence. Every slider below stays editable.",
                  [this, edited, ups] (int i) { if (i <= 0) return; st.edit ("Look"); auto lut = st.project.seq().fx.lutPath;
                      const int nl = (int) lookLibrary().size();
                      if (i - 1 < nl) { st.project.seq().fx.p.clear(); st.project.seq().fx.lutPath = lut; applyLook (st.project.seq().fx, lookLibrary()[(size_t) i - 1], 1.0f); }
                      else st.project.seq().fx = ups[(size_t) (i - 1 - nl)].fx;
                      edited();
                      juce::MessageManager::callAsync ([this] { rebuild(); resized(); }); });
        juce::StringArray modes { "(none)" }; modes.addArray (styleModes());
        addCombo ("Style mode", modes, 0, "Classic, Gothic, VHS, Y2K, Underground, Abstract: a base treatment layered under the look.",
                  [this, edited] (int i) { if (i <= 0) return; st.edit ("Style mode"); applyStyleMode (st.project.seq().fx, styleModes()[i - 1]); edited();
                      juce::MessageManager::callAsync ([this] { rebuild(); resized(); }); });
        addCombo ("LUT", { seq.fx.lutPath.isEmpty() ? "(none)" : juce::File (seq.fx.lutPath).getFileName(), "Load .cube...", "Clear" }, 0,
                  "3D LUT (.cube) applied in the grade; LUT Mix sets the intensity.",
                  [this, edited] (int i)
                  {
                      if (i == 2) { st.edit ("Clear LUT"); st.project.seq().fx.lutPath = {}; edited(); juce::MessageManager::callAsync ([this] { rebuild(); resized(); }); }
                      if (i != 1) return;
                      auto fc = std::make_shared<juce::FileChooser> ("Choose a .cube LUT", juce::File(), "*.cube");
                      fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, fc, edited] (const juce::FileChooser& f)
                      {
                          auto file = f.getResult();
                          if (file.existsAsFile())
                          {
                              Lut3D l; juce::String err;
                              if (! l.load (file, err)) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "LUT", err); return; }
                              st.edit ("Load LUT"); st.project.seq().fx.lutPath = file.getFullPathName();
                              if (st.project.seq().fx.get ("lutMix", 0) <= 0) st.project.seq().fx.set ("lutMix", 1.0f);
                              edited();
                          }
                          rebuild(); resized();
                      });
                  });
        addParams (sequenceParams(), [this]() -> FxStack& { return st.project.seq().fx; });
    }
    resized();
}
}
