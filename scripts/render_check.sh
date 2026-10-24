#!/usr/bin/env bash
# Linux GUI check (run under xvfb-run): the real app opens its window, builds a demo project from
# generated media, renders preview frames through the same renderer as export, exports an MP4, exits 0.
set -euo pipefail
BUILD="${1:-build}"
APP="$BUILD/InSequenceFX_artefacts/Release/InSequence FX"
OUT="${2:-$BUILD/selftest}"
mkdir -p "$OUT"
timeout 300 "$APP" --selftest "$OUT"
test -s "$OUT/selftest.mp4"
test -s "$OUT/selftest_window.png"
ffprobe -v error -show_entries stream=codec_type,width,height -of csv=p=0 "$OUT/selftest.mp4"
echo "render check OK"
