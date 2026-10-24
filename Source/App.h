#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Model.h"
#include "Media.h"
#include "Analysis.h"
#include "Editing.h"
#include "Looks.h"
#include "AudioMix.h"
#include "Exporter.h"
#include "Renderer.h"
#include "Plugins.h"
#include "Presets.h"

namespace isfx
{
// ---- theme
namespace theme
{
    const juce::Colour bg { 0xff0e0f13 }, panel { 0xff16181e }, panel2 { 0xff1d2028 }, line { 0xff2a2e38 },
                       text { 0xffe8e9ee }, dim { 0xff8a8f9c }, accent { 0xffff3d6e }, accent2 { 0xff38d6ff },
                       good { 0xff3ddc84 }, warn { 0xffffb020 }, bad { 0xffff4d4d };
}
void drawLogo (juce::Graphics&, juce::Rectangle<float> area);  // sleek InSequence FX wordmark

class ISFXLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ISFXLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
};

// ---- shared app state
struct AppState
{
    Project project = Project::makeNew();
    History history;
    AudioCache audio;
    std::map<juce::String, Waveform> waves;
    std::map<juce::String, juce::Image> thumbs;
    juce::File file;
    bool dirty = false;
    // selection / transport
    juce::StringArray selected;
    Ticks playhead = 0, rangeA = 0, rangeB = 0;
    SnapMode snap = SnapMode::Sixteenth;
    bool snapOn = true, ripple = false, showGrid = true, magnetic = true;
    juce::ChangeBroadcaster changed;   // project edited -> repaint everything

    void edit (const juce::String& what) { history.push (project, what); dirty = true; }
    void notify() { changed.sendChangeMessage(); }
    Ticks snapT (Ticks t) const { return snapOn ? snapTime (t, project, snap) : t; }
};

// ---- playback: song + clip audio through mixSequence and the master plug-in rack; the audio clock drives the preview
class Player : public juce::AudioIODeviceCallback
{
public:
    Player (AppState& s, PluginRack& r) : st (s), rack (r) {}
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int nOut, int n, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { rate = d->getCurrentSampleRate(); }
    void audioDeviceStopped() override {}
    void play (Ticks from);
    void stop() { playing = false; }
    bool isPlaying() const { return playing.load(); }
    Ticks position() const { return secToTicks (posSec.load()); }
    void setSequence (const Sequence& s) { const juce::ScopedLock sl (lock); seq = s; }
    static constexpr int kBlock = 512;
private:
    AppState& st;
    PluginRack& rack;
    juce::CriticalSection lock;
    Sequence seq;
    std::atomic<bool> playing { false }, restart { false };
    std::atomic<double> posSec { 0 };
    std::atomic<juce::int64> startSample { 0 };
    double rate = 48000;
    // 48 kHz mixed + plug-in processed FIFO, read with a fractional cursor (device rate may differ)
    std::vector<float> fL, fR, bL, bR;
    juce::int64 fifoStart = 0, mixPos = 0;
    double readPos = 0;
};

// ---- preview: renders on a background thread, so the UI never blocks
class Preview : public juce::Component, private juce::Thread, private juce::AsyncUpdater
{
public:
    explicit Preview (AppState& s);
    ~Preview() override;
    void paint (juce::Graphics&) override;
    void requestFrame (Ticks t);
    juce::Rectangle<int> frameArea() const;
    void sourcesChanged() { clearReq = true; requestFrame (lastReq); }
    void setProject (const Project& p);
    double renderMs() const { return lastMs; }
    void setLive (bool b) { if (live.exchange (b) && ! b) requestFrame (lastReq); }
private:
    void run() override;
    void handleAsyncUpdate() override { repaint(); }
    AppState& st;
    Renderer renderer;
    juce::CriticalSection lock;
    juce::WaitableEvent wake;
    Ticks want = -1, lastReq = 0;
    std::shared_ptr<const Project> snap;
    int wantW = 0, wantH = 0;
    std::atomic<bool> clearReq { false }, live { false };
    int wantDecodeH = 0;
    juce::Image shown;
    double lastMs = 0;
};

// ---- timeline
enum class Tool { Select, Paint, Delete, Cut };
class Timeline : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    explicit Timeline (AppState& s);
    std::function<void (const juce::Array<juce::File>&, int track, Ticks at)> onFilesDropped;   // from Finder/Explorer
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int x, int y) override;
    std::function<void()> onEdited;          // after any edit
    std::function<void (Ticks)> onSeek;
    Tool tool = Tool::Select;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;
    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;
    void zoomBy (float f, int anchorX);
    void ensureVisible (Ticks t);
    void clipMenu (const juce::String& id);
    bool switchAngles = true;
    double pxPerSec = 60;   // zoom
    double scrollSec = 0;
private:
    static constexpr int kRuler = 28, kHeader = 70;
    int trackY (int i) const;
    int trackAt (int y) const;
    double xToSec (int x) const { return scrollSec + (x - kHeader) / pxPerSec; }
    int secToX (double s) const { return kHeader + (int) std::lround ((s - scrollSec) * pxPerSec); }
    const Clip* clipAt (juce::Point<int> p, int& edge) const;
    AppState& st;
    enum class Drag { None, Move, TrimL, TrimR, Range, Scrub } drag = Drag::None;
    juce::String dragClip;
    Ticks dragStart = 0, dragOrig = 0; int dragTrack = 0;
    Ticks snapGuide = -1;
    Ticks magnetSnap (Ticks t, Ticks len);
    Project beforeDrag;
};

// ---- panels
class MediaPanel : public juce::Component, public juce::DragAndDropContainer, private juce::ListBoxModel
{
public:
    explicit MediaPanel (AppState& s);
    void refresh() { list.updateContent(); list.repaint(); }
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<void()> onImport, onSyncAll;
    std::function<void (const juce::String&)> onAddToTimeline, onSetSong, onRelink, onRemove, onResync;
private:
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override;
    juce::String getTooltipForRow (int) override;
    void listBoxItemClicked (int, const juce::MouseEvent&) override;
    AppState& st;
    juce::ListBox list;
    juce::TextButton importBtn { "Import" }, syncBtn { "Auto Sync" };
};

class Inspector : public juce::Component
{
public:
    explicit Inspector (AppState& s);
    void rebuild();            // selection changed
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<void()> onEdited;
private:
    AppState& st;
    juce::Viewport view;
    juce::Component content;
    juce::OwnedArray<juce::Component> items;
    juce::Label title;
    juce::String target;       // clip id or "" = sequence
};
}
