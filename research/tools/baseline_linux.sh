#!/usr/bin/env bash
# ============================================================
# Linux baseline of the release build (objective.md SS-1)
# ============================================================
# Builds the release `build/timings` from the working tree and runs it
# SESSIONS times, back to back, over the whole battery
# (--sizes all --types all), each session one process with REPS
# repetitions; every repetition times Stratum Sort and std::sort on the
# same input, alternated. The sessions are three runs of the SAME binary,
# so they also give the per-dataset noise floor. baseline_table.py turns
# the raw CSVs into the baseline and the floors file.
#
# Usage (normally through `make linux-baseline`):
#   baseline_linux.sh
# Environment (all optional):
#   SESSIONS=3  REPS=9  WARMUP=1  SIZES=all  TYPES=all  DATASETS=(all)
#   NAME=v$(VERSION)-linux-x86_64   OUTDIR=research/baselines
# Writes $OUTDIR/$NAME.csv, $OUTDIR/$NAME-floors.csv and the per-session
# raw CSVs in $OUTDIR/raw/. Exit status: that of baseline_table.py
# (1 if any sort disagreed with std::sort), 2 on usage or build errors.
set -euo pipefail

SESSIONS=${SESSIONS:-3}
REPS=${REPS:-9}
WARMUP=${WARMUP:-1}
SIZES=${SIZES:-all}
TYPES=${TYPES:-all}
DATASETS=${DATASETS:-}
VERSION=${VERSION:-$(sed -n 's/^VERSION := //p' Makefile)}
NAME=${NAME:-v${VERSION}-linux-x86_64}
OUTDIR=${OUTDIR:-research/baselines}

if [ ! -f benchmarks/timings.cpp ]; then
    echo "run from the repository root" >&2; exit 2
fi
make -s build/timings || exit 2

commit=$(git rev-parse HEAD 2>/dev/null || echo unknown)
dirty=""
if ! git diff --quiet HEAD -- include benchmarks datasets 2>/dev/null; then
    dirty=" (include/, benchmarks/ or datasets/ modified)"
fi
mkdir -p "$OUTDIR/raw"
args=(--csv --sizes "$SIZES" --types "$TYPES" --reps "$REPS" --warmup "$WARMUP")
[ -n "$DATASETS" ] && args+=(--datasets "$DATASETS")

raws=()
for s in $(seq 1 "$SESSIONS"); do
    raw="$OUTDIR/raw/$NAME-s$s.csv"
    echo "session $s/$SESSIONS: ./build/timings ${args[*]} --tag s$s > $raw" >&2
    start=$(date +%s)
    status=0
    ./build/timings "${args[@]}" --tag "s$s" > "$raw" || status=$?
    echo "session $s: $(( $(date +%s) - start )) s, exit $status" >&2
    raws+=("$raw")
done

python3 research/tools/baseline_table.py "${raws[@]}" \
    --out "$OUTDIR/$NAME.csv" --floors "$OUTDIR/$NAME-floors.csv" \
    --meta "commit: $commit$dirty" \
    --meta "date (UTC): $(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    --meta "command: SESSIONS=$SESSIONS REPS=$REPS WARMUP=$WARMUP SIZES=$SIZES TYPES=$TYPES make linux-baseline" \
    --meta "macOS arm64 (Apple M4): pendiente - the author contributes that CSV (objective.md section 7)"
