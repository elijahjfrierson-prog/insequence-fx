#include "Media.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/display.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#include <algorithm>

#if LIBAVFORMAT_VERSION_MAJOR >= 59
 using ISFXCodecPtr = const AVCodec*;
#else
 using ISFXCodecPtr = AVCodec*;
#endif
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
 #define ISFX_NEW_CHLAYOUT 1
#else
 #define ISFX_NEW_CHLAYOUT 0
#endif

namespace isfx
{
static juce::String avErr (int e) { char b[256] = {}; av_strerror (e, b, sizeof (b)); return b; }

static int streamRotation (AVStream* st)
{
    const uint8_t* sd = nullptr;
#if LIBAVFORMAT_VERSION_MAJOR >= 61
    if (auto* p = av_packet_side_data_get (st->codecpar->coded_side_data, st->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX))
        sd = p->data;
#else
    sd = av_stream_get_side_data (st, AV_PKT_DATA_DISPLAYMATRIX, nullptr);
#endif
    if (sd == nullptr) return 0;
    double r = -av_display_rotation_get ((const int32_t*) sd);
    if (std::isnan (r)) return 0;
    int deg = ((int) std::lround (r) % 360 + 360) % 360;
    return (deg + 45) / 90 * 90 % 360;
}

bool isMediaFile (const juce::File& f)
{
    static const juce::StringArray ext { ".mp4", ".mov", ".m4v", ".mkv", ".avi", ".webm", ".mts", ".mxf",
                                         ".mp3", ".wav", ".flac", ".aif", ".aiff", ".m4a", ".aac", ".ogg",
                                         ".png", ".jpg", ".jpeg" };
    return ext.contains (f.getFileExtension().toLowerCase());
}

ProbeResult probeMedia (const juce::File& f)
{
    ProbeResult r;
    AVFormatContext* fmt = nullptr;
    int e = avformat_open_input (&fmt, f.getFullPathName().toRawUTF8(), nullptr, nullptr);
    if (e < 0) { r.error = "Cannot open: " + avErr (e); return r; }
    if ((e = avformat_find_stream_info (fmt, nullptr)) < 0) { r.error = avErr (e); avformat_close_input (&fmt); return r; }
    int vs = av_find_best_stream (fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int as = av_find_best_stream (fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    r.hasAudio = as >= 0;
    if (vs >= 0 && (fmt->streams[vs]->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0)
    {
        auto* st = fmt->streams[vs];
        r.hasVideo = true;
        r.width = st->codecpar->width; r.height = st->codecpar->height;
        auto fr = av_guess_frame_rate (fmt, st, nullptr);
        r.fps = (fr.num > 0 && fr.den > 0) ? av_q2d (fr) : 30.0;
        r.rotation = streamRotation (st);
        bool still = juce::StringArray { ".png", ".jpg", ".jpeg" }.contains (f.getFileExtension().toLowerCase());
        r.kind = still ? MediaKind::Image : MediaKind::Video;
    }
    else if (r.hasAudio) r.kind = MediaKind::Audio;
    else { r.error = "No audio or video stream"; avformat_close_input (&fmt); return r; }

    if (fmt->duration > 0) r.duration = (Ticks) ((double) fmt->duration / AV_TIME_BASE * (double) kFlicks);
    if (r.kind == MediaKind::Image) r.duration = secToTicks (5.0);
    r.ok = r.duration > 0;
    if (! r.ok) r.error = "Unknown duration";
    avformat_close_input (&fmt);
    return r;
}

bool decodeAudio (const juce::File& f, int rate, int channels, std::vector<float>& out, juce::String& error)
{
    out.clear();
    AVFormatContext* fmt = nullptr;
    int e = avformat_open_input (&fmt, f.getFullPathName().toRawUTF8(), nullptr, nullptr);
    if (e < 0) { error = avErr (e); return false; }
    avformat_find_stream_info (fmt, nullptr);
    ISFXCodecPtr codec = nullptr;
    int si = av_find_best_stream (fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (si < 0 || codec == nullptr) { error = "No audio stream"; avformat_close_input (&fmt); return false; }
    auto* ctx = avcodec_alloc_context3 (codec);
    avcodec_parameters_to_context (ctx, fmt->streams[si]->codecpar);
    if ((e = avcodec_open2 (ctx, codec, nullptr)) < 0) { error = avErr (e); avcodec_free_context (&ctx); avformat_close_input (&fmt); return false; }

    SwrContext* swr = nullptr;
#if ISFX_NEW_CHLAYOUT
    AVChannelLayout outL; av_channel_layout_default (&outL, channels);
    if (ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default (&ctx->ch_layout, ctx->ch_layout.nb_channels);
    swr_alloc_set_opts2 (&swr, &outL, AV_SAMPLE_FMT_FLT, rate, &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
#else
    auto inLayout = ctx->channel_layout ? ctx->channel_layout : (uint64_t) av_get_default_channel_layout (ctx->channels);
    swr = swr_alloc_set_opts (nullptr, av_get_default_channel_layout (channels), AV_SAMPLE_FMT_FLT, rate,
                              (int64_t) inLayout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
#endif
    if (swr == nullptr || swr_init (swr) < 0) { error = "Resampler init failed"; avcodec_free_context (&ctx); avformat_close_input (&fmt); return false; }

    auto* pkt = av_packet_alloc(); auto* fr = av_frame_alloc();
    std::vector<float> buf;
    auto drain = [&]
    {
        while (avcodec_receive_frame (ctx, fr) >= 0)
        {
            int maxOut = (int) av_rescale_rnd (swr_get_delay (swr, ctx->sample_rate) + fr->nb_samples, rate, ctx->sample_rate, AV_ROUND_UP);
            buf.resize ((size_t) (maxOut * channels));
            auto* o = (uint8_t*) buf.data();
            int n = swr_convert (swr, &o, maxOut, (const uint8_t**) fr->extended_data, fr->nb_samples);
            if (n > 0) out.insert (out.end(), buf.begin(), buf.begin() + n * channels);
        }
    };
    while (av_read_frame (fmt, pkt) >= 0)
    {
        if (pkt->stream_index == si && avcodec_send_packet (ctx, pkt) >= 0) drain();
        av_packet_unref (pkt);
    }
    avcodec_send_packet (ctx, nullptr); drain();
    {
        buf.resize ((size_t) (4096 * channels)); auto* o = (uint8_t*) buf.data();
        int n = swr_convert (swr, &o, 4096, nullptr, 0);
        if (n > 0) out.insert (out.end(), buf.begin(), buf.begin() + n * channels);
    }
    av_frame_free (&fr); av_packet_free (&pkt); swr_free (&swr);
    avcodec_free_context (&ctx); avformat_close_input (&fmt);
    if (out.empty()) { error = "Audio stream decoded to nothing"; return false; }
    return true;
}

// ------------------------------------------------------------------ VideoDecoder
VideoDecoder::VideoDecoder() = default;
VideoDecoder::~VideoDecoder() { close(); }

void VideoDecoder::close()
{
    if (sws) { sws_freeContext (sws); sws = nullptr; }
    if (frame) av_frame_free (&frame);
    if (pkt) av_packet_free (&pkt);
    if (dec) avcodec_free_context (&dec);
    if (fmt) avformat_close_input (&fmt);
    cur = {}; next = {}; haveNext = false; eof = false; stream = -1;
}

bool VideoDecoder::open (const juce::File& f, int maxHeight, juce::String& error)
{
    close();
    maxH = maxHeight;
    int e = avformat_open_input (&fmt, f.getFullPathName().toRawUTF8(), nullptr, nullptr);
    if (e < 0) { error = avErr (e); fmt = nullptr; return false; }
    avformat_find_stream_info (fmt, nullptr);
    ISFXCodecPtr codec = nullptr;
    stream = av_find_best_stream (fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (stream < 0 || codec == nullptr) { error = "No video stream"; close(); return false; }
    auto* st = fmt->streams[stream];
    dec = avcodec_alloc_context3 (codec);
    avcodec_parameters_to_context (dec, st->codecpar);
    dec->thread_count = 0;
    dec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if ((e = avcodec_open2 (dec, codec, nullptr)) < 0) { error = avErr (e); close(); return false; }
    tb = av_q2d (st->time_base);
    auto fr = av_guess_frame_rate (fmt, st, nullptr);
    frameRate = (fr.num > 0 && fr.den > 0) ? av_q2d (fr) : 30.0;
    rot = streamRotation (st);
    outW = dec->width; outH = dec->height;
    if (maxH > 0 && outH > maxH) { outW = (int) std::lround ((double) outW * maxH / outH) & ~1; outH = maxH; }
    frame = av_frame_alloc(); pkt = av_packet_alloc();
    return true;
}

bool VideoDecoder::decodeNext (Frame& out)
{
    while (true)
    {
        int r = avcodec_receive_frame (dec, frame);
        if (r >= 0)
        {
            auto ts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
            auto startTs = fmt->streams[stream]->start_time != AV_NOPTS_VALUE ? fmt->streams[stream]->start_time : 0;
            out.pts = secToTicks ((double) (ts - startTs) * tb);
            sws = sws_getCachedContext (sws, frame->width, frame->height, (AVPixelFormat) frame->format,
                                        outW, outH, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            out.w = outW; out.h = outH; out.rgba.resize ((size_t) (outW * outH * 4));
            uint8_t* dst[1] = { out.rgba.data() }; int ls[1] = { outW * 4 };
            sws_scale (sws, frame->data, frame->linesize, 0, frame->height, dst, ls);
            return true;
        }
        if (r == AVERROR_EOF) { eof = true; return false; }
        // need more input
        int rr = av_read_frame (fmt, pkt);
        if (rr < 0) { avcodec_send_packet (dec, nullptr); continue; }
        if (pkt->stream_index == stream) avcodec_send_packet (dec, pkt);
        av_packet_unref (pkt);
    }
}

bool VideoDecoder::seekTo (Ticks t)
{
    auto* st = fmt->streams[stream];
    auto startTs = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    int64_t ts = startTs + (int64_t) (ticksToSec (std::max<Ticks> (0, t)) / tb);
    if (av_seek_frame (fmt, stream, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers (dec);
    eof = false; haveNext = false; cur = {};
    return true;
}

bool VideoDecoder::frameAt (Ticks t, Frame& out)
{
    if (! isOpen()) return false;
    const Ticks frameDur = secToTicks (1.0 / frameRate);
    bool backwards = cur.valid() && t < cur.pts;
    bool farAhead = cur.valid() && t > cur.pts + secToTicks (2.0);
    if (! cur.valid() || backwards || farAhead) seekTo (t);
    while (true)
    {
        if (! haveNext)
        {
            if (eof || ! decodeNext (next)) break;
            haveNext = true;
        }
        if (next.pts <= t + frameDur / 4 || ! cur.valid()) { std::swap (cur, next); haveNext = false; if (cur.pts >= t) break; }
        else break;
    }
    if (! cur.valid()) return false;
    out = cur;
    return true;
}

juce::Image frameToImage (const Frame& f, int rotation)
{
    if (! f.valid()) return {};
    bool swapWH = rotation == 90 || rotation == 270;
    int W = swapWH ? f.h : f.w, H = swapWH ? f.w : f.h;
    juce::Image img (juce::Image::ARGB, W, H, false);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x)
        {
            auto* s = &f.rgba[(size_t) ((y * f.w + x) * 4)];
            int dx = x, dy = y;
            if (rotation == 90) { dx = f.h - 1 - y; dy = x; }
            else if (rotation == 180) { dx = f.w - 1 - x; dy = f.h - 1 - y; }
            else if (rotation == 270) { dx = y; dy = f.w - 1 - x; }
            bd.setPixelColour (dx, dy, juce::Colour (s[0], s[1], s[2], (juce::uint8) 255));
        }
    return img;
}

juce::Image makeThumbnail (const juce::File& f, Ticks t, int height)
{
    VideoDecoder d; juce::String err;
    if (! d.open (f, height, err)) return {};
    Frame fr;
    if (! d.frameAt (t, fr)) return {};
    return frameToImage (fr, d.rotation());
}

Waveform buildWaveform (const std::vector<float>& mono, int rate, double bps)
{
    Waveform w; w.bucketsPerSec = bps;
    size_t per = (size_t) std::max (1.0, rate / bps);
    w.peaks.resize (mono.size() / per + 1, 0.0f);
    for (size_t i = 0; i < mono.size(); ++i)
        w.peaks[i / per] = std::max (w.peaks[i / per], std::abs (mono[i]));
    return w;
}
}
