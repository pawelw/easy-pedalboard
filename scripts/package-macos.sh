#!/usr/bin/env bash
# Builds universal binaries and packages them for another Mac.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build-universal"
DIST="$ROOT/dist"
STAGE="$DIST/EasyEffects"

cmake -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DEE_UNIVERSAL_BINARY=ON \
    -DEE_BUILD_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/install"

cmake --build "$BUILD_DIR"

rm -rf "$STAGE"
mkdir -p "$STAGE"

ARTEFACTS="$BUILD_DIR/plugins/easy-reverb/EasyReverb_artefacts/Release"
for item in "$ARTEFACTS/VST3/Easy Reverb.vst3" \
            "$ARTEFACTS/AU/Easy Reverb.component" \
            "$ARTEFACTS/Standalone/Easy Reverb.app"; do
    [[ -e "$item" ]] && cp -R "$item" "$STAGE/"
done

# Ad-hoc signing is all that is possible without a paid Developer ID. It is not
# notarised, so the receiving Mac still has to clear the quarantine flag.
while IFS= read -r bundle; do
    codesign --force --sign - --timestamp=none "$bundle"
    echo "signed: $(basename "$bundle")"
done < <(find "$STAGE" -maxdepth 1 -mindepth 1)

cat > "$STAGE/INSTALL.txt" <<'EOF'
Easy Effects - install on macOS
===============================

1. Copy the bundles into place:

     Easy Reverb.vst3       ->  ~/Library/Audio/Plug-Ins/VST3/
     Easy Reverb.component  ->  ~/Library/Audio/Plug-Ins/Components/
     Easy Reverb.app        ->  anywhere (optional, for testing without a DAW)

2. These builds are ad-hoc signed, not notarised, so macOS will refuse to load
   them until the quarantine flag is cleared. In Terminal:

     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Easy Reverb.vst3"
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Easy Reverb.component"

   Without this you get "Apple could not verify ... is free of malware".

3. Rescan plugins in your DAW.
     Ableton Live: Preferences -> Plug-Ins -> Rescan

Universal binaries: run on both Apple Silicon and Intel.
EOF

ZIP="$DIST/EasyEffects-macOS.zip"
rm -f "$ZIP"
(cd "$DIST" && zip -qr "$(basename "$ZIP")" "EasyEffects")

echo
echo "Architectures:"
lipo -archs "$STAGE/Easy Reverb.vst3/Contents/MacOS/Easy Reverb"
echo
echo "Package: $ZIP"
