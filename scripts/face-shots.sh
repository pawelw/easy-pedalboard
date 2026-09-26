#!/usr/bin/env bash
# The website's screenshots of the six products sold, in one command: each face
# rebuilt from source, opened for real with audio running through it, and
# snapshotted with a transparent background, dark and light.
#
#   scripts/face-shots.sh                          # -> build-fast/face-shots/
#   scripts/face-shots.sh ~/Sites/website/img      # anywhere else
#   scripts/face-shots.sh out -- --scale 3         # the rest goes to every tool
#   scripts/face-shots.sh out -- --only tuner      # ...or just some shots
#   SKIP_WEB=1 scripts/face-shots.sh               # jsui/dist already current
#
# What each product shows - preset, pinned knobs, which dialogs and tabs - is
# scripts/face-shots.json. The tool is tests/FaceShots.cpp. Windows open and
# close on screen while it runs (WebKit only paints what is on screen); leave
# them alone for the minute or so it takes.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-build-fast/face-shots}"
shift || true
[[ "${1:-}" == "--" ]] && shift

BUILD=build-fast
PEDALS=(bitbit-alpine bitbit-grain bitbit-artifact bitbit-modulation bitbit-delay bitbit-reverb)
TARGETS=(BitBitAlpine BitBitGrain BitBitArtifact BitBitModulation BitBitDelay BitBitReverb)

if [[ -z "${SKIP_WEB:-}" ]]; then
    for pedal in "${PEDALS[@]}"; do
        echo "==> web face: $pedal"
        log=$(npm run build --prefix "plugins/$pedal/jsui" 2>&1) || { echo "$log"; echo "npm run build FAILED for $pedal"; exit 1; }
    done
fi

# Reuse build-fast, but make sure all six are in it - added to whatever
# EE_PLUGINS it already has, rather than replacing someone's selection.
need_configure=0
if [[ ! -f "$BUILD/build.ninja" ]]; then
    need_configure=1
else
    targets=$(ninja -C "$BUILD" -t targets all 2>/dev/null)
    for t in "${TARGETS[@]}"; do
        grep -q "^ee_face_shots_$t:" <<<"$targets" || need_configure=1
    done
fi

if (( need_configure )); then
    current=$(sed -n 's/^EE_PLUGINS:STRING=//p' "$BUILD/CMakeCache.txt" 2>/dev/null || true)
    if [[ "$current" == "all" ]]; then
        plugins=all
    else
        plugins=$(printf '%s\n' ${current//;/ } "${PEDALS[@]}" | awk 'NF && !seen[$0]++' | paste -sd ';' -)
    fi
    echo "==> configure (EE_PLUGINS=$plugins)"
    cmake --preset fast -DEE_PLUGINS="$plugins" >/dev/null || { echo "configure FAILED"; exit 1; }
fi

echo "==> build"
cmake --build "$BUILD" --target "${TARGETS[@]/#/ee_face_shots_}" >/dev/null || { echo "build FAILED"; exit 1; }

mkdir -p "$OUT"
for t in "${TARGETS[@]}"; do
    echo "==> $t"
    "$BUILD/tests/ee_face_shots_${t}_artefacts/Release/ee_face_shots_$t" --out "$OUT" "$@"
done

echo "==> $(ls "$OUT"/*.png | wc -l | tr -d ' ') PNGs in $OUT"
