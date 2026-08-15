#!/usr/bin/env bash
#
# LINEAR vs CARTESIAN l-EDS: build cost, index cost, query cost, across an l sweep.
#
# The two merge modes are the same transform with and without source information.
# CARTESIAN keeps every combination of adjacent alternatives; LINEAR keeps only
# those some path actually carries. The comparison measures what phasing buys —
# in space, in build feasibility, and in query precision.
#
# Usage:
#   run_merge_mode_experiment.sh <base.eds> <base.seds> <name> [out_dir]
#
# Knobs (env): L_VALUES PATTERN_LEN N_PATTERNS SEED MEM_CAP_GB TIMEOUT_S REPS
#
# Every l value must satisfy (l+1) | PATTERN_LEN, because biofmi-locate requires
# |P| to be a multiple of l+1. The default sweep is the divisor set of 120.

set -uo pipefail

BASE_EDS="${1:?usage: $0 <base.eds> <base.seds> <name> [out_dir]}"
BASE_SEDS="${2:?missing .seds}"
NAME="${3:?missing dataset name}"
OUT_DIR="${4:-results/$NAME}"

L_VALUES="${L_VALUES:-3 5 9 11 14 19 29 39 59}"
PATTERN_LEN="${PATTERN_LEN:-120}"
N_PATTERNS="${N_PATTERNS:-200}"
SEED="${SEED:-7}"
MEM_CAP_GB="${MEM_CAP_GB:-8}"
TIMEOUT_S="${TIMEOUT_S:-600}"
REPS="${REPS:-5}"

MEM_CAP_KB=$((MEM_CAP_GB * 1024 * 1024))

# ---- tool resolution: build tree first, never a stale install -----------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
EDSP_TOOLS="$REPO/external/edsparser/build/tools"
BIOFMI_TOOLS="$REPO/build/tools"

pick() {  # pick <name> <preferred_dir>
    [[ -x "$2/$1" ]] && { echo "$2/$1"; return 0; }
    command -v "$1" 2>/dev/null && return 0
    echo "ERROR: $1 not found (looked in $2 and PATH)" >&2
    return 1
}
EDS2LEDS=$(pick eds2leds "$EDSP_TOOLS") || exit 1
STATS=$(pick edsparser-stats "$EDSP_TOOLS") || exit 1
GENPAT=$(pick edsparser-genpatterns "$EDSP_TOOLS") || exit 1
BUILD=$(pick biofmi-build "$BIOFMI_TOOLS") || exit 1
LOCATE=$(pick biofmi-locate "$BIOFMI_TOOLS") || exit 1

# Refuse to measure anything with a binary that cannot say what it is. A stale
# eds2leds silently emits l-EDS containing strings no genome carries.
STAMP=$("$EDS2LEDS" --version 2>/dev/null)
COMMIT=$(awk -F= '/^COMMIT=/{print $2}' <<<"$STAMP")
COMMIT_DATE=$(awk -F= '/^COMMIT_DATE=/{print $2}' <<<"$STAMP")
DIRTY=$(awk -F= '/^DIRTY=/{print $2}' <<<"$STAMP")
if [[ -z "$COMMIT_DATE" || "$COMMIT_DATE" == "unknown" ]]; then
    echo "ERROR: eds2leds reports no build provenance — rebuild before measuring" >&2
    exit 1
fi
if [[ "$COMMIT_DATE" < "2026-08-04T14:16:54Z" ]]; then
    echo "ERROR: eds2leds ($COMMIT, $COMMIT_DATE) predates the complement fix." >&2
    exit 1
fi
[[ "$DIRTY" == "1" ]] && echo "WARNING: eds2leds built from a dirty tree — results not reproducible from $COMMIT alone" >&2

mkdir -p "$OUT_DIR"/{leds,index,patterns,logs}
CSV="$OUT_DIR/results.csv"
QCSV="$OUT_DIR/queries.csv"

# ---- helpers -----------------------------------------------------------------

# Run a command under a hard address-space cap and a timeout, capturing peak RSS
# and wall time without a pipeline (a pipe would mask the command's exit code).
TIMEFILE=$(mktemp); trap 'rm -f "$TIMEFILE"' EXIT
run_capped() {  # run_capped <logfile> <cmd...>  -> sets RC, PEAK_KB, SECS
    local log="$1"; shift
    ( ulimit -v "$MEM_CAP_KB"; exec /usr/bin/time -f "%M %e" -o "$TIMEFILE" \
        timeout "$TIMEOUT_S" "$@" ) >"$log" 2>&1
    RC=$?
    PEAK_KB=$(awk 'NF==2{print $1}' "$TIMEFILE" 2>/dev/null | tail -1)
    SECS=$(awk 'NF==2{print $2}' "$TIMEFILE" 2>/dev/null | tail -1)
    PEAK_KB=${PEAK_KB:-0}; SECS=${SECS:-0}
}

fsize() { [[ -f "$1" ]] && stat -c%s "$1" || echo 0; }

# biofmi-build writes a *directory* holding the index components
# (.ri .ci .loc .iloc .tloc .abp .ss .aof .meta), so the index size is the sum
# over that directory, not one file.
index_bytes() {
    [[ -d "$1" ]] || { echo 0; return; }
    du -sb "$1" 2>/dev/null | awk '{print $1}'
}

# Per-component breakdown, so the reference vs changes split is visible.
index_component() {  # index_component <dir> <ext>
    local f="$1/index.$2"
    [[ -f "$f" ]] && stat -c%s "$f" || echo 0
}

# ---- base EDS characterisation ----------------------------------------------
echo "=== $NAME: characterising base EDS ==="
BASE_STATS=$("$STATS" -i "$BASE_EDS" -s "$BASE_SEDS" 2>/dev/null)
CTX_AVG=$(awk '/Average:/{print $2; exit}' <<<"$BASE_STATS" | tr ',' '.')
NUM_PATHS=$(awk '/Total paths/{print $NF; exit}' <<<"$BASE_STATS" | tr -d '.')
EDS_BYTES=$(fsize "$BASE_EDS")
echo "  ctx_avg=$CTX_AVG  paths=$NUM_PATHS  eds=$EDS_BYTES bytes"

echo "dataset,mode,l,l_over_ctx,leds_bytes,merge_peak_kb,merge_secs,merge_status,index_bytes,index_ri_bytes,index_ci_bytes,index_build_secs,index_peak_kb,index_status" > "$CSV"

# ---- phase 1+2: build l-EDS and index, per mode per l ------------------------
for MODE in linear cartesian; do
  for L in $L_VALUES; do
    if (( PATTERN_LEN % (L + 1) != 0 )); then
        echo "  skip l=$L — (l+1) does not divide PATTERN_LEN=$PATTERN_LEN" >&2
        continue
    fi
    TAG="${MODE}_l${L}"
    LEDS="$OUT_DIR/leds/$TAG.leds"
    IDX="$OUT_DIR/index/$TAG.idx"
    printf "  %-10s l=%-3s " "$MODE" "$L"

    if [[ "$MODE" == "linear" ]]; then
        run_capped "$OUT_DIR/logs/$TAG.merge.log" \
            "$EDS2LEDS" -i "$BASE_EDS" -s "$BASE_SEDS" -l "$L" -o "$LEDS"
    else
        run_capped "$OUT_DIR/logs/$TAG.merge.log" \
            "$EDS2LEDS" -i "$BASE_EDS" -l "$L" -o "$LEDS"
    fi
    MRC=$RC; MPEAK=$PEAK_KB; MSECS=$SECS

    # A cap kill can leave a truncated or empty file behind, so size is part of
    # the success test, not just the exit code.
    if (( MRC != 0 )) || [[ ! -s "$LEDS" ]]; then
        MSTATUS=$([[ $MRC -eq 124 ]] && echo timeout || echo oom_or_error)
        echo "MERGE $MSTATUS (peak $((MPEAK/1024))MB)"
        echo "$NAME,$MODE,$L,,0,$MPEAK,$MSECS,$MSTATUS,0,0,0,0,0,skipped" >> "$CSV"
        rm -f "$LEDS"
        continue
    fi
    LEDS_BYTES=$(fsize "$LEDS")

    run_capped "$OUT_DIR/logs/$TAG.index.log" \
        "$BUILD" -i "$LEDS" -l "$L" -o "$IDX"
    IRC=$RC; IPEAK=$PEAK_KB; ISECS=$SECS
    if (( IRC != 0 )); then
        echo "leds=$((LEDS_BYTES/1024))KB  INDEX FAILED"
        echo "$NAME,$MODE,$L,,$LEDS_BYTES,$MPEAK,$MSECS,ok,0,0,0,0,$IPEAK,failed" >> "$CSV"
        continue
    fi
    IDX_BYTES=$(index_bytes "$IDX")
    IDX_RI=$(index_component "$IDX" ri)
    IDX_CI=$(index_component "$IDX" ci)
    RATIO=$(python3 -c "print(f'{$L/$CTX_AVG:.3f}')" 2>/dev/null || echo "")
    echo "leds=$((LEDS_BYTES/1024))KB idx=$((IDX_BYTES/1024))KB merge=${MSECS}s"
    echo "$NAME,$MODE,$L,$RATIO,$LEDS_BYTES,$MPEAK,$MSECS,ok,$IDX_BYTES,$IDX_RI,$IDX_CI,$ISECS,$IPEAK,ok" >> "$CSV"
  done
done

# ---- phase 3: pattern sets ---------------------------------------------------
# real    — source-aware: each pattern walks one path, so it is a substring of a
#           genome in the panel. Both modes must find every one of these.
# decoy   — cartesian-only: generated ignoring sources, then filtered to those a
#           LINEAR index does NOT contain. These are the strings the cartesian
#           representation invents. Measures the precision difference.
# negative— random ACGT, essentially never present. Isolates early-exit cost.
echo "=== $NAME: pattern sets ==="
P_REAL="$OUT_DIR/patterns/real.txt"
P_DECOY="$OUT_DIR/patterns/decoy.txt"
P_NEG="$OUT_DIR/patterns/negative.txt"

"$GENPAT" -i "$BASE_EDS" -s "$BASE_SEDS" -o "$P_REAL" \
    -n "$N_PATTERNS" -l "$PATTERN_LEN" --seed "$SEED" 2>/dev/null
echo "  real:     $(wc -l < "$P_REAL")"

python3 - "$P_NEG" "$N_PATTERNS" "$PATTERN_LEN" "$SEED" <<'PY'
import random, sys
out, n, ln, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
r = random.Random(seed)
with open(out, "w") as f:
    for _ in range(n):
        f.write("".join(r.choice("ACGT") for _ in range(ln)) + "\n")
PY
echo "  negative: $(wc -l < "$P_NEG")"

# Decoys need a linear index to filter against; use the largest l that built.
REF_L=$(awk -F, '$2=="linear" && $14=="ok"{print $3}' "$CSV" | tail -1)
: > "$P_DECOY"
if [[ -n "${REF_L:-}" ]]; then
    RAW="$OUT_DIR/patterns/decoy_raw.txt"
    "$GENPAT" -i "$BASE_EDS" -o "$RAW" -n $((N_PATTERNS * 4)) \
        -l "$PATTERN_LEN" --seed "$SEED" --ignore-sources 2>/dev/null
    "$LOCATE" --benchmark -i "$OUT_DIR/index/linear_l$REF_L.idx" -l "$REF_L" \
        -P "$RAW" 2>/dev/null | awk -F'\t' '$2>0{print $1}' > "$OUT_DIR/patterns/.found"
    grep -Fxv -f "$OUT_DIR/patterns/.found" "$RAW" 2>/dev/null | head -n "$N_PATTERNS" > "$P_DECOY"
    echo "  decoy:    $(wc -l < "$P_DECOY")  (cartesian-only, filtered against linear l=$REF_L)"
fi

# ---- phase 4: query ----------------------------------------------------------
# biofmi-locate's reported runtime includes loading the index, which is not the
# quantity of interest. Timing the same set twice over (N and 2N patterns) makes
# the difference the cost of N queries with the load term cancelled.
echo "=== $NAME: querying ($REPS reps) ==="
echo "dataset,mode,l,pattern_set,n_patterns,matched,occurrences,load_ms,query_total_ms,per_pattern_ms" > "$QCSV"

time_ms() {  # time_ms <idx> <l> <patterns> -> echoes ms
    local st en
    st=$(date +%s%N)
    "$LOCATE" --benchmark -i "$1" -l "$2" -P "$3" >/dev/null 2>&1
    en=$(date +%s%N)
    echo $(( (en - st) / 1000000 ))
}

while IFS=, read -r ds mode l ratio lb mp ms mst ib iri ici ibs ip ist; do
    [[ "$ds" == "dataset" || "$ist" != "ok" ]] && continue
    IDX="$OUT_DIR/index/${mode}_l${l}.idx"
    for SET in real decoy negative; do
        PF="$OUT_DIR/patterns/$SET.txt"
        [[ -s "$PF" ]] || continue
        DBL="$OUT_DIR/patterns/.${SET}_double.txt"
        cat "$PF" "$PF" > "$DBL"

        OUT=$("$LOCATE" --benchmark -i "$IDX" -l "$l" -P "$PF" 2>&1)
        MATCHED=$(grep -cE "^[ACGT]+	[1-9][0-9]*$" <<<"$OUT")
        OCC=$(grep "Total occurrences" <<<"$OUT" | awk '{print $NF}')

        BEST1=999999999; BEST2=999999999
        for _ in $(seq "$REPS"); do
            t1=$(time_ms "$IDX" "$l" "$PF");  (( t1 < BEST1 )) && BEST1=$t1
            t2=$(time_ms "$IDX" "$l" "$DBL"); (( t2 < BEST2 )) && BEST2=$t2
        done
        # T(2N) - T(N) = N queries; T(N) - that = load.
        QMS=$(( BEST2 - BEST1 )); (( QMS < 0 )) && QMS=0
        LOADMS=$(( BEST1 - QMS )); (( LOADMS < 0 )) && LOADMS=0
        NP=$(wc -l < "$PF")
        PER=$(python3 -c "print(f'{$QMS/max($NP,1):.4f}')")
        echo "$ds,$mode,$l,$SET,$NP,$MATCHED,${OCC:-0},$LOADMS,$QMS,$PER" >> "$QCSV"
        printf "  %-10s l=%-3s %-9s matched=%-4s occ=%-8s query=%sms (load %sms)\n" \
            "$mode" "$l" "$SET" "$MATCHED" "${OCC:-0}" "$QMS" "$LOADMS"
        rm -f "$DBL"
    done
done < "$CSV"

cat > "$OUT_DIR/MANIFEST.txt" <<EOF
dataset      $NAME
base_eds     $BASE_EDS ($EDS_BYTES bytes)
base_seds    $BASE_SEDS
ctx_avg      $CTX_AVG
num_paths    $NUM_PATHS
l_values     $L_VALUES
pattern_len  $PATTERN_LEN
n_patterns   $N_PATTERNS
seed         $SEED
reps         $REPS
mem_cap      ${MEM_CAP_GB}G
timeout      ${TIMEOUT_S}s
edsparser    $COMMIT ($COMMIT_DATE) DIRTY=$DIRTY
host         $(hostname)
date         $(date -Iseconds)
EOF

echo
echo "=== done: $CSV / $QCSV ==="
