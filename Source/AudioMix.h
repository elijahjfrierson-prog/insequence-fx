#pragma once
#include "Model.h"
#include <mutex>

namespace isfx
{
// Decoded stereo audio (interleaved, kMixRate) per media id. Shared by playback and export.
constexpr int kMixRate = 48000;
class AudioCache
{
public:
    std::shared_ptr<const std::vector<float>> get (const juce::String& mediaId) const;
    void put (const juce::String& mediaId, std::shared_ptr<const std::vector<float>> stereo);
    bool has (const juce::String& mediaId) const;
private:
    mutable std::mutex lock;
    std::map<juce::String, std::shared_ptr<const std::vector<float>>> data;
};

// Mixes the sequence's audible clips for [start, start + n samples) at kMixRate into L/R (overwritten).
void mixSequence (const Sequence& s, const AudioCache& cache, juce::int64 startSample, int n, float* L, float* R);
}
