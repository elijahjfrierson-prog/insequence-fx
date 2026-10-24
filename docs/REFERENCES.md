# Quality-bar references (from Elijah)

Measured with ffmpeg scene detection (threshold 0.25) + signalstats on 360p copies. Used to tune looks + the generator.
These are targets for quality, not things to copy.

| Ref | Cuts / dur | Avg shot | Y low/avg/high | Sat | Character |
|---|---|---|---|---|---|
| Drocetti – can't talk (shot @Visualbyjacob, vfx @salosix6) | 110 / 145 s | 1.3 s | 31 / 58 / 94 | 11 | night street, teal/cyan cast, wide/fisheye close-ups, 1–3 frame flash bursts, ~47 solarize/negative flip frames, push-ins |
| Drocetti – club classic (dir. oxy.cotten) | 64 / 113 s | 1.8 s | 20 / 61 / 111 | 8 | crushed blacks, warm brick red, heavy vignette, red-mono fire isolation, clean hard cuts, no flashes |
| Travis Scott – BUTTERFLY EFFECT | 226 / 213 s | 0.94 s | 34 / 102 / 168 | 21 | bright sky-blue + gold, chrome highlights, motion-smear/whip, bokeh + light leaks, B&W isolation shots, framed/letterbox, hand-drawn text |

Looks built from these: "Night Teal Flip", "Warm Crush", "Sky Chrome Haze" (Looks.cpp), plus the generator
rhythm presets in Editing.cpp. Reference Match (Reference.cpp) measures any reference video the same way in-app.

## Product additions requested mid-build
- Full music videos up to 10 minutes (kMaxSongSeconds = 600).
- All aspect ratios (16:9, 9:16, 1:1, 4:5, 4:3, 3:4, 2:1, 1.85:1, 2.39:1, 21:9, custom) with fit/fill/blur-fill.
- Overlay compositing: blend modes, opacity, chroma/luma key, feathered masks, transform; procedural light leak,
  film burn, dust/scratches, lens flare, bokeh generators.
- Background cutout (local segmentation; Apache-licensed model) + acid FX: solarize/invert, hue cycle, posterize,
  edge glow, thermal, kaleidoscope, liquid displace, feedback trails, chroma smear, strobe.
- Reference Match: measure a reference video's cut rhythm + grade + FX -> Visual DNA -> "Edit like this".
