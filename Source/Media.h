#pragma once
#include <functional>
#include "Model.h"
#include <juce_graphics/juce_graphics.h>

struct AVFormatContext; struct AVCodecContext; struct AVFrame; struct AVPacket; struct SwsContext;

namespace isfx
{
struct ProbeResult
{
    bool ok = false;
    juce::String error;
    MediaKind kind = MediaKind::Video;
    Ticks duration = 0;
    double fps = 30.0;
    int width = 0, height = 0, rotation = 0;
    bool hasAudio = false, hasVideo = false;
};

ProbeResult probeMedia (const juce::File& f);
bool isMediaFile (const juce::File& f);

// Decodes the whole audio stream, resampled to `rate`, mixed to `channels`, interleaved float.
bool decodeAudio (const juce::File& f, int rate, int channels, std::vector<float>& out, juce::String& error);

// RGBA8 frame (top-down), with the source presentation time.
struct Frame
{
    int w = 0, h = 0;
    std::vector<juce::uint8> rgba;
    Ticks pts = -1;
    bool valid() const { return w > 0 && h > 0; }
};

// Sequential-friendly decoder: seeks only when the requested time jumps backwards or far ahead.
class VideoDecoder
{
public:
    VideoDecoder();
    ~VideoDecoder();
    bool open (const juce::File& f, int maxHeight, juce::String& error);  // maxHeight 0 = native
    void close();
    bool isOpen() const { return fmt != nullptr; }
    // returns the frame showing at source time t (the last frame with pts <= t)
    bool frameAt (Ticks t, Frame& out);
    const Frame* frameRef (Ticks t);  // valid until the next call
    bool readNext (Frame& out) { return isOpen() && ! eof && decodeNext (out); }
    int width() const { return outW; }
    int height() const { return outH; }
    int rotation() const { return rot; }
    double fps() const { return frameRate; }
private:
    bool decodeNext (Frame& out);
    bool seekTo (Ticks t);
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws = nullptr;
    int stream = -1, outW = 0, outH = 0, rot = 0, maxH = 0;
    double frameRate = 30.0, tb = 0;
    Frame cur, next;
    bool eof = false, haveNext = false;
};

juce::Image frameToImage (const Frame& f, int rotation);
// Intra-only MJPEG edit proxy (every frame a keyframe: instant seeks, light decode) for smooth preview playback.
juce::File proxyFileFor (const juce::File& source, int height);
bool makeProxy (const juce::File& source, const juce::File& dest, int height, double durationSec,
                const std::function<bool (double)>& progress, juce::String& error);
juce::Image makeThumbnail (const juce::File& f, Ticks t, int height);

// Peak overview for waveform drawing (min/max pairs per bucket).
struct Waveform
{
    std::vector<float> peaks;  // abs peak per bucket
    double bucketsPerSec = 200.0;
};
Waveform buildWaveform (const std::vector<float>& mono, int rate, double bucketsPerSec = 200.0);
}
