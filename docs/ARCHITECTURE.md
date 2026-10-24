# InSequence FX — architecture

Native JUCE 8 app (C++17). FFmpeg (libav*) for decode/encode, OpenGL (GLSL via JUCE translate*ToV3) for every
visual effect. Preview and export render through the same `Renderer`, so the export matches the preview.

## Time
All timeline times are int64 **flicks** (705,600,000 per second, exact for 23.976/24/25/29.97/30/59.94/60 fps and
48 kHz). Musical grid times are computed as `offset + n * beatLen / subdiv` in double and rounded once
(no accumulated rounding). A cut keeps its ideal musical time; the renderer quantizes per output frame.

## Modules
- `Model`     Project / Sequence / Track / Clip / MediaItem / FxStack, JSON (.isfx) save/load, snapshot undo.
- `Media`     probe, audio decode to float, VideoDecoder (seek + forward decode, rotation, VFR pts),
              proxy files (.isp: indexed JPEG frames + real pts) for instant scrubbing, thumbnails.
- `Analysis`  STFT spectral flux, tempo (autocorr + fine phase fit), downbeat, kick/snare/hat onsets,
              per-bar energy sections, audio sync (onset-envelope FFT cross-correlation + sample refine, confidence).
- `Editing`   split/delete/ripple/paint/move, CHOP (1/8 1/16 1/32 grid, kick/snare/hat events, 16-step pattern),
              sync-preserving angle switching, Smart Replace, GENERATE 3 EDITS (deterministic seed).
- `Looks`     FX parameter definitions, style modes (Classic/Gothic/VHS/Y2K/Underground/Abstract), look library, .cube LUTs.
- `AudioMix`  sample-accurate mix of audio clips (shared by playback and export).
- `Renderer`  GL: per-clip pass (transform, kick zoom, shake, clip grade, transitions) -> bloom/halation pyramid ->
              final pass (grade, LUT, VHS, glitch, RGB split, grain, vignette, snare flash).
- `Exporter`  FBO readback -> swscale -> H.264 (VideoToolbox / MediaFoundation / x264 / mpeg4 fallback) + AAC, MP4.
- UI          MainComponent (toolbar, panels, preview, inspector), Timeline (FL tools E/P/D/C/M/T), Panels.
