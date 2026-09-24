#!/usr/bin/env bash
# The frozen regression checksums (release-plan.md G1b): run the deterministic
# host/regress tools and diff what they print against tests/baselines/<platform>/.
#
#   scripts/regress-baselines.sh              # diff; exit 1 if anything moved
#   scripts/regress-baselines.sh --update     # rewrite the baselines from this build
#   scripts/regress-baselines.sh --update ee_delay_regress   # ...for one tool
#
# Sample-exact is the bar (see RegressHarness.h): a change meant to change nothing
# must leave every line identical, and a change meant to change the sound should
# move exactly the lines it means to - so a moved baseline goes in the same commit
# as the change, with the reason, exactly like tests/golden.
#
# One directory per platform and architecture, because a different compiler or
# library maths moves the checksums (release-plan.md 2.1): tests/baselines/Darwin-arm64
# today, a Windows one when that build exists. A platform with no directory is
# skipped, not failed.
#
# The baselines are taken from the `fast` preset's build (build-fast), the one
# scripts/dev-check.sh builds; set BUILD to point at another tree. Only tools that
# print the same thing on every run are listed - each was run twice and compared
# before it went on this list. A tool that is not built in the current EE_PLUGINS
# selection is skipped.
set -uo pipefail
cd "$(dirname "$0")/.."

BUILD="${BUILD:-build-fast}"
PLATFORM="$(uname -s)-$(uname -m)"
DIR="tests/baselines/$PLATFORM"

TOOLS=(
    ee_delay_regress
    ee_spring_regress
    ee_alpine_host
    ee_modulation_host
    ee_reverb_host
    ee_grain_host
    ee_module_stress
    ee_bit_check
    ee_reverb_stress
)

update=0
if [[ "${1:-}" == "--update" ]]; then
    update=1
    shift
fi
[[ $# -gt 0 ]] && TOOLS=("$@")

if [[ $update -eq 0 && ! -d $DIR ]]; then
    echo "  no baselines for $PLATFORM ($DIR) - skipped"
    exit 0
fi

mkdir -p "$DIR"
status=0

for tool in "${TOOLS[@]}"; do
    bin="$BUILD/tests/${tool}_artefacts/Release/$tool"
    base="$DIR/$tool.txt"

    if [[ ! -x $bin ]]; then
        printf '  %-24s skipped (not built)\n' "$tool"
        continue
    fi

    # Run from a scratch directory: a tool that writes a file when it is given
    # none must not drop it in the source tree.
    out=$(cd "$(mktemp -d)" && "$OLDPWD/$bin" 2>&1)
    rc=$?

    if [[ $rc -ne 0 ]]; then
        printf '  %-24s FAIL (exit %d) - not comparable\n' "$tool" "$rc"
        status=1
        continue
    fi

    if [[ $update -eq 1 ]]; then
        printf '%s\n' "$out" >"$base"
        printf '  %-24s written  (%d lines)\n' "$tool" "$(wc -l <"$base")"
    elif [[ ! -f $base ]]; then
        printf '  %-24s no baseline (run with --update)\n' "$tool"
        status=1
    elif diff -u "$base" <(printf '%s\n' "$out") >/tmp/regress-diff.$$; then
        printf '  %-24s identical\n' "$tool"
    else
        printf '  %-24s MOVED\n' "$tool"
        sed 's/^/      /' /tmp/regress-diff.$$ | head -30
        status=1
    fi
done

rm -f /tmp/regress-diff.$$
exit $status
