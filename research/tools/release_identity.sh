#!/usr/bin/env bash
# ============================================================
# Is the RELEASE object code of the working tree identical to a revision's?
# ============================================================
# For a change that is meant to touch only the research build (anything
# behind STRATUM_ENABLE_METRICS, or a new empty note* wrapper), this is a
# stronger statement than any timing: if the machine code is the same, the
# release build cannot have become slower or faster.
#
# Compiles benchmarks/timings.cpp and examples/basic.cpp of the working tree
# twice with the release flags - once against include/ of BASE, once against
# the working tree's include/ - and compares the .text sections byte for
# byte. The embedded flags string is pinned to the same value on both sides
# so it cannot make an otherwise identical binary differ.
#
# Usage: release_identity.sh [BASE]     (default: master; CXX=g++)
# Exit status: 0 identical, 1 different, 2 usage/build error.
set -euo pipefail
BASE=${1:-master}
CXX=${CXX:-g++}
ROOT=$(git rev-parse --show-toplevel)
cd "$ROOT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
git archive "$BASE" include | tar -x -C "$TMP" || exit 2
FLAGS="-std=c++17 -O3 -Wall -Wextra -DNDEBUG"
status=0
for src in benchmarks/timings.cpp examples/basic.cpp; do
    b=$(basename "$src" .cpp)
    "$CXX" $FLAGS -I"$TMP/include" -Idatasets -Ibenchmarks -DSTRATUM_CXXFLAGS='"pinned"' "$src" -o "$TMP/$b.base" || exit 2
    "$CXX" $FLAGS -Iinclude -Idatasets -Ibenchmarks -DSTRATUM_CXXFLAGS='"pinned"' "$src" -o "$TMP/$b.tree" || exit 2
    objcopy -O binary --only-section=.text "$TMP/$b.base" "$TMP/$b.base.text"
    objcopy -O binary --only-section=.text "$TMP/$b.tree" "$TMP/$b.tree.text"
    if cmp -s "$TMP/$b.base.text" "$TMP/$b.tree.text"; then
        echo "$CXX $src: .text identical to $BASE ($(wc -c < "$TMP/$b.tree.text") bytes)"
    else
        echo "$CXX $src: .text DIFFERS from $BASE"
        status=1
    fi
done
exit $status
