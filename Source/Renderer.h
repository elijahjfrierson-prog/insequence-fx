#pragma once
#include "Model.h"
#include "Media.h"
#include "Looks.h"
#include <mutex>

namespace isfx
{
struct AspectPreset { const char* name; int w, h; const char* use; };
const std::vector<AspectPreset>& aspectPresets();
// output size for the project aspect with the given short side (1080 = HD, 2160 = 4K), even dimensions
void outputSize (const Project& p, int shortSide, int& W, int& H);

juce::StringArray blendModeNames();

// Composites a sequence frame: tracks + transforms + keys/masks + blend modes, then grade, LUT, film,
// beat-reactive, acid and generated overlay FX. Preview and export use this same code path.
class Renderer
{
public:
    // decodeH caps decoded source height (preview uses a small value, export the output height)
    bool render (const Project& p, const Sequence& s, Ticks t, int W, int H, std::vector<juce::uint8>& rgba, int decodeH);
    void clearSources();   // close decoders (project changed / media relinked)
private:
    struct Source
    {
        std::unique_ptr<VideoDecoder> dec;
        Frame still, last;
        bool failed = false;
        int rotation = 0;
    };
    const Frame* frameFor (const Project& p, const juce::String& mediaId, Ticks srcT, int decodeH, int& rotation);
    std::shared_ptr<Lut3D> lutFor (const juce::String& path);

    std::map<juce::String, std::unique_ptr<Source>> sources;
    std::map<juce::String, std::shared_ptr<Lut3D>> luts;
    std::vector<float> trail;
    Ticks trailT = -1;
    int trailW = 0, trailH = 0;
};
}
