#!/usr/bin/env bash
# Builds InSequence-FX-macOS.pkg: guided installer that shows the EULA (must be accepted) and installs
# "InSequence FX.app" (with its FFmpeg libraries in Contents/Frameworks) into /Applications.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
VERSION="${VERSION:-1.0.0}"
BUNDLE_ID="com.humhouse.insequencefx.installer"
PKG_OUT="${PKG_OUT:-InSequence-FX-macOS.pkg}"
NAME="InSequence FX"
FFMPEG_DIR="${FFMPEG_DIR:?FFMPEG_DIR}"
APP_SRC="$REPO/build/InSequenceFX_artefacts/Release/$NAME.app"
[[ -d "$APP_SRC" ]] || { echo "ERROR: $APP_SRC not found" >&2; exit 1; }

STAGE="$(mktemp -d)"
ROOT="$STAGE/root/Applications"
mkdir -p "$ROOT" "$STAGE/pkgs" "$STAGE/scripts" "$STAGE/resources"
cp -R "$APP_SRC" "$ROOT/"
APP="$ROOT/$NAME.app"
mkdir -p "$APP/Contents/Frameworks"
for f in "$FFMPEG_DIR"/lib/lib{avformat,avcodec,avutil,swscale,swresample}*.dylib; do cp -a "$f" "$APP/Contents/Frameworks/"; done
cp "$REPO/installer/LICENSE.txt" "$APP/Contents/Resources/LICENSE.txt"
# every non-system dependency must resolve inside the bundle
if otool -L "$APP/Contents/MacOS/$NAME" "$APP"/Contents/Frameworks/*.dylib | grep -E "/opt/homebrew|/usr/local|$FFMPEG_DIR"; then
  echo "ERROR: bundle links outside itself" >&2; exit 1; fi
codesign --force --deep --sign "${APPLE_DEVELOPER_ID:--}" "$APP" || true

cat > "$STAGE/scripts/preinstall" <<PRE
#!/bin/sh
pkill -x "$NAME" 2>/dev/null || true
sleep 1
rm -rf "/Applications/$NAME.app" 2>/dev/null || true
exit 0
PRE
chmod +x "$STAGE/scripts/preinstall"

pkgbuild --analyze --root "$STAGE/root" "$STAGE/component.plist"
plutil -replace BundleIsRelocatable -bool NO "$STAGE/component.plist"
pkgbuild --identifier "${BUNDLE_ID}.app" --version "$VERSION" --root "$STAGE/root" \
  --component-plist "$STAGE/component.plist" \
  --scripts "$STAGE/scripts" --install-location "/" "$STAGE/pkgs/app.pkg"

cat > "$STAGE/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$NAME $VERSION</title>
    <welcome    file="welcome.html"    mime-type="text/html"/>
    <license    file="license.txt"     mime-type="text/plain"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64"/>
    <choices-outline><line choice="app"/></choices-outline>
    <choice id="app" title="$NAME" visible="false" start_selected="true"><pkg-ref id="${BUNDLE_ID}.app"/></choice>
    <pkg-ref id="${BUNDLE_ID}.app" version="$VERSION" onConclusion="none">app.pkg</pkg-ref>
</installer-gui-script>
XML

cp "$REPO/installer/LICENSE.txt" "$STAGE/resources/license.txt"
cat > "$STAGE/resources/welcome.html" <<'HTML'
<html><body style="font-family: -apple-system, system-ui; padding: 20px;">
<h2 style="margin-top:0;">Welcome to InSequence FX</h2>
<p>Drop your song and the takes you filmed to it. InSequence FX syncs every take to the music, cuts on the beat
(1/8, 1/16, 1/32, kicks, snares, hi-hats), generates three editable music videos, and grades them with
film grain, halation, LUTs and analog/digital FX on the GPU.</p>
<p>InSequence FX will be installed in /Applications. Requires an Apple Silicon Mac.</p>
</body></html>
HTML
cat > "$STAGE/resources/conclusion.html" <<'HTML'
<html><body style="font-family: -apple-system, system-ui; padding: 20px;">
<h2 style="margin-top:0;">Installation complete.</h2>
<p>Open <b>InSequence FX</b> from Applications. First time: right-click the app &rarr; Open.</p>
</body></html>
HTML

UNSIGNED="$STAGE/unsigned.pkg"
productbuild --distribution "$STAGE/distribution.xml" --resources "$STAGE/resources" \
  --package-path "$STAGE/pkgs" --version "$VERSION" "$UNSIGNED"
if [[ -n "${APPLE_INSTALLER_ID:-}" ]]; then productsign --sign "$APPLE_INSTALLER_ID" "$UNSIGNED" "$PKG_OUT"; else cp "$UNSIGNED" "$PKG_OUT"; fi
ls -la "$PKG_OUT"
