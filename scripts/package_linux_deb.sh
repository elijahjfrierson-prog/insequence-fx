#!/usr/bin/env bash
# Builds InSequence-FX-Linux.deb (Ubuntu 22.04+/Debian; uses the system FFmpeg libraries)
set -euo pipefail
BUILD="${1:-build}"; OUT="${2:-artifacts}"
VERSION="${VERSION:-1.0.0}"; app="InSequence FX"; pkg=insequence-fx
mkdir -p "$OUT"
root="$(mktemp -d)"; chmod 755 "$root"
mkdir -p "$root/DEBIAN" "$root/opt/$pkg" "$root/usr/bin" "$root/usr/share/applications" "$root/usr/share/doc/$pkg"
cp "$BUILD/InSequenceFX_artefacts/Release/$app" "$root/opt/$pkg/$app"
chmod 755 "$root/opt/$pkg/$app"
cp installer/LICENSE.txt "$root/opt/$pkg/LICENSE.txt"
ln -s "/opt/$pkg/$app" "$root/usr/bin/$pkg"
cp installer/LICENSE.txt "$root/usr/share/doc/$pkg/copyright"
cat > "$root/usr/share/applications/$pkg.desktop" <<EOF2
[Desktop Entry]
Type=Application
Name=$app
Comment=Beat-locked music video editor
Exec="/opt/$pkg/$app"
Terminal=false
Categories=AudioVideo;Video;
EOF2
deps="libasound2, libfreetype6, libfontconfig1, libx11-6, libxext6, libxinerama1, libxrandr2, libxcursor1, libgl1"
for l in libavformat libavcodec libavutil libswscale libswresample; do
  so=$(ldconfig -p | grep -o "$l\.so\.[0-9]*" | head -1); n=${so##*.so.}; deps="$deps, ${l}${n}"; done
cat > "$root/DEBIAN/control" <<EOF2
Package: $pkg
Version: $VERSION
Section: video
Priority: optional
Architecture: amd64
Depends: $deps
Maintainer: Hum House <support@humhouse.studio>
Description: $app - beat-locked music video editor
EOF2
dpkg-deb --root-owner-group --build "$root" "$OUT/InSequence-FX-Linux.deb"
rm -rf "$root"
ls -la "$OUT"
