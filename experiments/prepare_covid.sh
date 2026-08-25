#!/usr/bin/env bash
#
# Prepare the COVID datasets for BIO-FMI index build + locate.
#
# Produces, per dataset, everything the index stage consumes and nothing it
# does not: a base EDS with sources, one l-EDS per l, and one shared pattern
# set. Building the index and running locate is deliberately NOT here — that is
# the measurement, and it belongs in the experiment harness.
#
#   ./experiments/prepare_covid.sh                     # covid294, linear, default l set
#   ./experiments/prepare_covid.sh --modes both        # add the cartesian arm
#   ./experiments/prepare_covid.sh --datasets clades   # the 31 per-clade panels
#   ./experiments/prepare_covid.sh --dry-run
#
# Layout produced under $WORK (default ~/Data/covid/work):
#
#   <dataset>/base.eds  base.seds
#   <dataset>/linear/l<N>.leds  l<N>.seds
#   <dataset>/cartesian/l<N>.leds            (no .seds: cartesian ignores sources)
#   <dataset>/patterns/real.txt              source-aware, seeded  -> E1/E3
#   <dataset>/patterns/cartesian_pool.txt    source-blind, seeded  -> decoy raw material
#   <dataset>/patterns/negative.txt          random, absent by construction
#   <dataset>/MANIFEST.txt
#   <dataset>/prepare.tsv                    one row per artifact: status, bytes, secs, peak KB
#
# Re-running skips artifacts that already exist and are non-empty; --force redoes them.
set -uo pipefail

# ── Defaults ─────────────────────────────────────────────────────────────────
DATA_ROOT="${DATA_ROOT:-$HOME/Data/covid}"
WORK="${WORK:-$DATA_ROOT/work}"

# The default l set is not arbitrary: BIO-FMI requires |P| to be a multiple of
# l+1 (chunk_size = context_length + 1), so a sweep whose l+1 values share no
# common multiple cannot be plotted against a fixed pattern length at all.
# Every l below satisfies (l+1) | 120, which is why PATTERN_LEN is 120.
L_VALUES="${L_VALUES:-3 5 9 11 14 19 29 39 59}"
PATTERN_LEN="${PATTERN_LEN:-120}"
N_PATTERNS="${N_PATTERNS:-200}"
SEED="${SEED:-7}"

MODES="linear"
DATASETS="covid294"
MAX_MEMORY="${MAX_MEMORY:-8G}"
THREADS="${THREADS:-1}"
FORCE=0
DRY_RUN=0

# ── Argument parsing ─────────────────────────────────────────────────────────
usage() {
    sed -n '2,30p' "$0" | sed 's/^# \?//'
    cat <<EOF

Options:
  --datasets LIST   covid294 | clades | both            (default: covid294)
  --modes LIST      linear | cartesian | both           (default: linear)
  --l-values "..."  space-separated l set               (default: $L_VALUES)
  --pattern-len N   pattern length                      (default: $PATTERN_LEN)
  --patterns N      patterns per set                    (default: $N_PATTERNS)
  --seed N          PRNG seed for the pattern sets      (default: $SEED)
  --max-memory SZ   per-merge cap, exit 3 => recorded as oom (default: $MAX_MEMORY)
  --threads N       eds2leds threads                    (default: $THREADS)
  --work DIR        output root                         (default: $WORK)
  --force           rebuild artifacts that already exist
  --dry-run         print what would run, do nothing
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --datasets)    DATASETS="$2"; shift 2 ;;
        --modes)       MODES="$2"; shift 2 ;;
        --l-values)    L_VALUES="$2"; shift 2 ;;
        --pattern-len) PATTERN_LEN="$2"; shift 2 ;;
        --patterns)    N_PATTERNS="$2"; shift 2 ;;
        --seed)        SEED="$2"; shift 2 ;;
        --max-memory)  MAX_MEMORY="$2"; shift 2 ;;
        --threads)     THREADS="$2"; shift 2 ;;
        --work)        WORK="$2"; shift 2 ;;
        --force)       FORCE=1; shift ;;
        --dry-run)     DRY_RUN=1; shift ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ "$MODES" == "both" ]] && MODES="linear cartesian"
[[ "$DATASETS" == "both" ]] && DATASETS="covid294 clades"

RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'; DIM=$'\033[2m'; NC=$'\033[0m'
log()  { echo "${DIM}[$(date +%H:%M:%S)]${NC} $*"; }
ok()   { echo "  ${GREEN}ok${NC}   $*"; }
warn() { echo "  ${YELLOW}warn${NC} $*"; }
die()  { echo "${RED}error${NC} $*" >&2; exit 1; }

# ── Locate the edsparser tools ───────────────────────────────────────────────
# build/tools before PATH, deliberately: an installed ~/.local/bin copy that
# predates a flag silently produces different output, and a whole sweep has
# already been lost to exactly that.
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
find_tool() {
    local name="$1" c
    for c in "$REPO/external/edsparser/build/tools/$name" \
             "$REPO/../edsparser/build/tools/$name"; do
        [[ -x "$c" ]] && { readlink -f "$c"; return 0; }
    done
    command -v "$name" 2>/dev/null && return 0
    return 1
}

MSA2EDS="$(find_tool msa2eds)"        || die "msa2eds not found — build edsparser first"
EDS2LEDS="$(find_tool eds2leds)"      || die "eds2leds not found — build edsparser first"
GENPATTERNS="$(find_tool edsparser-genpatterns)" || die "edsparser-genpatterns not found"

# Provenance. A DIRTY tool is fine for exploration and disqualifying for a
# published number, so record it loudly rather than refusing outright.
stamp() { "$1" --version 2>/dev/null | tr '\n' ' ' | sed 's/  */ /g'; }
EDSPARSER_STAMP="$(stamp "$EDS2LEDS")"
if [[ "$EDSPARSER_STAMP" == *"DIRTY=1"* ]]; then
    warn "edsparser tools are DIRTY=1 — usable for exploration, not for a published run"
fi

# ── Sanity: the chunking constraint ──────────────────────────────────────────
# Getting this wrong does not fail loudly, it silently produces a sweep whose
# points cannot share an x-axis. Check it before doing any work.
bad_l=""
for l in $L_VALUES; do
    (( PATTERN_LEN % (l + 1) != 0 )) && bad_l+=" $l"
done
if [[ -n "$bad_l" ]]; then
    warn "pattern length $PATTERN_LEN is not a multiple of l+1 for l in:$bad_l"
    warn "BIO-FMI will reject those patterns (chunk_size = l+1). Query results"
    warn "for these l cannot be compared against the others at fixed |P|."
fi

# ── Dataset resolution ───────────────────────────────────────────────────────
# Emits "<name>\t<msa path>" per dataset. The per-clade MSAs come from the
# sanitised copies: the originals are named "20H (Beta).msa", and spaces and
# parentheses in a filename are a standing hazard in a shell pipeline.
resolve_datasets() {
    local d
    for d in $DATASETS; do
        case "$d" in
            covid294)
                local m="$DATA_ROOT/raw/all_sequences.msa"
                [[ -f "$m" ]] || { warn "missing $m — skipping covid294"; continue; }
                printf 'covid294\t%s\n' "$m"
                ;;
            clades)
                local f
                for f in "$DATA_ROOT/derived/msa"/*.msa; do
                    [[ -f "$f" ]] || continue
                    printf 'clade_%s\t%s\n' "$(basename "$f" .msa)" "$f"
                done
                ;;
            *)  warn "unknown dataset '$d' — skipping" ;;
        esac
    done
}

# ── Run one command, recording status / seconds / peak RSS ───────────────────
# Exit 3 from eds2leds is the --max-memory refusal: a recorded outcome, not a
# script failure. A cartesian arm hitting the cap at high l *is* the
# measurement, so it must land in the table rather than abort the run.
TIME_FMT='%e %M'
run_step() {
    local label="$1" outfile="$2" tsv="$3"; shift 3
    if [[ -s "$outfile" && $FORCE -eq 0 ]]; then
        ok "$label (cached)"
        printf '%s\tcached\t%s\t\t\n' "$label" "$(stat -c%s "$outfile")" >> "$tsv"
        return 0
    fi
    if [[ $DRY_RUN -eq 1 ]]; then
        echo "  would run: $*"
        return 0
    fi

    local tf; tf="$(mktemp)"
    local rc=0
    /usr/bin/time -f "$TIME_FMT" -o "$tf" "$@" >/dev/null 2>"$tf.err" || rc=$?
    local secs peak
    read -r secs peak < "$tf" 2>/dev/null || { secs=""; peak=""; }
    rm -f "$tf"

    local status bytes=0
    if [[ $rc -eq 0 && -s "$outfile" ]]; then
        status=ok; bytes="$(stat -c%s "$outfile")"; ok "$label  ${secs}s  ${peak}KB"
    elif [[ $rc -eq 3 ]]; then
        status=oom;  warn "$label refused by --max-memory $MAX_MEMORY (exit 3)"
    else
        status=fail; warn "$label FAILED (exit $rc): $(tail -1 "$tf.err" 2>/dev/null)"
    fi
    rm -f "$tf.err"
    printf '%s\t%s\t%s\t%s\t%s\n' "$label" "$status" "$bytes" "$secs" "$peak" >> "$tsv"
    [[ "$status" == "ok" ]]
}

# ── Negative controls ────────────────────────────────────────────────────────
# Random ACGT strings. At |P|=120 the chance of one occurring in a 30 kb x 294
# panel is ~0, but "~0" is not "verified": the oracle in the B4 work should
# confirm absence rather than this script asserting it.
gen_negative() {
    local out="$1" n="$2" len="$3" seed="$4"
    python3 - "$out" "$n" "$len" "$seed" <<'PY'
import random, sys
out, n, ln, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
rng = random.Random(seed ^ 0x5eed)          # distinct stream from the real set
with open(out, "w") as f:
    for _ in range(n):
        f.write("".join(rng.choice("ACGT") for _ in range(ln)) + "\n")
PY
}

# ── Main ─────────────────────────────────────────────────────────────────────
log "data root   $DATA_ROOT"
log "work        $WORK"
log "l values    $L_VALUES"
log "modes       $MODES"
log "patterns    $N_PATTERNS x ${PATTERN_LEN}bp, seed $SEED"
log "edsparser   $EDSPARSER_STAMP"
echo

mapfile -t DS < <(resolve_datasets)
[[ ${#DS[@]} -gt 0 ]] || die "no datasets resolved — is $DATA_ROOT populated?"

for entry in "${DS[@]}"; do
    name="${entry%%$'\t'*}"
    msa="${entry##*$'\t'}"
    out="$WORK/$name"
    tsv="$out/prepare.tsv"

    log "── $name  ($(basename "$msa"))"
    [[ $DRY_RUN -eq 1 ]] || mkdir -p "$out/patterns"
    [[ $DRY_RUN -eq 1 ]] || { [[ -f "$tsv" ]] || printf 'artifact\tstatus\tbytes\tsecs\tpeak_kb\n' > "$tsv"; }

    # 1. MSA -> EDS + SEDS
    base_eds="$out/base.eds"; base_seds="$out/base.seds"
    run_step "base.eds" "$base_eds" "$tsv" \
        "$MSA2EDS" -i "$msa" -o "$base_eds" -s "$base_seds" || { warn "$name: base EDS failed, skipping"; continue; }

    # 2. EDS -> l-EDS, per mode per l.
    #    LINEAR passes -s, so the merge keeps only combinations some path
    #    carries; CARTESIAN omits it and keeps every combination. That single
    #    flag is the whole difference, and it is what E5 measures.
    for mode in $MODES; do
        mdir="$out/$mode"
        [[ $DRY_RUN -eq 1 ]] || mkdir -p "$mdir"
        for l in $L_VALUES; do
            leds="$mdir/l$l.leds"
            if [[ "$mode" == "linear" ]]; then
                run_step "$mode/l$l" "$leds" "$tsv" \
                    "$EDS2LEDS" -i "$base_eds" -s "$base_seds" -l "$l" -o "$leds" \
                    --threads "$THREADS" --max-memory "$MAX_MEMORY"
            else
                run_step "$mode/l$l" "$leds" "$tsv" \
                    "$EDS2LEDS" -i "$base_eds" -l "$l" -o "$leds" \
                    --threads "$THREADS" --max-memory "$MAX_MEMORY"
            fi
        done
    done

    # 3. Pattern sets — generated from the BASE EDS, once, and reused across
    #    every l. Regenerating per l would compare different queries, and the
    #    whole point of the |P|=120 anchor is that one set serves all of them.
    pat="$out/patterns"
    if [[ $DRY_RUN -eq 0 ]]; then
        # real: source-aware, so each pattern is a substring of a genome the
        # panel actually contains. Without -s the generator samples the
        # cartesian language, and a LINEAR-merged index legitimately rejects
        # those — the benchmark would measure how invalid the set is.
        run_step "patterns/real" "$pat/real.txt" "$tsv" \
            "$GENPATTERNS" -i "$base_eds" -s "$base_seds" -o "$pat/real.txt" \
            -n "$N_PATTERNS" -l "$PATTERN_LEN" --seed "$SEED"

        # cartesian_pool: the same generator ignoring sources. Raw material for
        # the decoy set; the decoys proper are the members of this pool that the
        # linear l=59 index rejects, which cannot be determined until that index
        # exists. Filter it after the index stage.
        run_step "patterns/cartesian_pool" "$pat/cartesian_pool.txt" "$tsv" \
            "$GENPATTERNS" -i "$base_eds" -s "$base_seds" -o "$pat/cartesian_pool.txt" \
            -n "$N_PATTERNS" -l "$PATTERN_LEN" --seed "$SEED" --ignore-sources

        if [[ ! -s "$pat/negative.txt" || $FORCE -eq 1 ]]; then
            gen_negative "$pat/negative.txt" "$N_PATTERNS" "$PATTERN_LEN" "$SEED"
            ok "patterns/negative"
            printf 'patterns/negative\tok\t%s\t\t\n' "$(stat -c%s "$pat/negative.txt")" >> "$tsv"
        else
            ok "patterns/negative (cached)"
        fi
    fi

    # 4. Manifest
    if [[ $DRY_RUN -eq 0 ]]; then
        {
            echo "dataset       $name"
            echo "source_msa    $msa"
            echo "sequences     $(grep -c '^>' "$msa" 2>/dev/null)"
            echo "l_values      $L_VALUES"
            echo "modes         $MODES"
            echo "pattern_len   $PATTERN_LEN"
            echo "n_patterns    $N_PATTERNS"
            echo "seed          $SEED"
            echo "max_memory    $MAX_MEMORY"
            echo "threads       $THREADS"
            echo "edsparser     $EDSPARSER_STAMP"
            echo "host          $(hostname)"
            echo "date          $(date -Is)"
        } > "$out/MANIFEST.txt"
        ok "MANIFEST.txt"
    fi
    echo
done

if [[ $DRY_RUN -eq 0 ]]; then
    log "done. Next: build an index per l-EDS, e.g."
    echo "    biofmi-build -i $WORK/<dataset>/linear/l19.leds -l 19 -o <index dir>"
    echo "    biofmi-locate -i <index dir> -l 19 -P $WORK/<dataset>/patterns/real.txt --benchmark"
fi
