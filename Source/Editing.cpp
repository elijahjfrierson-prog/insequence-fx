#include "Editing.h"
#include "Looks.h"
#include <algorithm>
#include <set>

namespace isfx
{
juce::StringArray snapModeNames() { return { "Off", "Bar", "Beat", "1/2", "1/4", "1/8", "1/16", "1/32", "Triplet", "Frame" }; }

// beat = quarter note. 1/8 note = 2 per beat, 1/16 = 4, 1/32 = 8
int snapPerBeat (SnapMode m)
{
    switch (m)
    {
        case SnapMode::Beat: return 1;
        case SnapMode::Half: return 1;     // handled as 2 beats
        case SnapMode::Quarter: return 1;
        case SnapMode::Eighth: return 2;
        case SnapMode::Sixteenth: return 4;
        case SnapMode::ThirtySecond: return 8;
        case SnapMode::Triplet: return 3;
        default: return 0;
    }
}

Ticks snapTime (Ticks t, const Project& p, SnapMode m)
{
    if (m == SnapMode::Off) return t;
    if (m == SnapMode::Frame || ! p.song.valid) return quantizeToFrame (t, p.fps);
    const auto& s = p.song;
    double beats = s.beatAt (t);
    if (m == SnapMode::Bar)  return s.gridTime (std::round (beats / s.beatsPerBar) * s.beatsPerBar);
    if (m == SnapMode::Half) return s.gridTime (std::round (beats / 2.0) * 2.0);
    int per = snapPerBeat (m);
    return s.subdivTime ((juce::int64) std::llround (beats * per), per);
}

juce::StringArray chopModeNames() { return { "CHOP 1/8", "CHOP 1/16", "CHOP 1/32", "KICK", "SNARE", "HI-HAT" }; }

std::vector<Ticks> chopPoints (const SongAnalysis& s, Ticks a, Ticks b, ChopMode m)
{
    std::vector<Ticks> out;
    if (b <= a) return out;
    if (m == ChopMode::Grid8 || m == ChopMode::Grid16 || m == ChopMode::Grid32)
    {
        int per = m == ChopMode::Grid8 ? 2 : (m == ChopMode::Grid16 ? 4 : 8);
        auto n0 = (juce::int64) std::ceil (s.beatAt (a) * per - 1e-6);
        for (auto n = n0;; ++n)
        {
            auto t = s.subdivTime (n, per);
            if (t >= b) break;
            if (t >= a) out.push_back (t);
        }
        return out;
    }
    EventType want = m == ChopMode::Kick ? EventType::Kick : (m == ChopMode::Snare ? EventType::Snare : EventType::HiHat);
    for (auto& e : s.events) if (e.type == want && e.time >= a && e.time < b) out.push_back (e.time);
    return out;
}

std::vector<const MediaItem*> takesCovering (const Project& p, Ticks t, Ticks len)
{
    std::vector<const MediaItem*> r;
    for (auto& m : p.media)
    {
        if (m.kind != MediaKind::Video || m.missing) continue;
        if (m.sync.status != SyncInfo::Synced && m.sync.status != SyncInfo::Manual) continue;
        if (m.sync.offset <= t && m.sync.offset + m.duration >= t + len) r.push_back (&m);
    }
    return r;
}

// ------------------------------------------------------------------ basic edits
bool splitAt (Sequence& s, Ticks t, const juce::StringArray& only)
{
    bool any = false;
    std::vector<Clip> added;
    for (auto& c : s.clips)
    {
        if (c.locked || s.tracks[(size_t) c.track].locked) continue;
        if (! only.isEmpty() && ! only.contains (c.id)) continue;
        if (t <= c.start || t >= c.end()) continue;
        Clip r = c;
        r.id = newId();
        r.srcIn = c.srcAt (t);
        r.start = t; r.length = c.end() - t;
        r.transIn = TransitionType::Cut; r.transLen = 0;
        c.length = t - c.start;
        added.push_back (r); any = true;
    }
    for (auto& a : added) s.clips.push_back (a);
    s.sortClips();
    return any;
}

void deleteClips (Sequence& s, const juce::StringArray& ids, bool ripple)
{
    std::vector<Clip> removed;
    s.clips.erase (std::remove_if (s.clips.begin(), s.clips.end(), [&] (const Clip& c)
    {
        bool del = ids.contains (c.id) && ! c.locked && ! s.tracks[(size_t) c.track].locked;
        if (del) removed.push_back (c);
        return del;
    }), s.clips.end());
    if (! ripple) return;
    // close the gaps per track, latest first so positions stay valid
    std::sort (removed.begin(), removed.end(), [] (auto& a, auto& b) { return a.start > b.start; });
    for (auto& r : removed)
        for (auto& c : s.clips)
            if (c.track == r.track && c.start >= r.end() && ! c.locked) c.start -= r.length;
}

void clearTrackRange (Sequence& s, int track, Ticks a, Ticks b)
{
    std::vector<Clip> keep;
    for (auto& c : s.clips)
    {
        if (c.track != track || c.end() <= a || c.start >= b) { keep.push_back (c); continue; }
        if (c.start < a)
        {
            Clip l = c; l.length = a - c.start; keep.push_back (l);
        }
        if (c.end() > b)
        {
            Clip r = c; r.id = newId(); r.srcIn = c.srcAt (b); r.start = b; r.length = c.end() - b;
            r.transIn = TransitionType::Cut; r.transLen = 0; keep.push_back (r);
        }
    }
    s.clips = keep;
    s.sortClips();
}

void deleteRange (Sequence& s, Ticks a, Ticks b, bool ripple, int track)
{
    for (int t = 0; t < (int) s.tracks.size(); ++t)
    {
        if (track >= 0 && t != track) continue;
        if (s.tracks[(size_t) t].locked) continue;
        clearTrackRange (s, t, a, b);
        if (ripple)
            for (auto& c : s.clips) if (c.track == t && c.start >= b) c.start -= (b - a);
    }
}

void keepRange (Sequence& s, Ticks a, Ticks b)
{
    for (int t = 0; t < (int) s.tracks.size(); ++t)
    {
        if (s.tracks[(size_t) t].kind == TrackKind::Audio) continue;   // song stays (Music Lock)
        clearTrackRange (s, t, std::numeric_limits<Ticks>::min() / 2, a);
        clearTrackRange (s, t, b, std::numeric_limits<Ticks>::max() / 2);
    }
}

bool moveClips (Sequence& s, const juce::StringArray& ids, Ticks delta, int trackDelta)
{
    for (auto& c : s.clips)
    {
        if (! ids.contains (c.id)) continue;
        int nt = c.track + trackDelta;
        if (c.locked || nt < 0 || nt >= (int) s.tracks.size()) return false;
        if (s.tracks[(size_t) nt].kind != s.tracks[(size_t) c.track].kind || s.tracks[(size_t) nt].locked) return false;
        if (c.start + delta < 0) return false;
    }
    for (auto& c : s.clips) if (ids.contains (c.id)) { c.start += delta; c.track += trackDelta; }
    s.sortClips();
    return true;
}

bool trimClip (Sequence& s, const Project& p, const juce::String& id, bool left, Ticks e)
{
    auto* c = s.findClip (id);
    if (c == nullptr || c->locked) return false;
    auto* m = p.findMedia (c->mediaId);
    Ticks mediaLen = m ? m->duration : std::numeric_limits<Ticks>::max() / 4;
    Ticks minLen = secToTicks (1.0 / p.fps);
    if (left)
    {
        Ticks newSrc = c->srcIn + (Ticks) ((double) (e - c->start) * c->speed);
        if (newSrc < 0) { e -= (Ticks) ((double) newSrc / c->speed); newSrc = 0; }
        if (c->end() - e < minLen) return false;
        c->length = c->end() - e; c->srcIn = newSrc; c->start = e;
    }
    else
    {
        Ticks maxEnd = c->start + (Ticks) ((double) (mediaLen - c->srcIn) / c->speed);
        if (m && m->kind == MediaKind::Image) maxEnd = std::numeric_limits<Ticks>::max() / 4;
        e = std::min (e, maxEnd);
        if (e - c->start < minLen) return false;
        c->length = e - c->start;
    }
    return true;
}

juce::String paintClip (Sequence& s, const juce::String& tid, Ticks at, int track)
{
    auto* t = s.findClip (tid);
    if (t == nullptr || track < 0 || track >= (int) s.tracks.size() || s.tracks[(size_t) track].locked) return {};
    if (s.tracks[(size_t) track].kind != s.tracks[(size_t) t->track].kind) return {};
    Clip c = *t; c.id = newId(); c.start = at; c.track = track;
    clearTrackRange (s, track, at, at + c.length);
    s.clips.push_back (c); s.sortClips();
    return c.id;
}

juce::String addMediaClip (Project& p, const juce::String& mid, int track, Ticks at)
{
    auto* m = p.findMedia (mid);
    auto& s = p.seq();
    if (m == nullptr) return {};
    bool audio = m->kind == MediaKind::Audio;
    if (track < 0 || track >= (int) s.tracks.size() || (s.tracks[(size_t) track].kind == TrackKind::Audio) != audio)
    {
        track = -1;
        for (int i = 0; i < (int) s.tracks.size(); ++i)
            if ((s.tracks[(size_t) i].kind == TrackKind::Audio) == audio) { track = i; break; }
        if (track < 0) track = s.addTrack (audio ? TrackKind::Audio : TrackKind::Video, audio ? "A1" : "V1");
    }
    Clip c; c.id = newId(); c.mediaId = mid; c.track = track; c.start = std::max<Ticks> (0, at);
    c.length = m->duration; c.volume = audio ? 1.0f : 0.0f;
    clearTrackRange (s, track, c.start, c.end());
    s.clips.push_back (c); s.sortClips();
    return c.id;
}

// ------------------------------------------------------------------ chop / multicam
static int firstVideoTrack (const Sequence& s)
{
    for (int i = 0; i < (int) s.tracks.size(); ++i) if (s.tracks[(size_t) i].kind == TrackKind::Video) return i;
    return 0;
}

static const MediaItem* pickTake (const std::vector<const MediaItem*>& takes, const juce::String& avoid, juce::Random& rng, float qualityWeight)
{
    if (takes.empty()) return nullptr;
    std::vector<double> w;
    double sum = 0;
    for (auto* t : takes)
    {
        double v = (t->id == avoid && takes.size() > 1) ? 0.0 : (1.0 - qualityWeight) + qualityWeight * (0.2 + t->quality) + (t->favorite ? 1.0 : 0.0);
        w.push_back (v); sum += v;
    }
    double r = rng.nextDouble() * sum;
    for (size_t i = 0; i < takes.size(); ++i) { r -= w[i]; if (r <= 0 && w[i] > 0) return takes[i]; }
    for (size_t i = takes.size(); i-- > 0;) if (w[i] > 0) return takes[i];
    return takes[0];
}

static void placeSynced (Sequence& s, int track, const MediaItem& m, Ticks a, Ticks b)
{
    Clip c; c.id = newId(); c.mediaId = m.id; c.track = track; c.start = a; c.length = b - a;
    c.srcIn = a - m.sync.offset;  // keeps lip-sync / performance sync
    c.volume = 0.0f;
    s.clips.push_back (c);
}

int chopRange (Project& p, Ticks a, Ticks b, ChopMode mode, juce::uint32 seed)
{
    auto& s = p.seq();
    if (! p.song.valid || b <= a) return 0;
    auto pts = chopPoints (p.song, a, b, mode);
    pts.insert (pts.begin(), a);
    pts.push_back (b);
    std::sort (pts.begin(), pts.end());
    pts.erase (std::unique (pts.begin(), pts.end()), pts.end());
    int track = firstVideoTrack (s);
    clearTrackRange (s, track, a, b);
    juce::Random rng ((juce::int64) seed);
    juce::String prev;
    int n = 0;
    Ticks minSeg = secToTicks (1.0 / p.fps);
    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {
        Ticks t0 = pts[i], t1 = pts[i + 1];
        if (t1 - t0 < minSeg && i + 2 < pts.size()) { pts[i + 1] = t0; continue; }
        auto takes = takesCovering (p, t0, t1 - t0);
        auto* m = pickTake (takes, prev, rng, 0.3f);
        if (m == nullptr) continue;
        placeSynced (s, track, *m, t0, t1);
        prev = m->id; ++n;
    }
    s.sortClips();
    return n;
}

int layoutSyncedTakes (Project& p)
{
    auto& s = p.seq();
    int n = 0;
    std::vector<const MediaItem*> takes;
    for (auto& m : p.media)
        if (m.kind == MediaKind::Video && (m.sync.status == SyncInfo::Synced || m.sync.status == SyncInfo::Manual)) takes.push_back (&m);
    while (s.videoTrackCount() < (int) takes.size()) s.addTrack (TrackKind::Video, "V" + juce::String (s.videoTrackCount() + 1));
    int vt = 0;
    for (auto* m : takes)
    {
        int track = -1, k = 0;
        for (int i = 0; i < (int) s.tracks.size(); ++i)
            if (s.tracks[(size_t) i].kind == TrackKind::Video && k++ == vt) { track = i; break; }
        Ticks a = std::max<Ticks> (0, m->sync.offset), b = m->sync.offset + m->duration;
        clearTrackRange (s, track, a, b);
        placeSynced (s, track, *m, a, b);
        ++vt; ++n;
    }
    s.sortClips();
    return n;
}

// ------------------------------------------------------------------ generator
struct StyleRules
{
    const char* name;
    const char* look;
    // segment length in beats for section low / mid / high
    double beats[3];
    ChopMode burst;          // used for short bursts in high sections
    double burstChance;      // per bar in high sections
    TransitionType sectionTrans;
    double qualityWeight;
    float beatFx[4];         // kickZoom, snareFlash, shake, hatStrobe
};

static const StyleRules kStyles[3] = {
    { "Cinematic", "Teal Night",        { 8, 4, 2 }, ChopMode::Grid8,  0.0,  TransitionType::Dissolve, 0.8, { 0.0f, 0.0f, 0.0f, 0.0f } },
    { "Underground", "Underground Grit",{ 4, 2, 1 }, ChopMode::HiHat,  0.35, TransitionType::Glitch,   0.3, { 0.0f, 0.25f, 0.15f, 0.25f } },
    { "Performance", "Performance Punch",{ 4, 2, 1 }, ChopMode::Kick,  0.25, TransitionType::Flash,    0.6, { 0.6f, 0.45f, 0.35f, 0.0f } },
};

static Sequence generateOne (const Project& p, const StyleRules& st, juce::uint32 seed, const juce::String& styleMode)
{
    Sequence s = Project::makeDefaultSequence (juce::String (st.name) + " Cut");
    s.style = st.name; s.seed = seed;
    juce::Random rng ((juce::int64) seed);
    const auto& a = p.song;
    Ticks songLen = std::min (a.duration, secToTicks (kMaxSongSeconds));
    int audioTrack = (int) s.tracks.size() - 1;
    if (auto* song = p.song_())
    {
        Clip c; c.id = newId(); c.mediaId = song->id; c.track = audioTrack; c.start = 0; c.length = std::min (song->duration, songLen);
        c.locked = p.musicLock; c.volume = 1.0f;
        s.clips.push_back (c);
    }
    // all takes, synced first
    std::vector<const MediaItem*> broll;
    for (auto& m : p.media)
        if (m.kind == MediaKind::Video && ! m.missing && m.sync.status != SyncInfo::Synced && m.sync.status != SyncInfo::Manual) broll.push_back (&m);

    const double beat = a.beatSec();
    int prevSection = -1;
    juce::String prev;
    // walk the song in musical segments from 0 (pickup before first downbeat is its own segment)
    Ticks t = 0;
    juce::int64 beatIdx = (juce::int64) std::ceil (a.beatAt (0));
    if (a.gridOffset > 0 && a.gridTime ((double) beatIdx) > 0)
    {
        // pickup bar: cut on the first grid line
    }
    while (t < songLen)
    {
        int sec = a.sectionAt (t + 1);
        double segBeats = st.beats[juce::jlimit (0, 2, sec)];
        // align segment to the grid
        double bt = a.beatAt (t);
        double nextBeat = std::floor (bt / segBeats + 1e-6) * segBeats + segBeats;
        Ticks t1 = std::min (songLen, a.gridTime (nextBeat));
        if (t1 - t < secToTicks (beat * 0.5)) t1 = std::min (songLen, a.gridTime (nextBeat + segBeats));

        std::vector<Ticks> cuts { t, t1 };
        if (sec == 2 && st.burstChance > 0 && rng.nextDouble() < st.burstChance)
        {
            auto extra = chopPoints (a, t, t1, st.burst);
            for (auto e : extra) cuts.push_back (e);
            std::sort (cuts.begin(), cuts.end());
            cuts.erase (std::unique (cuts.begin(), cuts.end()), cuts.end());
        }
        for (size_t i = 0; i + 1 < cuts.size(); ++i)
        {
            Ticks c0 = cuts[i], c1 = cuts[i + 1];
            if (c1 - c0 < secToTicks (1.0 / p.fps)) continue;
            auto takes = takesCovering (p, c0, c1 - c0);
            const MediaItem* m = pickTake (takes, prev, rng, (float) st.qualityWeight);
            bool useBroll = m == nullptr || (sec == 0 && ! broll.empty() && st.qualityWeight < 0.9 && rng.nextDouble() < 0.3);
            if (useBroll && ! broll.empty())
            {
                auto* b = broll[(size_t) rng.nextInt ((int) broll.size())];
                Clip c; c.id = newId(); c.mediaId = b->id; c.track = 0; c.start = c0; c.length = c1 - c0; c.volume = 0.0f;
                Ticks room = b->duration - c.length;
                if (room < 0) { c.length = b->duration; }
                c.srcIn = room > 0 ? (Ticks) (rng.nextDouble() * (double) room) : 0;
                if (c.length > 0) s.clips.push_back (c);
                prev = b->id;
            }
            else if (m != nullptr)
            {
                placeSynced (s, 0, *m, c0, c1);
                prev = m->id;
            }
            else continue;
            auto& placed = s.clips.back();
            if (sec != prevSection && prevSection >= 0 && i == 0)
            {
                placed.transIn = st.sectionTrans;
                placed.transLen = secToTicks (std::min (beat, ticksToSec (placed.length) * 0.5));
            }
            if (juce::String (st.name) == "Performance" && sec == 2 && rng.nextDouble() < 0.3)
                placed.fx.set ("scale", 1.15f + 0.15f * (float) rng.nextDouble());
        }
        prevSection = sec;
        t = t1;
    }
    for (auto& l : lookLibrary()) if (l.name == st.look) applyLook (s.fx, l, 1.0f);
    if (styleMode.isNotEmpty()) applyStyleMode (s.fx, styleMode);
    const char* beatIds[4] = { "kickZoom", "snareFlash", "shake", "hatStrobe" };
    for (int i = 0; i < 4; ++i) if (st.beatFx[i] > 0) s.fx.set (beatIds[i], st.beatFx[i]);
    s.sortClips();
    return s;
}

std::vector<int> generateEdits (Project& p, juce::uint32 seed, const juce::String& styleMode)
{
    std::vector<int> idx;
    if (! p.song.valid) return idx;
    for (int i = 0; i < 3; ++i)
    {
        p.sequences.push_back (generateOne (p, kStyles[i], seed * 7919u + (juce::uint32) i * 104729u, styleMode));
        idx.push_back ((int) p.sequences.size() - 1);
    }
    return idx;
}
}
