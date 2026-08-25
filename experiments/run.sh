#!/usr/bin/env bash
#
# Run a BIO-FMI experiment through the xbench harness.
#
# This repo holds only configuration: the specs in experiments/specs/ and their
# hooks. The engine lives in its own project so other tools can use it, and is
# located here rather than vendored.
#
# Usage:
#   ./experiments/run.sh                       # list the available specs
#   ./experiments/run.sh merge_mode            # run one
#   ./experiments/run.sh merge_mode --dry-run  # show the resolved plan, do nothing
#   ./experiments/run.sh merge_mode --tag pilot --work-dir /dev/shm
#
# Any further arguments are passed through to `xbench run` unchanged
# (--dataset, --tool, --stage, --jobs, --reps, --mem-cap, --mem-budget,
#  --timeout, --clean-work, --no-gzip, --quiet, …).
#
# Env:
#   XBENCH_HOME   where the harness lives; overrides the search below.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SPEC_DIR="$REPO/experiments/specs"

# ---- locate the harness -------------------------------------------------------
# An installed `xbench` on PATH is fine here: unlike the measured binaries, the
# harness is not the thing under test, and it stamps its own version into every
# manifest.
find_xbench() {
    if [[ -n "${XBENCH_HOME:-}" ]]; then
        if [[ -x "$XBENCH_HOME/bin/xbench" ]]; then echo "$XBENCH_HOME/bin/xbench"; return 0; fi
        echo "XBENCH_HOME=$XBENCH_HOME has no bin/xbench" >&2; return 1
    fi
    for c in "$REPO/../xbench/bin/xbench" "$HOME/Documents/uni_projects/xbench/bin/xbench"; do
        [[ -x "$c" ]] && { readlink -f "$c"; return 0; }
    done
    command -v xbench 2>/dev/null && return 0
    return 1
}

if ! XBENCH="$(find_xbench)"; then
    cat >&2 <<EOF
xbench not found.

  Expected at ~/Documents/uni_projects/xbench, or on PATH, or named by
  XBENCH_HOME. It is a separate project so that other tools can use it:

      XBENCH_HOME=/path/to/xbench ./experiments/run.sh $*
EOF
    exit 1
fi

# ---- pick the spec ------------------------------------------------------------
list_specs() {
    echo "Available specs in experiments/specs/:"
    shopt -s nullglob
    local found=0
    for s in "$SPEC_DIR"/*.yaml; do
        found=1
        printf '  %-16s %s\n' "$(basename "$s" .yaml)" \
            "$(awk -F': ' '/^description:/{getline; sub(/^ +/,""); print; exit}' "$s")"
    done
    (( found )) || echo "  (none)"
}

if [[ $# -eq 0 ]]; then
    list_specs
    echo
    echo "Usage: ./experiments/run.sh <spec> [xbench run options...]"
    echo "Harness: $XBENCH ($("$XBENCH" --version))"
    exit 0
fi

NAME="$1"; shift
SPEC="$SPEC_DIR/$NAME.yaml"
[[ -f "$SPEC" ]] || SPEC="$NAME"          # also accept a path
if [[ ! -f "$SPEC" ]]; then
    echo "No such spec: $NAME" >&2
    echo >&2
    list_specs >&2
    exit 1
fi

# ---- go -----------------------------------------------------------------------
# --root pins relative paths in the spec to this repo regardless of the cwd the
# script was invoked from; runs land in experiments/runs/<experiment>/<timestamp>.
exec "$XBENCH" run "$SPEC" \
    --root "$REPO" \
    --runs-dir "$REPO/experiments/runs" \
    "$@"
