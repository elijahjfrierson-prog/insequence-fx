// Headless tests: run with `ISFXTests` (ctest). Every check prints PASS/FAIL; exit code = failures.
#include "../Source/Model.h"
#include "../Source/Media.h"
#include "../Source/Analysis.h"
#include "../Source/Editing.h"
#include "../Source/Looks.h"
#include "../Source/AudioMix.h"
#include "../Source/Exporter.h"
#include "../Source/Renderer.h"
#include "../Source/Presets.h"
#include <iostream>
#include <chrono>
#include <set>
#include <functional>

using namespace isfx;
static int failures = 0;
#define CHECK(cond, msg) do { bool ok_ = (cond); std::cout << (ok_ ? "PASS " : "FAIL ") << msg << "\n"; if (! ok_) ++failures; } while (0)

// synthetic drum loop: kick on beats 1&3, snare on 2&4, closed hats on 1/8 notes, plus a bass note
static std::vector<float> drumSong (double bpm, double seconds, double startOffset, int rate, double bpm2 = 0)
{
    std::vector<float> x ((size_t) (seconds * rate), 0.0f);
    juce::Random rng (42);
    auto addKick = [&] (double t) {
        size_t s0 = (size_t) (t * rate);
        double ph = 0;
        for (size_t i = 0; i < (size_t) (0.25 * rate) && s0 + i < x.size(); ++i)
        {
            double tt = (double) i / rate; double f = 50 + 120 * std::exp (-tt * 30);
            ph += juce::MathConstants<double>::twoPi * f / rate;
            x[s0 + i] += (float) (0.9 * std::sin (ph) * std::exp (-tt * 12));
        }
    };
    auto addNoise = [&] (double t, double len, double decay, float amp, bool hp) {
        size_t s0 = (size_t) (t * rate); float prev = 0;
        for (size_t i = 0; i < (size_t) (len * rate) && s0 + i < x.size(); ++i)
        {
            float n = rng.nextFloat() * 2 - 1; float v = hp ? n - prev : n; prev = n;
            x[s0 + i] += amp * v * (float) std::exp (-(double) i / rate * decay);
        }
    };
    double t = startOffset; int beat = 0;
    while (t < seconds - 0.3)
    {
        double b = 60.0 / ((bpm2 > 0 && t > seconds / 2) ? bpm2 : bpm);
        if (beat % 2 == 0) addKick (t); else { addNoise (t, 0.2, 18, 0.5f, false); addKick (t + 0.0); }
        if (beat % 2 == 1) {}
        addNoise (t, 0.05, 80, 0.25f, true);
        addNoise (t + b / 2, 0.05, 80, 0.25f, true);
        t += b; ++beat;
    }
    return x;
}

static void testTiming()
{
    SongAnalysis a; a.bpm = 140; a.gridOffset = 0;
    double sixteenth = ticksToSec (a.subdivTime (1, 4)) * 1000.0;
    CHECK (std::abs (sixteenth - 107.142857) < 1e-4, "140 BPM 1/16 = 107.142857 ms (got " << sixteenth << ")");
    // 1000th sixteenth computed directly, no accumulated rounding
    double t1000 = ticksToSec (a.subdivTime (1000, 4));
    CHECK (std::abs (t1000 - 1000 * 60.0 / 140 / 4) < 1e-8, "no drift after 1000 sixteenths");
    // frame quantization at 24 fps keeps ideal time in the model
    Ticks ideal = a.subdivTime (3, 4);
    Ticks q = quantizeToFrame (ideal, 24.0);
    CHECK (std::abs (ticksToSec (q) * 24.0 - std::round (ticksToSec (q) * 24.0)) < 1e-6, "24 fps quantized to a real frame");
    CHECK (std::abs (ticksToSec (q - ideal)) <= 0.5 / 24.0 + 1e-9, "quantization error <= half a frame");
    CHECK (frameToTicks (24, 24.0) == kFlicks && frameToTicks (60, 60.0) == kFlicks && frameToTicks (30000, 29.97) % 1 == 0, "flicks exact for 24/60 fps");
}

static void testAnalysis()
{
    const int R = kAnalysisRate;
    for (double bpm : { 120.0, 140.0, 150.0 })
    {
        auto x = drumSong (bpm, 30, 0.5, R);
        auto a = analyzeSong (x, R);
        CHECK (a.valid && std::abs (a.bpm - bpm) < 0.6, "BPM " << bpm << " detected as " << a.bpm);
        double off = ticksToSec (a.gridOffset), beat = 60.0 / bpm;
        double phaseErr = std::fmod (std::abs (off - 0.5), beat); phaseErr = std::min (phaseErr, beat - phaseErr);
        CHECK (phaseErr < 0.025, "beat phase within 25 ms at " << bpm << " (err " << phaseErr * 1000 << " ms)");
        int kicks = 0, hats = 0, snares = 0;
        for (auto& e : a.events) { if (e.type == EventType::Kick) ++kicks; if (e.type == EventType::HiHat) ++hats; if (e.type == EventType::Snare) ++snares; }
        int beats = (int) ((30 - 0.8) / beat);
        CHECK (kicks > beats * 0.6 && kicks < beats * 1.4, "kicks detected " << kicks << " (~" << beats << ")");
        CHECK (hats > beats * 1.2, "hi-hats detected " << hats << " (~" << beats * 2 << ")");
        CHECK (snares > beats * 0.25, "snares detected " << snares << " (~" << beats / 2 << ")");
        // hi-hat events land on 1/8 grid
        int onGrid = 0, n = 0;
        for (auto& e : a.events) if (e.type == EventType::HiHat)
        {
            double pos = (ticksToSec (e.time) - 0.5) / (beat / 2); double d = std::abs (pos - std::round (pos)) * beat / 2;
            if (d < 0.03) ++onGrid; ++n;
        }
        CHECK (n > 0 && onGrid > n * 0.8, "hi-hats on the 1/8 grid " << onGrid << "/" << n);
    }
    {
        auto x = drumSong (120, 60, 0.5, R, 140);
        auto a = analyzeSong (x, R);
        CHECK (a.valid && (std::abs (a.bpm - 120) < 1 || std::abs (a.bpm - 140) < 1), "tempo-change song locks to one section tempo (" << a.bpm << ")");
    }
    {
        auto t0 = std::chrono::steady_clock::now();
        auto x = drumSong (128, kMaxSongSeconds, 0.25, R);
        auto a = analyzeSong (x, R);
        double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (a.valid && std::abs (a.bpm - 128) < 0.6, "10-minute song analyzed (bpm " << a.bpm << ", " << secs << " s)");
        CHECK (a.barEnergy.size() > 300, "10-minute song has " << a.barEnergy.size() << " bars");
    }
}

static void testSync()
{
    const int R = kAnalysisRate;
    auto song = drumSong (140, 60, 0.3, R);
    // add a non-periodic melody so the correlation has a unique peak
    juce::Random rng (7); double ph = 0, f = 220;
    for (size_t i = 0; i < song.size(); ++i) { if (i % (R / 3) == 0) f = 200 + rng.nextInt (400); ph += 6.2831853 * f / R; song[i] += 0.2f * (float) std::sin (ph); }
    const double startAt = 12.345;
    std::vector<float> take (song.begin() + (long) (startAt * R), song.begin() + (long) ((startAt + 20) * R));
    juce::Random n2 (3);
    for (size_t i = 0; i < take.size(); ++i) take[i] = take[i] * 0.6f + (n2.nextFloat() - 0.5f) * 0.15f;  // room mic + noise
    auto r = syncToSong (song, take, R);
    CHECK (r.ok && std::abs (ticksToSec (r.offset) - startAt) < 0.002, "take synced at " << ticksToSec (r.offset) << " s (expected 12.345), conf " << r.confidence);
    std::vector<float> noise ((size_t) R * 20); for (auto& v : noise) v = n2.nextFloat() - 0.5f;
    auto r2 = syncToSong (song, noise, R);
    CHECK (! r2.ok, "unrelated audio flagged uncertain (conf " << r2.confidence << ")");
}

static void testPremiereCommands()
{
    Project p; p.fps = 30;
    Sequence s = Project::makeDefaultSequence ("x");
    auto mk = [] (const char* id, double a, double b, double src) { Clip c; c.id = id; c.mediaId = "none"; c.track = 0; c.start = secToTicks (a); c.length = secToTicks (b - a); c.srcIn = secToTicks (src); return c; };
    s.clips = { mk ("a", 0, 4, 0), mk ("b", 4, 8, 4) };
    s.findClip ("a")->fx.set ("exposure", 0.7f);
    auto cb = copyClips (s, { "a" });
    auto pasted = pasteClips (s, cb, secToTicks (6));
    const Clip* pc = pasted.size() == 1 ? s.findClip (pasted[0]) : nullptr;
    const Clip* bl = s.findClip ("b");
    CHECK (pc && pc->start == secToTicks (6) && pc->id != "a" && bl && bl->end() == secToTicks (6) && bl->srcAt (secToTicks (5)) == secToTicks (5),
           "overwrite paste at the playhead keeps the clip under it in sync");
    pasteAttributes (s, cb.front(), { "b" });
    CHECK (s.findClip ("b")->fx.get ("exposure", 0) == 0.7f, "paste attributes copies effects");
    auto dup = duplicateClips (s, { "a" });
    CHECK (dup.size() == 1 && s.findClip (dup[0])->start == secToTicks (4), "duplicate lands right after the selection");
    s.clips = { mk ("a", 0, 4, 0), mk ("b", 4, 8, 4) };
    auto h = freezeFrame (s, "a", secToTicks (1), secToTicks (1));
    const Clip* hc = h.isNotEmpty() ? s.findClip (h) : nullptr;
    const Clip* tail = nullptr; for (auto& c : s.clips) if (c.start == secToTicks (2)) tail = &c;
    CHECK (hc && hc->speed == 0.0 && hc->srcAt (secToTicks (1.9)) == secToTicks (1) && tail && tail->srcAt (secToTicks (3)) == secToTicks (3),
           "frame hold freezes one segment and the rest stays in sync");
    s.clips = { mk ("a", 0, 4, 0), mk ("b", 4, 8, 4) };
    CHECK (setClipSpeed (s, p, "a", 0.5) && s.findClip ("a")->length == secToTicks (4) && setClipSpeed (s, p, "a", 2.0) && s.findClip ("a")->length == secToTicks (1),
           "speed/duration keeps the start and never runs into the next clip");
    s.findClip ("a")->enabled = false;
    auto j = s.findClip ("a");
    CHECK (! j->enabled, "clips can be disabled");
}

static void testDropInBetween()
{
    Project p; p.fps = 30;
    Sequence s = Project::makeDefaultSequence ("x");
    auto mk = [] (const char* id, int track, double a, double b, double src) { Clip c; c.id = id; c.mediaId = "none"; c.track = track; c.start = secToTicks (a); c.length = secToTicks (b - a); c.srcIn = secToTicks (src); return c; };
    s.clips = { mk ("a", 0, 0, 4, 0), mk ("b", 0, 4, 8, 4), mk ("c", 0, 3, 5, 0) };
    Ticks bSongAt6 = s.findClip ("b")->srcAt (secToTicks (6));
    settleClips (s, { "c" });
    auto* a = s.findClip ("a"); auto* c = s.findClip ("c"); const Clip* b = nullptr;
    for (auto& k : s.clips) if (k.track == 0 && k.start == secToTicks (5)) b = &k;   // trimmed remainder of "b"
    CHECK (a && b && c && a->end() == secToTicks (3) && b->start == secToTicks (5) && c->start == secToTicks (3),
           "drop in between covers only its spot");
    CHECK (b && b->srcAt (secToTicks (6)) == bSongAt6, "neighbour stays in sync after a drop");
    s.clips = { mk ("a", 0, 0, 3, 0), mk ("b", 0, 4, 8, 4) };
    fillGaps (s, p, 0);
    CHECK (s.findClip ("a")->end() == secToTicks (4) && s.findClip ("b")->start == secToTicks (4) && s.findClip ("b")->srcIn == secToTicks (4),
           "magnet fills the gap without sliding synced clips");
    int audioTrack = -1, videos = 0;
    for (int i = 0; i < (int) s.tracks.size(); ++i) { if (s.tracks[(size_t) i].kind == TrackKind::Audio && audioTrack < 0) audioTrack = i; if (s.tracks[(size_t) i].kind == TrackKind::Video) ++videos; }
    if (audioTrack >= 0) s.clips.push_back (mk ("song", audioTrack, 0, 8, 0));
    int nt = insertVideoTrack (s);
    int videos2 = 0; for (auto& t : s.tracks) if (t.kind == TrackKind::Video) ++videos2;
    CHECK (videos2 == videos + 1 && s.tracks[(size_t) nt].kind == TrackKind::Video && (audioTrack < 0 || s.findClip ("song")->track == audioTrack + 1),
           "dragging past the layers adds a video track and keeps audio clips on their track");
}

static Project syntheticProject()
{
    Project p = Project::makeNew();
    MediaItem song; song.id = "song"; song.kind = MediaKind::Audio; song.duration = secToTicks (60); song.name = "song.wav";
    p.media.push_back (song); p.songId = "song";
    for (int i = 0; i < 4; ++i)
    {
        MediaItem m; m.id = "take" + juce::String (i); m.kind = MediaKind::Video; m.duration = secToTicks (50 + i);
        m.sync.status = SyncInfo::Synced; m.sync.offset = secToTicks (i * 2.0); m.quality = 0.3f + 0.2f * i; m.name = m.id;
        p.media.push_back (m);
    }
    p.song.valid = true; p.song.bpm = 140; p.song.gridOffset = secToTicks (0.3); p.song.duration = secToTicks (60);
    for (int b = 0; b < 35; ++b) { p.song.barEnergy.push_back (b < 8 ? 0.2f : (b < 20 ? 0.5f : 1.0f)); p.song.barSection.push_back (b < 8 ? 0 : (b < 20 ? 1 : 2)); }
    for (int k = 0; k < 500; ++k)
    {
        p.song.events.push_back ({ p.song.subdivTime (k, 2), EventType::HiHat, 0.5f, 0.8f });
        if (k % 2 == 0) p.song.events.push_back ({ p.song.subdivTime (k, 2), (k / 2) % 2 ? EventType::Snare : EventType::Kick, 0.8f, 0.9f });
    }
    return p;
}

static void testEditing()
{
    Project p = syntheticProject();
    History h;
    auto id = addMediaClip (p, "take1", 0, 0);
    CHECK (id.isNotEmpty() && p.seq().clips.size() == 1, "clip added");
    h.push (p, "split");
    Ticks cut = p.song.subdivTime (40, 4);
    CHECK (splitAt (p.seq(), cut), "C cut at 1/16 line");
    CHECK (p.seq().clips.size() == 2 && p.seq().clips[1].srcIn == cut, "split keeps source time");
    h.push (p, "delete");
    auto right = p.seq().clips[1].id;
    deleteClips (p.seq(), { p.seq().clips[0].id }, true);
    CHECK (p.seq().clips.size() == 1 && p.seq().clips[0].start == 0, "D delete + ripple closes gap");
    CHECK (h.undo (p) && p.seq().clips.size() == 2, "undo delete");
    CHECK (h.undo (p) && p.seq().clips.size() == 1, "undo split");
    CHECK (h.redo (p) && p.seq().clips.size() == 2, "redo split");
    auto painted = paintClip (p.seq(), right, secToTicks (55), 1);
    CHECK (painted.isNotEmpty() && p.seq().findClip (painted)->track == 1, "P paint copies clip");
    auto* c = p.seq().findClip (right);
    Ticks before = c->srcIn;
    CHECK (trimClip (p.seq(), p, right, true, c->start + secToTicks (1)) && p.seq().findClip (right)->srcIn == before + secToTicks (1), "T trim left moves source in");
    CHECK (snapTime (secToTicks (1.0), p, SnapMode::Sixteenth) == p.song.subdivTime (std::llround ((1.0 - 0.3) / (60.0 / 140 / 4)), 4), "snap to 1/16");
}

static void testChopAndGenerate()
{
    Project p = syntheticProject();
    Ticks a = p.song.gridTime (16), b = p.song.gridTime (32);
    int n = chopRange (p, a, b, ChopMode::Grid16, 1);
    CHECK (n == 64, "CHOP 1/16 over 4 bars = 64 segments (got " << n << ")");
    bool synced = true, alternates = true;
    juce::String prev;
    for (auto& c : p.seq().clips) if (c.track == 0)
    {
        auto* m = p.findMedia (c.mediaId);
        if (c.srcIn != c.start - m->sync.offset) synced = false;
        if (c.mediaId == prev) alternates = false;
        prev = c.mediaId;
    }
    CHECK (synced, "every chopped segment keeps performance sync");
    CHECK (alternates, "chop switches angle on every hit");
    Project p2 = syntheticProject();
    int nh = chopRange (p2, a, b, ChopMode::HiHat, 1);
    CHECK (nh == 32, "HI-HAT chop over 4 bars = 32 hits (got " << nh << ")");

    Project g1 = syntheticProject(), g2 = syntheticProject();
    auto idx = generateEdits (g1, 99);
    generateEdits (g2, 99);
    CHECK (idx.size() == 3 && g1.sequences.size() == 4, "GENERATE 3 EDITS creates 3 sequences");
    bool det = true;
    for (int i : idx)
    {
        auto &s1 = g1.sequences[(size_t) i], &s2 = g2.sequences[(size_t) i];
        if (s1.clips.size() != s2.clips.size()) det = false;
        else for (size_t k = 0; k < s1.clips.size(); ++k) if (s1.clips[k].start != s2.clips[k].start || s1.clips[k].mediaId != s2.clips[k].mediaId) det = false;
    }
    CHECK (det, "generation is deterministic for a seed");
    CHECK (g1.sequences[1].name.contains ("Cinematic") && g1.sequences[2].name.contains ("Underground") && g1.sequences[3].name.contains ("Performance"), "3 styles named");
    size_t c0 = g1.sequences[1].clips.size(), c2 = g1.sequences[3].clips.size();
    CHECK (c2 > c0, "Performance cut is faster than Cinematic (" << c2 << " vs " << c0 << " clips)");
    for (int i : idx)
    {
        auto& s = g1.sequences[(size_t) i];
        bool gridded = true; Ticks covered = 0;
        for (auto& c : s.clips) if (c.track == 0) { covered += c.length; }
        CHECK (covered > secToTicks (55), s.name << " covers the song (" << ticksToSec (covered) << " s)");
        (void) gridded;
    }
    // keep the first 15 s, then continue manually
    g1.active = idx[0];
    keepRange (g1.seq(), 0, secToTicks (15));
    Ticks vEnd = 0; bool songKept = false;
    for (auto& c : g1.seq().clips) { if (c.track == 0) vEnd = std::max (vEnd, c.end()); if (c.mediaId == "song") songKept = c.length == secToTicks (60); }
    CHECK (vEnd == secToTicks (15) && songKept, "keep first 15 s: video ends at 15 s, song untouched");
    int more = chopRange (g1, secToTicks (15), secToTicks (25), ChopMode::Kick, 5);
    CHECK (more > 0, "manual continuation after 15 s with KICK chop (" << more << " cuts)");
}

static void testPersistence()
{
    Project p = syntheticProject();
    generateEdits (p, 5);
    p.seq().fx.set ("grain", 0.42f); p.seq().fx.lutPath = "/tmp/x.cube";
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("isfx_test.isfx");
    juce::String err;
    CHECK (saveProject (p, f, err), "save project");
    Project q;
    CHECK (loadProject (f, q, err), "reopen project " << err);
    CHECK (q.sequences.size() == p.sequences.size() && q.seq().clips.size() == p.seq().clips.size(), "sequences + clips round-trip");
    CHECK (std::abs (q.seq().fx.get ("grain", 0) - 0.42f) < 1e-6 && q.seq().fx.lutPath == "/tmp/x.cube", "effects + LUT reference round-trip");
    CHECK (q.song.events.size() == p.song.events.size() && q.song.bpm == p.song.bpm && q.song.gridOffset == p.song.gridOffset, "analysis round-trip");
    bool same = true;
    for (size_t s = 0; s < p.sequences.size(); ++s)
        for (size_t k = 0; k < p.sequences[s].clips.size(); ++k)
        {
            auto &x = p.sequences[s].clips[k], &y = q.sequences[s].clips[k];
            if (x.start != y.start || x.length != y.length || x.srcIn != y.srcIn || x.mediaId != y.mediaId) same = false;
        }
    CHECK (same, "clip timing exact after reload");
    CHECK (q.findMedia ("take0")->missing, "missing media detected on reopen");
    f.deleteFile();
}

static void testLut()
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("isfx_identity.cube");
    juce::String s = "TITLE \"id\"\nLUT_3D_SIZE 2\n";
    for (int b = 0; b < 2; ++b) for (int g = 0; g < 2; ++g) for (int r = 0; r < 2; ++r) s << r << " " << g << " " << b << "\n";
    f.replaceWithText (s);
    Lut3D l; juce::String err;
    CHECK (l.load (f, err) && l.size == 2 && l.rgb.size() == 24, ".cube LUT parsed");
    CHECK (styleModes().size() == 6 && lookLibrary().size() >= 12, "6 style modes and real look library");
    f.deleteFile();
}

static void testExport()
{
    // real media round trip: write a synced take with ffmpeg-free path (our encoder), then decode it back
    Sequence s = Project::makeDefaultSequence ("x");
    AudioCache cache;
    auto stereo = std::make_shared<std::vector<float>> ((size_t) kMixRate * 2 * 3);
    for (size_t i = 0; i < stereo->size() / 2; ++i) { float v = 0.3f * (float) std::sin (i * 2 * 3.14159 * 440 / kMixRate); (*stereo)[2 * i] = v; (*stereo)[2 * i + 1] = v; }
    cache.put ("tone", stereo);
    Clip c; c.id = "a"; c.mediaId = "tone"; c.track = 2; c.start = 0; c.length = secToTicks (3); s.clips.push_back (c);
    ExportJob job; job.width = 320; job.height = 180; job.fps = 24; job.mbps = 2;
    job.output = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("isfx_export.mp4");
    std::atomic<bool> cancel { false }; juce::String err, codec;
    bool ok = runExport (s, cache, job, [&] (Ticks t, std::vector<juce::uint8>& px)
    {
        auto f = ticksToFrame (t, 24);
        for (size_t i = 0; i < px.size(); i += 4) { px[i] = (juce::uint8) (f * 10); px[i + 1] = 80; px[i + 2] = 160; px[i + 3] = 255; }
        return true;
    }, nullptr, cancel, err, &codec);
    CHECK (ok, "export MP4 (" << codec << ") " << err);
    auto pr = probeMedia (job.output);
    CHECK (pr.ok && pr.hasAudio && pr.width == 320 && std::abs (ticksToSec (pr.duration) - 3.0) < 0.1, "exported file probes: " << pr.width << "x" << pr.height << " " << ticksToSec (pr.duration) << " s");
    VideoDecoder d; Frame fr;
    CHECK (d.open (job.output, 0, err) && d.frameAt (secToTicks (1.0), fr) && std::abs ((int) fr.rgba[0] - 240) < 30, "decoded frame at 1 s matches rendered frame 24");
    std::vector<float> back;
    CHECK (decodeAudio (job.output, kMixRate, 2, back, err) && std::abs ((double) back.size() / 2 / kMixRate - 3.0) < 0.06, "export audio length matches video (A/V sync)");
    job.output.deleteFile();

    // edit proxy: 720p source -> 540p MJPEG, frame-accurate and decodes back at the same time
    ExportJob hd; hd.width = 1280; hd.height = 720; hd.fps = 24; hd.mbps = 4;
    hd.output = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("isfx_hd.mp4");
    ok = runExport (s, cache, hd, [&] (Ticks t, std::vector<juce::uint8>& px)
    {
        auto f = ticksToFrame (t, 24);
        for (size_t i = 0; i < px.size(); i += 4) { px[i] = (juce::uint8) (f * 10); px[i + 1] = 80; px[i + 2] = 160; px[i + 3] = 255; }
        return true;
    }, nullptr, cancel, err, &codec);
    auto pf = proxyFileFor (hd.output, kProxyHeight); pf.deleteFile();
    CHECK (ok && makeProxy (hd.output, pf, kProxyHeight, 3.0, nullptr, err), "build 540p edit proxy " << err);
    VideoDecoder pd; Frame pfr;
    CHECK (pd.open (pf, kProxyHeight, err) && pd.height() == 540 && pd.width() == 960 && pd.frameAt (secToTicks (1.0), pfr)
           && std::abs ((int) pfr.rgba[0] - 240) < 30, "proxy decodes 960x540 with the right frame at 1 s (" << pd.width() << "x" << pd.height() << ")");
    CHECK (pd.frameAt (secToTicks (0.5), pfr) && std::abs ((int) pfr.rgba[0] - 120) < 30, "proxy seeks back frame-accurately");
    pf.deleteFile(); hd.output.deleteFile();
}


static juce::File writePng (const juce::String& name, std::function<juce::Colour (int, int)> fn, int w = 320, int h = 180)
{
    juce::Image im (juce::Image::ARGB, w, h, true);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) im.setPixelAt (x, y, fn (x, y));
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteFile();
    juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (im, os);
    return f;
}

static double diff (const std::vector<juce::uint8>& a, const std::vector<juce::uint8>& b)
{
    double d = 0; for (size_t i = 0; i < a.size() && i < b.size(); ++i) d += std::abs ((int) a[i] - (int) b[i]);
    return d / std::max<size_t> (1, a.size());
}

static constexpr int W = 192, H = 108;
static void testRender()
{
    auto gradF = writePng ("isfx_grad.png", [] (int x, int y) { return juce::Colour ((juce::uint8) (x * 255 / 319), (juce::uint8) (y * 255 / 179), (juce::uint8) ((x + y) % 256)); });
    auto greenF = writePng ("isfx_green.png", [] (int x, int y) { return (x > 100 && x < 220 && y > 50 && y < 130) ? juce::Colour (230, 40, 200) : juce::Colour (20, 220, 30); });
    Project p = Project::makeNew();
    auto addImg = [&] (const juce::File& f) { MediaItem m; m.id = newId(); m.path = f.getFullPathName(); m.kind = MediaKind::Image; m.width = 320; m.height = 180; m.duration = secToTicks (10); p.media.push_back (m); return m.id; };
    auto g = addImg (gradF), gr = addImg (greenF);
    auto& s = p.seq();
    Clip a; a.id = "a"; a.mediaId = g; a.track = 0; a.start = 0; a.length = secToTicks (1); s.clips.push_back (a);
    Clip b = a; b.id = "b"; b.start = secToTicks (1); b.length = secToTicks (1); s.clips.push_back (b);
    // song events right before t0 so beat FX fire
    Ticks t0 = secToTicks (1.02);
    p.song.valid = true; p.song.bpm = 120;
    for (auto ty : { EventType::Kick, EventType::Snare, EventType::HiHat }) p.song.events.push_back ({ t0 - secToTicks (0.005), ty, 1, 1 });
    auto renderAt = [&] (const Sequence& sq, Ticks t, int w = W, int h = H) {
        Renderer r; std::vector<juce::uint8> px;
        r.render (p, sq, t - secToTicks (1.0 / 30), w, h, px, 0);   // previous frame primes trails
        r.render (p, sq, t, w, h, px, 0);
        return px;
    };
    auto base = renderAt (s, t0);
    auto at = [&] (const std::vector<juce::uint8>& px, int x, int y, int c, int w = W) { return (int) px[((size_t) y * w + x) * 4 + c]; };
    CHECK (std::abs (at (base, W / 2, H / 2, 0) - 128) < 12 && std::abs (at (base, W / 2, H / 2, 1) - 128) < 12, "renderer draws the clip (center " << at (base, W / 2, H / 2, 0) << "," << at (base, W / 2, H / 2, 1) << ")");

    int dead = 0; juce::String deadList;
    for (auto& d : sequenceParams())
    {
        juce::String id (d.id);
        if (id == "lutMix" || id == "grainSize" || id == "trails" || id == "sharpen") continue;   // modifiers / need motion or texture (tested below)
        Sequence sq = s; float v = d.def > 0.5f * (d.min + d.max) ? d.min : d.max;
        if (id == "contrast" || id == "saturation") v = d.max;
        sq.fx.set (id, v);
        if (id == "letterbox") sq.fx.set (id, 1.0f);
        double df = diff (renderAt (sq, t0), base);
        bool finite = true;
        if (df < 0.5) { ++dead; deadList << id << " "; }
        juce::ignoreUnused (finite);
    }
    CHECK (dead == 0, "every sequence FX visibly changes the frame " << deadList);
    {
        Sequence boxed = s; for (auto& c : boxed.clips) c.fx.set ("scale", 0.5f);
        Sequence sharp = boxed; sharp.fx.set ("sharpen", 1.0f);
        auto bp = renderAt (boxed, t0), sp = renderAt (sharp, t0);
        double sd = diff (sp, bp);
        CHECK (sd > 0.1, "sharpen boosts edge detail (diff " << sd << ")");
        Sequence lifted = s; lifted.fx.set ("crvM2", 0.8f);
        auto lp = renderAt (lifted, t0);
        CHECK (at (lp, W / 2, H / 2, 0) > at (base, W / 2, H / 2, 0) + 40, "RGB curve lifts the midtones (" << at (lp, W / 2, H / 2, 0) << ")");
        Sequence redCut = s; redCut.fx.set ("crvR2", 0.2f);
        auto rp = renderAt (redCut, t0);
        CHECK (at (rp, W / 2, H / 2, 0) < at (base, W / 2, H / 2, 0) - 40 && std::abs (at (rp, W / 2, H / 2, 1) - at (base, W / 2, H / 2, 1)) < 3,
               "red curve changes only red");
    {
        juce::StringArray dead;
        for (int k = 1; k < transitionNames().size(); ++k)
        {
            Sequence tq = s; Clip* tc = nullptr;
            for (auto& c : tq.clips) if (tq.tracks[(size_t) c.track].kind == TrackKind::Video && (tc == nullptr || c.start > tc->start)) tc = &c;
            if (tc == nullptr) break;
            tc->fx.set ("scale", 0.6f);
            Sequence cutq = tq;
            tc->transIn = (TransitionType) k; tc->transLen = secToTicks (1.0);
            Ticks tm = tc->start + secToTicks (0.3);
            double d = diff (renderAt (tq, tm), renderAt (cutq, tm));
            if (d < 0.5) dead.add (transitionNames()[k] + " (" + juce::String (d, 2) + ")");
        }
        CHECK (dead.isEmpty(), "every transition visibly changes the frame " << dead.joinIntoString (", "));
    }
    {
        Sequence off = s;
        for (auto& c : off.clips) c.enabled = false;
        CHECK (diff (renderAt (off, t0), base) > 0.5, "disabled clips are not rendered");
    }
    }

    {   // echo trails need motion: a cut from the green frame to the gradient must leave an echo
        Sequence sq = s; sq.clips[0].mediaId = gr;
        auto plain = renderAt (sq, secToTicks (1.0));
        sq.fx.set ("trails", 1.0f);
        auto echo = renderAt (sq, secToTicks (1.0));
        CHECK (diff (echo, plain) > 2, "echo trails carry the previous frame (" << diff (echo, plain) << ")");
    }
    // blend modes and chroma key on V2
    Clip k; k.id = "k"; k.mediaId = gr; k.track = 1; k.start = 0; k.length = secToTicks (2);
    std::set<int> distinct;
    for (int m = 0; m < 12; ++m)
    {
        Sequence sq = s; k.fx.p.clear(); k.fx.set ("blend", (float) m); k.opacity = 0.8f; sq.clips.push_back (k);
        auto px = renderAt (sq, t0);
        distinct.insert ((int) std::lround (diff (px, base) * 10));
    }
    CHECK (distinct.size() >= 10, "12 blend modes produce distinct composites (" << (int) distinct.size() << ")");
    {
        Sequence sq = s; k.fx.p.clear(); k.opacity = 1; k.fx.set ("chromaKey", 1.0f); sq.clips.push_back (k);
        auto px = renderAt (sq, t0);
        int cx = 10, cy = 10;   // green area -> keyed to V1
        CHECK (std::abs (at (px, cx, cy, 1) - at (base, cx, cy, 1)) < 25 && at (px, W / 2, H / 2, 0) > 180, "chroma key removes green, keeps the subject");
        k.fx.p.clear(); k.fx.set ("maskShape", 1.0f); k.fx.set ("maskW", 0.4f); k.fx.set ("maskH", 0.4f);
        sq = s; sq.clips.push_back (k); px = renderAt (sq, t0);
        CHECK (diff (std::vector<juce::uint8> (px.begin(), px.begin() + 4 * 8), std::vector<juce::uint8> (base.begin(), base.begin() + 4 * 8)) < 2 && at (px, W / 2, H / 2, 0) > 180, "ellipse mask limits the overlay");
    }

    // aspect ratios + reframing
    int ow, oh;
    p.aspectW = 9; p.aspectH = 16; outputSize (p, 1080, ow, oh);
    CHECK (ow == 1080 && oh == 1920, "9:16 output 1080x1920");
    p.aspectW = 239; p.aspectH = 100; outputSize (p, 2160, ow, oh);
    CHECK (oh == 2160 && std::abs (ow - 5162) <= 2, "2.39:1 4K output " << ow << "x" << oh);
    CHECK (aspectPresets().size() >= 12, "aspect presets");
    {
        Sequence sq = s; sq.clips[0].fx.set ("reframe", 1.0f); sq.clips[1].fx.set ("reframe", 1.0f);
        auto fit = renderAt (sq, secToTicks (0.5), 108, 192);
        sq.clips[0].fx.set ("reframe", 2.0f);
        auto blur = renderAt (sq, secToTicks (0.5), 108, 192);
        auto fill = renderAt (s, secToTicks (0.5), 108, 192);
        CHECK (at (fit, 54, 5, 0, 108) < 3 && at (blur, 54, 5, 0, 108) + at (blur, 54, 5, 1, 108) > 20 && at (fill, 54, 5, 1, 108) > 0, "9:16 fit / blur-fill / fill reframing");
    }
    // full-HD cost with a heavy reference look
    {
        Sequence sq = s;
        for (auto& l : lookLibrary()) if (l.name == "Sky Chrome Haze") applyLook (sq.fx, l, 1.0f);
        Renderer r; std::vector<juce::uint8> px;
        r.render (p, sq, t0, 1920, 1080, px, 0);
        auto st = std::chrono::steady_clock::now();
        for (int i = 0; i < 3; ++i) r.render (p, sq, t0 + secToTicks (i / 30.0), 1920, 1080, px, 0);
        double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - st).count() / 3;
        CHECK (px.size() == 1920u * 1080 * 4, "1080p frame with Sky Chrome Haze renders in " << ms << " ms");
    }
    gradF.deleteFile(); greenF.deleteFile();
}

static void testLayoutsStacksAndLayers()
{
    auto redF = writePng ("isfx_red.png", [] (int, int) { return juce::Colour (230, 20, 20); });
    auto blueF = writePng ("isfx_blue.png", [] (int, int) { return juce::Colour (20, 20, 230); });
    Project p = Project::makeNew(); p.aspectW = 9; p.aspectH = 16;
    auto addImg = [&] (const juce::File& f) { MediaItem m; m.id = newId(); m.path = f.getFullPathName(); m.kind = MediaKind::Image; m.width = 320; m.height = 180; m.duration = secToTicks (10); p.media.push_back (m); return m.id; };
    auto red = addImg (redF), blue = addImg (blueF);
    auto& s = p.seq();
    int v2 = insertVideoTrack (s);
    Clip a; a.id = "a"; a.mediaId = red; a.track = 0; a.start = 0; a.length = secToTicks (2);
    Clip b = a; b.id = "b"; b.mediaId = blue; b.track = v2;
    applyLayout (p, a, 1); applyLayout (p, b, 2);
    s.clips.push_back (a); s.clips.push_back (b); s.sortClips();
    const int w = 108, h = 192;
    auto px = [&] (const Sequence& sq, Ticks t) { Renderer r; std::vector<juce::uint8> o; r.render (p, sq, t, w, h, o, 0); return o; };
    auto at = [&] (const std::vector<juce::uint8>& o, int x, int y, int ch) { return (int) o[((size_t) y * w + x) * 4 + (size_t) ch]; };
    auto split = px (s, secToTicks (0.5));
    CHECK (at (split, 54, 40, 0) > 120 && at (split, 54, 40, 2) < 100 && at (split, 54, 150, 2) > 120 && at (split, 54, 150, 0) < 100,
           "9:16 split screen: one clip on top, another underneath");
    {
        Sequence sq = s; sq.clips.clear();
        Clip c = a; c.fx = {}; c.fx.set ("scaleY", 0.5f); sq.clips.push_back (c);
        auto o = px (sq, secToTicks (0.5));
        CHECK (at (o, 54, 3, 0) < 30 && at (o, 54, 96, 0) > 120, "non-uniform Scale Y squashes the clip");
        sq.clips[0].fx = {}; sq.clips[0].fx.set ("cropT", 0.5f);
        o = px (sq, secToTicks (0.5));
        CHECK (at (o, 54, 20, 0) < 30 && at (o, 54, 170, 0) > 120, "crop top removes the upper half");
        // effect layer above darkens everything under it while the playhead is over it
        FxStack dark; dark.set ("exposure", -2.0f);
        auto plain = px (sq, secToTicks (0.5));
        auto fxId = addEffectClip (sq, dark, "Dark", 0, secToTicks (1));
        auto withFx = px (sq, secToTicks (0.5)), after = px (sq, secToTicks (1.5));
        CHECK (fxId.isNotEmpty() && at (withFx, 54, 170, 0) < at (plain, 54, 170, 0) - 40 && at (after, 54, 170, 0) == at (plain, 54, 170, 0),
               "effect layer applies only while it plays");
    }
    // on-video transform handles use the renderer's exact geometry (position, scale, rotation)
    {
        Sequence sq = s; sq.clips.clear();
        Project q = p; q.aspectW = 16; q.aspectH = 9;
        if (auto im = juce::ImageFileFormat::loadFrom (redF); im.isValid())
            for (auto& m : q.media) { m.width = im.getWidth(); m.height = im.getHeight(); }
        Clip c = a; c.fx = {}; c.fx.set ("posX", 0.4f); c.fx.set ("scale", 0.5f); c.fx.set ("rotation", 30.0f); c.fx.set ("cropR", 0.25f);
        sq.clips.push_back (c);
        Renderer r; std::vector<juce::uint8> o; const int W = 320, H = 180;
        r.render (q, sq, secToTicks (0.5), W, H, o, 0);
        ClipGeom g; bool ok = clipGeometry (q, c, (float) W, (float) H, g);
        auto red = [&] (juce::Point<float> pt) { int x = (int) pt.x, y = (int) pt.y;
            return x >= 0 && y >= 0 && x < W && y < H && o[((size_t) y * W + x) * 4] > 120 && o[((size_t) y * W + x) * 4 + 2] < 100; };
        CHECK (ok && red (g.toScreen (0, 0)) && red (g.toScreen (g.x0 * 0.85f, g.y0 * 0.85f)) && red (g.toScreen (g.x1 - 4, g.y1 * 0.85f))
               && ! red (g.toScreen (g.x0 * 1.2f, g.y0 * 1.2f)) && ! red (g.toScreen (g.x1 + 6, 0)),
               "transform handle box matches the rendered clip (move/scale/rotate/crop)");
    }
    // 1000 stacked clips: a full-frame opaque top layer hides the rest, so the frame costs about one layer
    {
        Sequence one = s; one.clips.clear();
        Clip c = a; c.fx = {}; one.clips.push_back (c);
        Sequence big = one;
        for (int i = 0; i < 999; ++i) { Clip k = c; k.id = newId(); k.mediaId = i % 2 ? red : blue; k.track = insertVideoTrack (big); big.clips.push_back (k); }
        big.sortClips();
        Renderer r; std::vector<juce::uint8> o;
        auto time = [&] (const Sequence& sq) { r.render (p, sq, 0, 540, 960, o, 0);
            auto t0 = std::chrono::steady_clock::now(); for (int i = 0; i < 3; ++i) r.render (p, sq, secToTicks (i / 30.0), 540, 960, o, 0);
            return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / 3; };
        double ms1 = time (one), ms1000 = time (big);
        CHECK (big.tracks.size() > 1000 && ms1000 < ms1 * 3 + 20, "1000 stacked clips render in " << ms1000 << " ms (1 clip: " << ms1 << " ms)");
    }
    // drag the song: synced takes, grid and hits follow; free slow-mo stays
    {
        Project q = Project::makeNew();
        MediaItem song; song.id = "song"; song.kind = MediaKind::Audio; song.duration = secToTicks (60); q.media.push_back (song); q.songId = "song";
        MediaItem take; take.id = "take"; take.kind = MediaKind::Video; take.duration = secToTicks (60); take.sync.status = SyncInfo::Synced; q.media.push_back (take);
        MediaItem broll; broll.id = "broll"; broll.kind = MediaKind::Video; broll.duration = secToTicks (60); q.media.push_back (broll);
        q.song.valid = true; q.song.gridOffset = secToTicks (0.3); q.song.events.push_back ({ secToTicks (1), EventType::Kick, 1, 1 });
        auto& sq = q.seq();
        int at0 = -1; for (int i = 0; i < (int) sq.tracks.size(); ++i) if (sq.tracks[(size_t) i].kind == TrackKind::Audio) { at0 = i; break; }
        if (at0 < 0) at0 = sq.addTrack (TrackKind::Audio, "SONG");
        Clip sc; sc.id = "s"; sc.mediaId = "song"; sc.track = at0; sc.length = secToTicks (60); sc.locked = true; sq.clips.push_back (sc);
        Clip tc; tc.id = "t"; tc.mediaId = "take"; tc.track = 0; tc.start = secToTicks (4); tc.length = secToTicks (2); tc.srcIn = secToTicks (4); sq.clips.push_back (tc);
        Clip fc; fc.id = "f"; fc.mediaId = "broll"; fc.track = 0; fc.start = secToTicks (10); fc.length = secToTicks (2); fc.speed = 0.5; sq.clips.push_back (fc);
        CHECK (shiftSong (q, secToTicks (8), true) && sq.findClip ("s")->start == secToTicks (8) && sq.findClip ("t")->start == secToTicks (12)
               && sq.findClip ("f")->start == secToTicks (10) && q.song.gridOffset == secToTicks (8.3) && q.song.events[0].time == secToTicks (9),
               "dragging the song moves synced clips, grid and hits; free slow-mo stays");
        CHECK (shiftSong (q, -secToTicks (100), false) && sq.findClip ("s")->start == 0 && sq.findClip ("t")->start == secToTicks (12),
               "Alt-drag moves only the song and stops at 0");
    }
    // transitions dropped near a cut lock onto the incoming clip
    {
        Sequence sq = s; sq.clips.clear();
        Clip x = a; x.fx = {}; x.length = secToTicks (1); Clip y = x; y.id = "y"; y.start = secToTicks (1);
        sq.clips.push_back (x); sq.clips.push_back (y);
        CHECK (setTransitionAtCut (sq, 0, secToTicks (1.08), TransitionType::Flash, secToTicks (0.25), secToTicks (0.3))
               && sq.findClip ("y")->transIn == TransitionType::Flash && sq.findClip ("y")->transLen == secToTicks (0.25), "drag-drop transition locks to the cut");
    }
    redF.deleteFile(); blueF.deleteFile();
}

static void testAutoCutAndPresets()
{
    Project p = Project::makeNew();
    p.song.valid = true; p.song.bpm = 120; p.song.gridOffset = 0; p.song.beatsPerBar = 4; p.song.duration = secToTicks (16);
    MediaItem m; m.id = "take"; m.kind = MediaKind::Video; m.duration = secToTicks (20); m.fps = 30; m.path = "/nonexistent.mov"; m.name = "take";
    m.sync.status = SyncInfo::Synced; m.sync.offset = 0; p.media.push_back (m);
    MediaItem m2 = m; m2.id = "take2"; m2.name = "take2"; p.media.push_back (m2);
    auto& s = p.seq();
    Clip c; c.id = "c"; c.mediaId = "take"; c.track = 0; c.start = 0; c.length = secToTicks (4); c.srcIn = 0; s.clips.push_back (c);   // 2 bars
    int n = autoCutClip (p, "c", AutoCut::Sixteenth, false);
    CHECK (n == 32 && p.seq().clips.size() == 32, "auto cut 1/16 on 2 bars = 32 slices (" << n << ")");
    bool onGrid = true; for (auto& k : p.seq().clips) { double b = ticksToSec (k.start) / 0.125; onGrid &= std::abs (b - std::round (b)) * 0.125 <= 1.0 / 30 && k.srcIn == k.start; }   // video cuts land on frames
    CHECK (onGrid, "1/16 slices on grid and keep sync");
    p.seq().clips.clear(); c.length = secToTicks (2); p.seq().clips.push_back (c);   // 1 bar
    n = autoCutClip (p, "c", AutoCut::Roll, false);
    bool stut = true; for (auto& k : p.seq().clips) stut &= k.srcIn == 0;
    CHECK (n == 16 && stut, "hi-hat roll: 4 x 1/8 + 4 x 1/16 + 8 x 1/32 stutter slices (" << n << ")");
    p.seq().clips.clear(); c.length = secToTicks (2); p.seq().clips.push_back (c);
    n = autoCutClip (p, "c", AutoCut::Eighth, true, 7);
    int switches = 0; for (size_t i = 1; i < p.seq().clips.size(); ++i) switches += p.seq().clips[i].mediaId != p.seq().clips[i - 1].mediaId;
    CHECK (n == 8 && switches >= 6, "auto cut with angle switching (" << switches << " switches)");
    p.seq().clips.clear(); c.length = secToTicks (2); p.seq().clips.push_back (c);
    for (double t : { 0.3, 0.8, 1.55 }) p.song.events.push_back ({ secToTicks (t), EventType::HiHat, 1, 1 });
    CHECK (autoCutClip (p, "c", AutoCut::HiHat, false) == 4, "auto cut on detected hi-hats");

    // presets
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("isfx_presets"); dir.deleteRecursively(); dir.createDirectory();
    UserPreset up; up.name = "My Night Look"; up.fx.set ("contrast", 1.3f); up.fx.set ("grain", .4f); juce::File wf; juce::String err;
    CHECK (saveUserPreset (up, wf, err, dir), "save user preset " << err);
    auto all = loadUserPresets (dir);
    CHECK (all.size() == 1 && all[0].name == "My Night Look" && std::abs (all[0].fx.get ("grain", 0) - .4f) < 1e-6, "user preset round-trip");

    // template: made at 120 BPM, applied to a 140 BPM song -> every cut on the new grid
    Project src = Project::makeNew(); src.song = p.song; src.media = p.media;
    for (int i = 0; i < 8; ++i) { Clip k; k.id = newId(); k.mediaId = i % 2 ? "take" : "take2"; k.track = 0; k.start = secToTicks (i * 0.25); k.length = secToTicks (0.25); k.srcIn = k.start; src.seq().clips.push_back (k); }
    auto tf = dir.getChildFile ("t.isfxtemplate");
    CHECK (saveEditTemplate (src, src.seq(), "Eighth Flip", tf, err), "save edit template " << err);
    Project dst = Project::makeNew(); dst.media = p.media; dst.song.valid = true; dst.song.bpm = 140; dst.song.gridOffset = secToTicks (0.2); dst.song.duration = secToTicks (10);
    CHECK (applyEditTemplate (dst, tf, err), "apply template " << err);
    bool grid = true; int vids = 0;
    for (auto& k : dst.seq().clips) { if (dst.seq().tracks[(size_t) k.track].kind != TrackKind::Video) continue; ++vids;
        double b = dst.song.beatAt (k.start) * 2; grid &= std::abs (b - std::round (b)) * (30.0 / 140) <= 1.0 / 30 + 1e-6; }   // within one video frame of the 1/8 grid
    CHECK (dst.seq().name == "Eighth Flip" && vids >= 40 && grid, "template re-locked to 140 BPM grid (" << vids << " clips)");
    dir.deleteRecursively();
}

static void testReverseAndSlowMo()
{
    Clip c; c.id = "r"; c.mediaId = "m"; c.start = secToTicks (10); c.length = secToTicks (2); c.srcIn = secToTicks (5);
    Clip f = c; c.reverse = true;
    CHECK (std::abs (ticksToSec (c.srcAt (c.start)) - 7.0) < 1e-3 && std::abs (ticksToSec (c.srcAt (c.end() - secToTicks (0.5))) - 5.5) < 1e-3,
           "reversed clip plays its source range backwards");
    Sequence s; s.tracks.push_back ({}); s.clips.push_back (c);
    const Ticks cutT = secToTicks (10.5);
    const Ticks before = c.srcAt (cutT - 1), after = c.srcAt (cutT);
    splitAt (s, cutT, {});
    s.sortClips();
    CHECK (s.clips.size() == 2 && std::llabs (s.clips[0].srcAt (cutT - 1) - before) <= 1 && std::llabs (s.clips[1].srcAt (cutT) - after) <= 1
           && std::llabs (s.clips[0].srcAt (s.clips[0].start) - c.srcAt (c.start)) <= 1,
           "splitting a reversed clip keeps every frame where it was");
    // 60 fps source at 50% on a 30 fps timeline: output frame n shows source frame n exactly (no repeats, no blends)
    f.speed = 0.5; f.srcIn = quantizeToFrame (secToTicks (5.01), 60.0);
    bool clean = true;
    for (int n = 0; n < 120; ++n)
    {
        auto src = f.srcAt (f.start + frameToTicks (n, 30.0));
        clean = clean && ticksToFrame (src + secToTicks (1.0 / 240), 60.0) == ticksToFrame (f.srcIn, 60.0) + n;
    }
    CHECK (clean, "clean slow-mo: 60 fps at 50% on a 30 fps timeline uses every real frame once");
}

static void testSyncShortClip()
{
    const int rate = kAnalysisRate;
    juce::Random rnd (7);
    std::vector<float> song ((size_t) (rate * 40));
    float env = 0;
    for (size_t i = 0; i < song.size(); ++i)
    {
        if (i % (size_t) (rate / 8) == 0) env = rnd.nextFloat() < 0.45f ? 1.0f : 0.15f;
        song[i] = env * (rnd.nextFloat() * 2 - 1);
    }
    // a take recorded from 12 s to 30 s of the song; the clip is only 0.8 s cut out of its middle
    std::vector<float> take (song.begin() + rate * 12, song.begin() + rate * 30);
    auto r = syncSegment (song, take, rate, secToTicks (9.4), secToTicks (0.8));
    CHECK (r.ok && std::abs (ticksToSec (r.offset) - 12.0) < 0.01, "short clip syncs by its own sound (offset " + juce::String (ticksToSec (r.offset), 3) + " s)");
}

static long rssMB()
{
    auto s = juce::File ("/proc/self/status").loadFileAsString();
    auto i = s.indexOf ("VmRSS:");
    return i < 0 ? 0 : s.substring (i + 6).trim().getLargeIntValue() / 1024;
}

// ISFX_BENCH="a.mp4:b.mov:c.mov": real-time playback of 3 stacked layers (no proxies), like the app's preview
static void benchPlayback (const juce::String& files, int W, int H, int decodeH, bool look, bool proxy)
{
    Project p = Project::makeNew();
    auto& s = p.seq();
    int track = 0;
    for (auto& f : juce::StringArray::fromTokens (files, ":", ""))
    {
        auto pr = probeMedia (juce::File (f));
        if (! pr.ok) { std::cout << "bench: cannot open " << f << "\n"; continue; }
        MediaItem m; m.id = newId(); m.path = f; m.name = juce::File (f).getFileName(); m.kind = MediaKind::Video;
        m.duration = pr.duration; m.fps = pr.fps; m.width = pr.width; m.height = pr.height; m.rotation = pr.rotation;
        if (proxy)
        {
            auto pf = proxyFileFor (juce::File (f), 540); juce::String err;
            if (! pf.existsAsFile()) makeProxy (juce::File (f), pf, 540, ticksToSec (pr.duration), [] (double) { return true; }, err);
            m.proxyPath = pf.getFullPathName();
        }
        p.media.push_back (m);
        if (track > 0) track = insertVideoTrack (s);
        Clip c; c.id = newId(); c.mediaId = m.id; c.track = track; c.start = 0; c.length = std::min<Ticks> (pr.duration, secToTicks (8));
        if (track > 0) { c.fx.set ("scale", 0.55f); c.fx.set ("posX", track == 1 ? -0.4f : 0.4f); }
        s.clips.push_back (c); s.sortClips();
        ++track;
    }
    if (look && ! lookLibrary().empty()) applyLook (s.fx, lookLibrary()[0], 1.0f);
    Renderer r; std::vector<juce::uint8> o;
    const long rss0 = rssMB();
    double worst = 0, sum = 0; int n = 0;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    double now = 0;
    while ((now = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0) < 6.0)
    {
        double a = juce::Time::getMillisecondCounterHiRes();
        r.render (p, s, secToTicks (now), W, H, o, decodeH);
        double ms = juce::Time::getMillisecondCounterHiRes() - a;
        worst = std::max (worst, ms); sum += ms; ++n;
    }
    std::cout << "BENCH " << (look ? "look " : "plain ") << (proxy ? "proxy " : "orig ") << W << "x" << H << " dec " << decodeH << ": " << n << " frames in 6 s (" << juce::String (n / 6.0, 1)
              << " fps), avg " << juce::String (sum / std::max (1, n), 1) << " ms, worst " << juce::String (worst, 1)
              << " ms, RSS " << rss0 << " -> " << rssMB() << " MB\n";
}

// ISFX_SOAK="a.mp4:b.mov:c.mov" ISFX_SOAK_Q=1|2|4|8 ISFX_SOAK_SEC=60: the app's playback loop for a long run.
// The playhead follows the wall clock (looping the clips) and each render shows the newest frame, so a slow frame
// is dropped rather than slowing playback. Quality tiers decode exactly like Preview (1080 / div, proxies at <= 540).
static void soakPlayback (const juce::String& files, int q, double secs)
{
    const int tier = std::max (90, 1080 / std::max (1, q));
    const bool proxy = tier <= 540;
    Project p = Project::makeNew();
    auto& s = p.seq();
    int track = 0; Ticks loopLen = secToTicks (3600);
    for (auto& f : juce::StringArray::fromTokens (files, ":", ""))
    {
        auto pr = probeMedia (juce::File (f));
        if (! pr.ok) continue;
        MediaItem m; m.id = newId(); m.path = f; m.name = juce::File (f).getFileName(); m.kind = MediaKind::Video;
        m.duration = pr.duration; m.fps = pr.fps; m.width = pr.width; m.height = pr.height; m.rotation = pr.rotation;
        if (proxy)
        {
            auto pf = proxyFileFor (juce::File (f), 540); juce::String err;
            if (! pf.existsAsFile()) makeProxy (juce::File (f), pf, 540, ticksToSec (pr.duration), [] (double) { return true; }, err);
            m.proxyPath = pf.getFullPathName();
        }
        p.media.push_back (m);
        if (track > 0) track = insertVideoTrack (s);
        Clip c; c.id = newId(); c.mediaId = m.id; c.track = track; c.start = 0; c.length = pr.duration;
        loopLen = std::min (loopLen, pr.duration);
        if (track > 0) { c.fx.set ("scale", 0.55f); c.fx.set ("posX", track == 1 ? -0.4f : 0.4f); }
        s.clips.push_back (c); s.sortClips();
        ++track;
    }
    if (! lookLibrary().empty()) applyLook (s.fx, lookLibrary()[0], 1.0f);
    // output size: the app's preview panel (~960x540 on a 1600x960 window), capped by the tier like Preview does
    const int H = std::min (540, tier), W = H * 16 / 9;
    Renderer r; std::vector<juce::uint8> o;
    const long rss0 = rssMB();
    long rssPeak = rss0;
    std::vector<double> times;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    double now = 0, nextRep = 10;
    std::cout << "SOAK quality " << (q <= 1 ? juce::String ("Full") : "1/" + juce::String (q)) << " (decode " << tier << (proxy ? "p proxy" : " original")
              << ", output " << W << "x" << H << ", " << track << " stacked layers + look), RSS start " << rss0 << " MB\n";
    int lastN = 0; double lastT = 0;
    while ((now = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0) < secs)
    {
        const double a = juce::Time::getMillisecondCounterHiRes();
        r.render (p, s, secToTicks (now) % loopLen, W, H, o, tier);
        times.push_back (juce::Time::getMillisecondCounterHiRes() - a);
        rssPeak = std::max (rssPeak, rssMB());
        if (now >= nextRep)
        {
            const int n = (int) times.size() - lastN;
            std::cout << "  t=" << juce::String (nextRep, 0) << "s  " << juce::String (n / (now - lastT), 1) << " fps  RSS " << rssMB() << " MB\n";
            lastN = (int) times.size(); lastT = now; nextRep += 10;
        }
    }
    auto sorted = times; std::sort (sorted.begin(), sorted.end());
    auto pct = [&] (double f) { return sorted.empty() ? 0.0 : sorted[(size_t) std::min<double> ((double) sorted.size() - 1, f * (double) sorted.size())]; };
    double sum = 0; for (auto x : times) sum += x;
    const int shown = (int) times.size(), expected30 = (int) (secs * 30);
    int over100 = 0; for (auto x : times) over100 += x > 100;
    std::cout << "  RESULT " << shown << " frames in " << juce::String (secs, 0) << " s = " << juce::String (shown / secs, 1) << " fps"
              << " | avg " << juce::String (sum / std::max (1, shown), 1) << " ms, median " << juce::String (pct (0.5), 1) << ", 99% " << juce::String (pct (0.99), 1)
              << ", worst " << juce::String (sorted.empty() ? 0.0 : sorted.back(), 1) << " ms | frames >100 ms: " << over100
              << " | dropped vs 30 fps: " << std::max (0, expected30 - shown)
              << " | RSS start " << rss0 << " MB, end " << rssMB() << " MB, peak " << rssPeak << " MB\n";
}

int main (int argc, char** argv)
{
    if (auto b = juce::SystemStats::getEnvironmentVariable ("ISFX_SOAK", {}); b.isNotEmpty())
    {
        soakPlayback (b, juce::SystemStats::getEnvironmentVariable ("ISFX_SOAK_Q", "2").getIntValue(),
                      juce::SystemStats::getEnvironmentVariable ("ISFX_SOAK_SEC", "60").getDoubleValue());
        return 0;
    }
    if (auto b = juce::SystemStats::getEnvironmentVariable ("ISFX_BENCH", {}); b.isNotEmpty())
    {
        benchPlayback (b, 960, 540, 540, true, false);
        benchPlayback (b, 960, 540, 540, false, false);
        benchPlayback (b, 480, 270, 270, true, false);
        benchPlayback (b, 960, 540, 540, true, true);
        return 0;
    }
    juce::ignoreUnused (argc, argv);
    testTiming();
    testAnalysis();
    testSync();
    testEditing();
    testDropInBetween();
    testPremiereCommands();
    testChopAndGenerate();
    testPersistence();
    testLut();
    testExport();
    testRender();
    testAutoCutAndPresets();
    testLayoutsStacksAndLayers();
    testReverseAndSlowMo();
    testSyncShortClip();
    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "FAILURES: ") << (failures ? juce::String (failures) : juce::String()) << "\n";
    return failures;
}
