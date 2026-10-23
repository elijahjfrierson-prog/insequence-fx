#!/usr/bin/env bash
# Builds an LGPL FFmpeg (no GPL or external libs, VideoToolbox hardware codecs) into $1, Apple Silicon, macOS 11+.
set -euo pipefail
PREFIX="${1:?prefix}"; VER="${FFMPEG_VERSION:-7.1}"
[[ -f "$PREFIX/lib/libavcodec.dylib" ]] && { echo "cached FFmpeg in $PREFIX"; exit 0; }
WORK="$(mktemp -d)"; cd "$WORK"
curl -fsSL "https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz" -o ff.tar.xz
tar xf ff.tar.xz && cd "ffmpeg-$VER"
./configure --prefix="$PREFIX" --enable-shared --disable-static --disable-programs --disable-doc \
  --disable-autodetect --enable-videotoolbox --enable-audiotoolbox --enable-zlib --enable-pthreads \
  --disable-avdevice --disable-avfilter --disable-postproc --disable-network \
  --install-name-dir='@rpath' --extra-cflags="-mmacosx-version-min=11.0" --extra-ldflags="-mmacosx-version-min=11.0"
make -j"$(sysctl -n hw.ncpu)" >/dev/null
make install >/dev/null
ls "$PREFIX/lib"
