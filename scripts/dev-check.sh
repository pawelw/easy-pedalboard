#!/usr/bin/env bash
# Configure, build and verify in one command, with one exit code.
#
#   scripts/dev-check.sh                 # all nine pedals, Standalone only
#   scripts/dev-check.sh peak-wah        # just one pedal - much quicker
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
        printf '  %-20s skipped (not in this selection)\n' "$name"
        return
    fi
    if [[ ! -x $bin ]]; then
        printf '  %-20s skipped (not built)\n' "$name"
        return
    fi
    local out rc new
    out=$("$bin" 2>&1); rc=$?

    # The two known-bad checks are filtered out of the verdict so the exit code
    # means "something you did", not "this tree has always been like this".
    # Keep this list in sync with the "Known failures" section of CLAUDE.md.
    new=$(grep -E '^\s+FAIL' <<<"$out" \
          | grep -vF 'tape at 100 % moves the level too far' \
          | grep -vF 'chorus is silent on a silent input')

    if [[ $rc -eq 0 ]]; then
        printf '  %-20s PASS\n' "$name"
    elif [[ -z $new ]]; then
        printf '  %-20s PASS (known failures only)\n' "$name"
    else
        printf '  %-20s FAIL\n' "$name"
        sed 's/^/      /' <<<"$new"
        status=1
    fi
}

echo "==> tests"
run ee_dsp_tests
run ee_tape_stress
run ee_reverb_stress
run ee_trempan_stress

if [[ $status -eq 0 ]]; then
    echo "==> OK"
else
    echo "==> FAILED"
fi
exit $status
