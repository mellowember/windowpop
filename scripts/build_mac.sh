#!/bin/bash
# Builds WindowPop.plugin for After Effects on macOS (Apple Silicon + Intel).
#
#   ./scripts/build_mac.sh <path-to-AE-SDK-folder> [--install]
#
# Needs only the Xcode Command Line Tools (xcode-select --install).
# --install copies the plugin to the shared Adobe plug-ins folder (asks for your password).
set -euo pipefail

SDK_IN="${1:-}"
if [ -z "$SDK_IN" ] || [ ! -d "$SDK_IN" ]; then
  echo "Usage: $0 <path-to-After-Effects-SDK-folder> [--install]"
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/mac"
NAME=WindowPop

# Find the SDK's Headers folder wherever it sits inside the unzipped download.
EFFECT_H="$(find "$SDK_IN" -name AE_Effect.h -path '*Headers*' | head -n 1)"
if [ -z "$EFFECT_H" ]; then
  echo "Couldn't find AE_Effect.h under $SDK_IN. Point this at the unzipped After Effects SDK."
  exit 1
fi
HEADERS="$(dirname "$EFFECT_H")"
EXAMPLES="$(dirname "$HEADERS")"
GENERAL_R="$(find "$EXAMPLES" -name AE_General.r | head -n 1)"
echo "Using SDK headers: $HEADERS"

INCLUDES=(-I"$HEADERS" -I"$HEADERS/SP" -I"$HEADERS/adobesdk" -I"$EXAMPLES/Util" -I"$ROOT/src/core")

rm -rf "$OUT/$NAME.plugin"
mkdir -p "$OUT/$NAME.plugin/Contents/MacOS" "$OUT/$NAME.plugin/Contents/Resources"

echo "Compiling..."
xcrun clang++ -std=c++17 -O2 -arch arm64 -arch x86_64 -mmacosx-version-min=11.0 \
  -bundle -fvisibility=hidden -undefined dynamic_lookup \
  "${INCLUDES[@]}" \
  "$ROOT/src/ae/WindowPopAE.cpp" "$ROOT/src/core/WindowPop.cpp" \
  -o "$OUT/$NAME.plugin/Contents/MacOS/$NAME"

echo "Compiling PiPL resource..."
REZ_INCLUDES=(-i "$HEADERS" -i "$HEADERS/SP" -i "$HEADERS/adobesdk")
[ -n "$GENERAL_R" ] && REZ_INCLUDES+=(-i "$(dirname "$GENERAL_R")")
xcrun Rez -useDF -d AE_OS_MAC -d __MACH__ "${REZ_INCLUDES[@]}" \
  -isysroot "$(xcrun --show-sdk-path)" \
  -o "$OUT/$NAME.plugin/Contents/Resources/$NAME.rsrc" \
  "$ROOT/src/ae/WindowPopPiPL.r"

cat > "$OUT/$NAME.plugin/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>com.windowpop.$NAME</string>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundlePackageType</key><string>eFKT</string>
  <key>CFBundleSignature</key><string>FXTC</string>
  <key>CFBundleShortVersionString</key><string>0.1.0</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
</dict>
</plist>
PLIST
printf 'eFKTFXTC' > "$OUT/$NAME.plugin/Contents/PkgInfo"

# Ad-hoc signature so Apple Silicon will load it.
codesign --force --deep -s - "$OUT/$NAME.plugin"
echo "Built $OUT/$NAME.plugin"

if [ "${2:-}" = "--install" ]; then
  DEST="/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
  echo "Installing to $DEST (you may be asked for your Mac password)..."
  sudo mkdir -p "$DEST"
  sudo rm -rf "$DEST/$NAME.plugin"
  sudo cp -R "$OUT/$NAME.plugin" "$DEST/"
  echo "Installed. Restart After Effects, then look under Effect > Stylize > WindowPop."
fi
