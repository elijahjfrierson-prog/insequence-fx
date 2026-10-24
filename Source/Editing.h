#pragma once
#include "Model.h"

namespace isfx
{
constexpr double kMaxSongSeconds = 600.0;   // full music videos up to 10 minutes

enum class SnapMode { Off = 0, Bar, Beat, Half, Quarter, Eighth, Sixteenth, ThirtySecond, Triplet, Frame };
juce::StringArray snapModeNames();
int snapPerBeat (SnapMode m);                 // grid lines per beat (0 = bar/frame/off)
Ticks snapTime (Ticks t, const Project& p, SnapMode m);

enum class ChopMode { Grid8 = 0, Grid16, Grid32, Kick, Snare, HiHat };
juce::StringArray chopModeNames();

// cut times inside [a, b) for a chop mode (grid modes: exact musical times; event modes: detected hits)
std::vector<Ticks> chopPoints (const SongAnalysis& s, Ticks a, Ticks b, ChopMode m);

// takes whose synced footage covers [t, t + len)
std::vector<const MediaItem*> takesCovering (const Project& p, Ticks t, Ticks len);

// ---- edit operations (all on the active sequence; caller records undo) --------------------
bool splitAt (Sequence& s, Ticks t, const juce::StringArray& onlyClips = {});
void deleteClips (Sequence& s, const juce::StringArray& ids, bool ripple);
void deleteRange (Sequence& s, Ticks a, Ticks b, bool ripple, int track = -1);
void keepRange (Sequence& s, Ticks a, Ticks b);   // removes everything outside [a, b)
bool moveClips (Sequence& s, const juce::StringArray& ids, Ticks delta, int trackDelta);
bool trimClip (Sequence& s, const Project& p, const juce::String& id, bool leftEdge, Ticks newEdge);
juce::String paintClip (Sequence& s, const juce::String& templateId, Ticks at, int track);
juce::String addMediaClip (Project& p, const juce::String& mediaId, int track, Ticks at, Ticks maxLen = 0);
int insertVideoTrack (Sequence&);                                    // new top video layer, returns its index
void settleClips (Sequence&, const juce::StringArray& ids);          // dropped clips cover what is under them
void fillGaps (Sequence&, const Project&, int track);                // sync-safe magnet: extend neighbours, never slide
void clearTrackRange (Sequence& s, int track, Ticks a, Ticks b);   // overwrite helper

// sync-preserving angle switch: replaces V1 content in [a,b) with cuts between synced takes
int chopRange (Project& p, Ticks a, Ticks b, ChopMode m, juce::uint32 seed);

// put every synced take on its own track at its sync position (multicam stack)
int layoutSyncedTakes (Project& p);

// GENERATE 3 EDITS: appends Cinematic / Underground / Performance sequences, returns their indices
std::vector<int> generateEdits (Project& p, juce::uint32 seed, const juce::String& styleMode = {});
// Right-click Auto Cut: slices one clip on the grid / detected hits, or a hi-hat-style roll
// (1/8 -> 1/16 -> 1/32 into the next bar, every slice repeating the roll's first slice = stutter).
// switchAngles: video slices cycle through the other synced takes covering each slice.
enum class AutoCut { Quarter = 0, Eighth, Sixteenth, ThirtySecond, Triplet, HiHat, Kick, Snare, Roll };
juce::StringArray autoCutNames();
int autoCutClip (Project& p, const juce::String& clipId, AutoCut mode, bool switchAngles, juce::uint32 seed = 1);
}
