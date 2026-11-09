#!/usr/bin/env bash
# ============================================================
# Alternating A/B of two commits, release build (objective.md G3)
# ============================================================
# Builds benchmarks/timings.cpp of the WORKING TREE twice, in the release
# configuration, once against the library headers (include/) of commit A
# and once against those of commit B, then runs the two binaries
# ALTERNATELY - A,B then B,A on the next repetition - so that drift in the
# machine hits both equally (BENCHMARKS.md, "Alternate"). The verdict
# table is produced by ab_table.py from the raw per-repetition CSV.
#
# Why the harness comes from the working tree and only include/ from each
# commit: an older commit's timings.cpp does not have the battery, the
# CSV output or the --reps 1 mode this needs, and the point of the A/B is
# to measure a change to the LIBRARY on identical inputs. The adversaries
# are still rebuilt per side: timings.cpp takes lambda and D from the
# headers it is compiled with, so each binary builds them against its own
# configuration, as BENCHMARKS.md requires.
#
# Usage (normally through `make ab`):
#   ab_compare.sh A B
#     A, B       any git revision, or WORKTREE for the uncommitted include/
# Environment (all optional):
#   SESSIONS=3   independent sessions (objective.md section 4 asks for 3)
#   REPS=9       alternated repetitions per session
#   WARMUP=1     untimed sorts per input before the timed one (see
#                timings.cpp --warmup); 0 halves the run time, at the cost
#                of a wider spread at small n
#   SIZES=1e4,1e5,1e6,1e7   TYPES=int64   DATASETS=(all)
#   FLOORS=file  CSV dataset,threshold with measured noise floors (SS-1);
#                the larger of it and the built-in threshold is used
#   OUT=dir      where to put binaries, raw.csv and table.md
#                (default build/ab/<A>_vs_<B>)
#   CXX=g++
# Exit status: that of ab_table.py - 0 if G3 passes, 1 if it rejects or if
# any sort disagreed with std::sort, 2 on usage or build errors.
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 A B   (git revisions, or WORKTREE)" >&2
    exit 2
fi
A_REV=$1
B_REV=$2
SESSIONS=${SESSIONS:-3}
REPS=${REPS:-9}
WARMUP=${WARMUP:-1}
SIZES=${SIZES:-1e4,1e5,1e6,1e7}
TYPES=${TYPES:-int64}
DATASETS=${DATASETS:-}
CXX=${CXX:-g++}

ROOT=$(git rev-parse --show-toplevel)
cd "$ROOT"

resolve() { # prints a short label for a revision
    if [ "$1" = WORKTREE ]; then echo WORKTREE; else git rev-parse --short=10 "$1^{commit}"; fi
}
A_ID=$(resolve "$A_REV") || { echo "unknown revision $A_REV" >&2; exit 2; }
B_ID=$(resolve "$B_REV") || { echo "unknown revision $B_REV" >&2; exit 2; }
OUT=${OUT:-build/ab/${A_ID}_vs_${B_ID}}
mkdir -p "$OUT"

# The release flags, identical to PROD_CXXFLAGS in the Makefile.
FLAGS="-std=c++17 -O3 -Wall -Wextra -DNDEBUG"

build_side() { # side rev id
    local side=$1 rev=$2 id=$3 inc
    if [ "$rev" = WORKTREE ]; then
        inc="$ROOT/include"
    else
        inc="$OUT/src_$side/include"
        rm -rf "$OUT/src_$side"
        mkdir -p "$OUT/src_$side"
        git archive "$id" include | tar -x -C "$OUT/src_$side"
    fi
    # -I order matters: the side's include/ is the ONLY place the library
    # can come from; datasets/ and benchmarks/ hold no library headers.
    "$CXX" $FLAGS -I"$inc" -Idatasets -Ibenchmarks \
        -DSTRATUM_CXXFLAGS="\"$FLAGS (A/B side $side = $id)\"" \
        benchmarks/timings.cpp -o "$OUT/timings_$side"
}

echo "A/B: A=$A_REV ($A_ID)  B=$B_REV ($B_ID)" >&2
echo "     sessions=$SESSIONS reps=$REPS warmup=$WARMUP sizes=$SIZES types=$TYPES datasets=${DATASETS:-all}" >&2
build_side A "$A_REV" "$A_ID" || exit 2
build_side B "$B_REV" "$B_ID" || exit 2

{
    echo "A=$A_REV $A_ID"
    echo "B=$B_REV $B_ID"
    echo "harness=working tree at $(git rev-parse --short=10 HEAD)$([ -n "$(git status --porcelain -- benchmarks datasets)" ] && echo ' + uncommitted changes in benchmarks/ or datasets/')"
    echo "sessions=$SESSIONS reps=$REPS warmup=$WARMUP sizes=$SIZES types=$TYPES datasets=${DATASETS:-all}"
    echo "cxx=$("$CXX" --version | head -1)"
    echo "host=$(uname -srm) cpus=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo ?)"
    echo "started=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$OUT/meta.txt"

RAW="$OUT/raw.csv"
echo "session,rep,side,tag,type,dataset,n,n_real,run,stratum_ms,std_ms,status" > "$RAW"

ds_args=()
[ -n "$DATASETS" ] && ds_args=(--datasets "$DATASETS")

run_side() { # side session rep size
    local tmp="$OUT/.run_$1.csv"
    # A disagreement with std::sort makes timings exit 1; it is NOT allowed
    # to abort the run silently: the MISMATCH rows reach raw.csv and
    # ab_table.py fails the whole A/B on them.
    "$OUT/timings_$1" --csv --reps 1 --warmup "$WARMUP" --sizes "$4" --types "$TYPES" "${ds_args[@]}" --tag "$1" \
        > "$tmp" || true
    grep -v '^#' "$tmp" | tail -n +2 | sed "s/^/$2,$3,$1,/" >> "$RAW"
}

IFS=',' read -r -a size_list <<< "$SIZES"
start=$(date +%s)
for ((s = 1; s <= SESSIONS; s++)); do
    for ((r = 1; r <= REPS; r++)); do
        for n in "${size_list[@]}"; do
            # ABBA: who goes first alternates with the repetition, so that
            # neither side systematically inherits the other's cache state.
            if (( r % 2 == 1 )); then
                run_side A "$s" "$r" "$n"; run_side B "$s" "$r" "$n"
            else
                run_side B "$s" "$r" "$n"; run_side A "$s" "$r" "$n"
            fi
        done
        echo "  session $s/$SESSIONS  rep $r/$REPS  ($(( $(date +%s) - start )) s)" >&2
    done
done
echo "finished=$(date -u +%Y-%m-%dT%H:%M:%SZ) elapsed_s=$(( $(date +%s) - start ))" >> "$OUT/meta.txt"

floors_args=()
[ -n "${FLOORS:-}" ] && floors_args=(--floors "$FLOORS")
set +e
python3 research/tools/ab_table.py "$RAW" --meta "$OUT/meta.txt" "${floors_args[@]}" | tee "$OUT/table.md"
status=${PIPESTATUS[0]}
echo "raw data: $RAW   table: $OUT/table.md" >&2
exit "$status"
