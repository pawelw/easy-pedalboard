#!/usr/bin/env bash
# Configure, build and verify in one command, with one exit code.
#
#   scripts/dev-check.sh                 # all eleven pedals, Standalone only
#   scripts/dev-check.sh bitbit-wah        # just one pedal - much quicker
#
# Uses the "fast" preset: no VST3/AU, no LTO, nothing installed into ~/Library.
# For a build you actually want to load in a host, use `cmake --preset dev`.
set -uo pipefail
cd "$(dirname "$0")/.."

PLUGINS="${1:-all}"
BUILD=build-fast

echo "==> configure (EE_PLUGINS=$PLUGINS)"
cmake --preset fast -DEE_PLUGINS="$PLUGINS" >/dev/null || { echo "configure FAILED"; exit 1; }

echo "==> build"
cmake --build "$BUILD" >/dev/null || { echo "build FAILED"; exit 1; }

status=0

# A test binary left over from a previous EE_PLUGINS selection stays on disk and
# would otherwise be run - and reported as a pass - without having been rebuilt.
# Ask ninja what this configuration actually contains rather than trusting the
# file system.
targets=$(ninja -C "$BUILD" -t targets all 2>/dev/null)

run() {
    local name=$1 bin="$BUILD/tests/$1_artefacts/Release/$1"
    if ! grep -q "/$name\b" <<<"$targets"; then
        printf '  %-30s skipped (not in this selection)\n' "$name"
        return
    fi
    if [[ ! -x $bin ]]; then
        printf '  %-30s skipped (not built)\n' "$name"
        return
    fi
    local out rc new
    out=$("$bin" 2>&1); rc=$?

    # There are no known-bad checks any more (the last two were closed under
    # G1.4 of docs/release-plan.md), so any FAIL line is yours. If one ever has
    # to be waived again, filter it here and list it in CLAUDE.md, with why.
    new=$(grep -E '^\s+FAIL' <<<"$out")

    if [[ $rc -eq 0 ]]; then
        printf '  %-30s PASS\n' "$name"
    else
        printf '  %-30s FAIL (exit %d)\n' "$name" "$rc"
        # A crash has no FAIL line to show, so fall back to the last of the output.
        sed 's/^/      /' <<<"${new:-$(tail -n 5 <<<"$out")}"
        status=1
    fi
}

echo "==> tests"
run ee_dsp_tests
run ee_preset_tests
run ee_tape_stress
run ee_reverb_stress
run ee_trempan_stress
run ee_wah_stress
run ee_grain_stress
run ee_modulation_host
run ee_reverb_host

# The frozen parameter contract for the six products that are sold. A failure
# here is not a bug - it is a parameter id, range, default or engine order
# moving, which breaks every saved session and preset keyed on it. If the change
# is deliberate, regenerate and say so in the commit:
#
#   for t in BitBitAlpine BitBitGrain BitBitArtifact BitBitModulation BitBitDelay BitBitReverb; do
#       "$BUILD/tests/ee_param_golden_${t}_artefacts/Release/ee_param_golden_$t" --update
#   done
for product in BitBitAlpine BitBitGrain BitBitArtifact BitBitModulation BitBitDelay BitBitReverb; do
    run "ee_param_golden_$product"
done

if [[ $status -eq 0 ]]; then
    echo "==> OK"
else
    echo "==> FAILED"
fi
exit $status
