#include "Looks.h"

namespace isfx
{
const std::vector<ParamDef>& sequenceParams()
{
    static const std::vector<ParamDef> d {
        { "exposure",   "Exposure",    -2, 2, 0, "Color", "Overall brightness in stops." },
        { "contrast",   "Contrast",     0, 2, 1, "Color", "Pushes darks down and brights up around mid grey." },
        { "saturation", "Saturation",   0, 2, 1, "Color", "Color intensity. 0 = black and white." },
        { "temperature","Temperature", -1, 1, 0, "Color", "Cooler (blue) to warmer (orange) white balance." },
        { "tint",       "Tint",        -1, 1, 0, "Color", "Green to magenta balance." },
        { "fade",       "Fade",         0, 1, 0, "Color", "Lifts the blacks for a faded film print." },
        { "splitTone",  "Teal/Orange",  0, 1, 0, "Color", "Teal shadows and warm highlights." },
        { "crush",      "Crush Blacks", 0, 1, 0, "Color", "Deep, inky shadows (Gothic)." },
        { "lutMix",     "LUT Intensity",0, 1, 1, "Color", "How much of the loaded .cube LUT is applied." },
        { "grain",      "Film Grain",   0, 1, 0, "Film",  "Procedural, animated film grain. Never a looped overlay." },
        { "grainSize",  "Grain Gauge",  0, 2, 1, "Film",  "0 = 35mm fine, 1 = 16mm, 2 = 8mm coarse." },
        { "halation",   "Halation",     0, 1, 0, "Film",  "Red glow bleeding around bright highlights." },
        { "bloom",      "Bloom",        0, 1, 0, "Film",  "Soft glow from the brightest parts of the image." },
        { "vignette",   "Vignette",     0, 1, 0, "Film",  "Darkens the frame edges." },
        { "letterbox",  "Letterbox",    0, 1, 0, "Film",  "Cinema bars (0.5 = 2.39:1)." },
        { "vhs",        "VHS",          0, 1, 0, "Distort","Tape chroma bleed, tracking wobble, noise lines." },
        { "rgbSplit",   "RGB Split",    0, 1, 0, "Distort","Separates red and blue channels horizontally." },
        { "glitch",     "Glitch",       0, 1, 0, "Distort","Block displacement bursts, driven by the beat." },
        { "kickZoom",   "Kick Zoom",    0, 1, 0, "Beat",  "Punch-in on every detected kick." },
        { "snareFlash", "Snare Flash",  0, 1, 0, "Beat",  "White flash on every detected snare." },
        { "shake",      "Kick Shake",   0, 1, 0, "Beat",  "Camera shake triggered by kicks." },
        { "hatStrobe",  "Hi-Hat Strobe",0, 1, 0, "Beat",  "Exposure flicker on detected hi-hats." },
        { "beatGlitch", "Beat Glitch",  0, 1, 0, "Beat",  "Glitch + RGB split hits on snares." },
    };
    return d;
}

const std::vector<ParamDef>& clipParams()
{
    static const std::vector<ParamDef> d {
        { "exposure",   "Exposure",   -2, 2, 0, "Clip", "Brightness of this clip only." },
        { "contrast",   "Contrast",    0, 2, 1, "Clip", "Contrast of this clip only." },
        { "saturation", "Saturation",  0, 2, 1, "Clip", "Saturation of this clip only." },
        { "scale",      "Scale",     0.5f, 3, 1, "Transform", "Zoom of this clip." },
        { "posX",       "Position X", -1, 1, 0, "Transform", "Horizontal offset." },
        { "posY",       "Position Y", -1, 1, 0, "Transform", "Vertical offset." },
        { "rotation",   "Rotation", -180, 180, 0, "Transform", "Rotation in degrees." },
        { "mirror",     "Mirror",      0, 1, 0, "Transform", "Flip horizontally (1 = on)." },
    };
    return d;
}

const ParamDef* findParam (const std::vector<ParamDef>& defs, const juce::String& id)
{
    for (auto& d : defs) if (id == d.id) return &d;
    return nullptr;
}

const std::vector<Look>& lookLibrary()
{
    static const std::vector<Look> L {
        { "Clean Rec709", "Color", "Neutral, no grade.", {} },
        { "Teal Night", "Cinematic", "Teal shadows, warm skin, soft bloom.",
          { { "splitTone", .7f }, { "contrast", 1.15f }, { "saturation", .9f }, { "bloom", .25f }, { "vignette", .35f }, { "letterbox", .5f } } },
        { "Warm Golden Hour", "Cinematic", "Warm highlights and gentle halation.",
          { { "temperature", .35f }, { "halation", .35f }, { "bloom", .2f }, { "contrast", 1.05f }, { "grain", .2f } } },
        { "Faded Print", "Film", "Lifted blacks, muted color, fine grain.",
          { { "fade", .35f }, { "saturation", .75f }, { "contrast", .9f }, { "grain", .35f }, { "grainSize", .3f } } },
        { "16mm Documentary", "Film", "16mm-style grain, halation and vignette.",
          { { "grain", .55f }, { "grainSize", 1.0f }, { "halation", .3f }, { "vignette", .4f }, { "fade", .12f } } },
        { "Super 8 Memory", "Film", "Coarse 8mm grain, warm, soft and vignetted.",
          { { "grain", .8f }, { "grainSize", 2.0f }, { "temperature", .3f }, { "vignette", .6f }, { "fade", .2f }, { "halation", .25f } } },
        { "Gothic Noir", "Gothic", "Deep blacks, desaturated cold tones.",
          { { "crush", .6f }, { "saturation", .35f }, { "temperature", -.3f }, { "contrast", 1.35f }, { "vignette", .55f }, { "grain", .3f } } },
        { "Blood Moon", "Gothic", "Crushed shadows with red halation.",
          { { "crush", .5f }, { "tint", .2f }, { "halation", .7f }, { "saturation", .7f }, { "contrast", 1.25f } } },
        { "VHS Rental", "VHS", "Tape bleed, noise and soft chroma.",
          { { "vhs", .75f }, { "rgbSplit", .25f }, { "saturation", 1.15f }, { "fade", .15f }, { "grain", .2f } } },
        { "Y2K Chrome", "Y2K", "Cool, glossy, bloom-heavy early-2000s look.",
          { { "temperature", -.25f }, { "bloom", .55f }, { "saturation", 1.25f }, { "contrast", 1.1f }, { "rgbSplit", .1f } } },
        { "Underground Grit", "Underground", "Harsh contrast, grain and beat glitches.",
          { { "contrast", 1.4f }, { "saturation", .6f }, { "grain", .6f }, { "grainSize", 1.4f }, { "beatGlitch", .5f }, { "rgbSplit", .2f } } },
        { "Acid Abstract", "Abstract", "Oversaturated, split channels, strobing.",
          { { "saturation", 1.8f }, { "rgbSplit", .5f }, { "hatStrobe", .35f }, { "glitch", .25f }, { "bloom", .4f } } },
        { "Performance Punch", "Beat", "Kick zoom, snare flash and kick shake.",
          { { "kickZoom", .6f }, { "snareFlash", .45f }, { "shake", .35f }, { "contrast", 1.1f } } },
        { "Mono Contrast", "Color", "High contrast black and white.",
          { { "saturation", 0.0f }, { "contrast", 1.4f }, { "grain", .3f } } },
    };
    return L;
}

void applyLook (FxStack& s, const Look& look, float amount)
{
    for (auto& [id, v] : look.p)
    {
        auto* d = findParam (sequenceParams(), id);
        float def = d ? d->def : 0.0f;
        s.set (id, def + (v - def) * amount);
    }
}

juce::StringArray styleModes() { return { "Classic", "Gothic", "VHS", "Y2K", "Underground", "Abstract" }; }

juce::String styleModeHelp (const juce::String& m)
{
    if (m == "Classic") return "Clean cinematic grade, letterbox, gentle film texture.";
    if (m == "Gothic") return "Crushed blacks, cold desaturation, red halation.";
    if (m == "VHS") return "Tape bleed, chroma shift, noise and warm fade.";
    if (m == "Y2K") return "Glossy chrome bloom, cool highlights, saturated color.";
    if (m == "Underground") return "Harsh contrast, heavy grain, beat glitches.";
    if (m == "Abstract") return "Split channels, strobes and saturated color.";
    return {};
}

void applyStyleMode (FxStack& s, const juce::String& mode)
{
    static const std::map<juce::String, juce::String> map {
        { "Classic", "Teal Night" }, { "Gothic", "Gothic Noir" }, { "VHS", "VHS Rental" },
        { "Y2K", "Y2K Chrome" }, { "Underground", "Underground Grit" }, { "Abstract", "Acid Abstract" } };
    auto it = map.find (mode);
    if (it == map.end()) return;
    // keep beat-reactive settings, reset the grade
    std::map<juce::String, float> keep;
    for (auto id : { "kickZoom", "snareFlash", "shake", "hatStrobe" }) if (s.p.count (id)) keep[id] = s.p[id];
    s.p = keep;
    for (auto& l : lookLibrary()) if (l.name == it->second) applyLook (s, l, 1.0f);
}

juce::StringArray transitionNames() { return { "Cut", "Dissolve", "Dip Black", "Dip White", "Flash", "Zoom", "Whip", "Glitch" }; }

bool Lut3D::load (const juce::File& f, juce::String& error)
{
    juce::StringArray lines; lines.addLines (f.loadFileAsString());
    size = 0; rgb.clear();
    float dmin[3] { 0, 0, 0 }, dmax[3] { 1, 1, 1 };
    for (auto& raw : lines)
    {
        auto l = raw.trim();
        if (l.isEmpty() || l.startsWithChar ('#')) continue;
        if (l.startsWith ("TITLE")) { title = l.fromFirstOccurrenceOf ("\"", false, false).upToLastOccurrenceOf ("\"", false, false); continue; }
        if (l.startsWith ("LUT_3D_SIZE")) { size = l.fromFirstOccurrenceOf (" ", false, false).trim().getIntValue(); continue; }
        if (l.startsWith ("LUT_1D_SIZE")) { error = "1D LUTs are not supported, use a 3D .cube"; return false; }
        if (l.startsWith ("DOMAIN_MIN") || l.startsWith ("DOMAIN_MAX"))
        {
            auto t = juce::StringArray::fromTokens (l, " \t", ""); t.removeEmptyStrings();
            float* dst = l.startsWith ("DOMAIN_MIN") ? dmin : dmax;
            for (int i = 0; i < 3 && i + 1 < t.size(); ++i) dst[i] = t[i + 1].getFloatValue();
            continue;
        }
        if (! (juce::CharacterFunctions::isDigit (l[0]) || l[0] == '-' || l[0] == '.')) continue;
        auto t = juce::StringArray::fromTokens (l, " \t", ""); t.removeEmptyStrings();
        if (t.size() < 3) continue;
        for (int i = 0; i < 3; ++i) rgb.push_back ((t[i].getFloatValue() - dmin[i]) / std::max (1e-6f, dmax[i] - dmin[i]));
    }
    if (size < 2 || size > 128) { error = "Missing or invalid LUT_3D_SIZE"; return false; }
    if (rgb.size() != (size_t) size * size * size * 3) { error = "LUT has " + juce::String ((int) rgb.size() / 3) + " entries, expected " + juce::String (size * size * size); return false; }
    if (title.isEmpty()) title = f.getFileNameWithoutExtension();
    return true;
}
}
