#!/bin/sh
# **Two runs, the same bytes.** The determinism gate of the design (section 7): every case in
# tests/cases compiled twice for all four targets at -O0 and -O2, the assembly, the diagnostics
# and the exit status of the two runs diffed. Self-hosting needs it - stage 2 and stage 3 can only
# agree if one compiler gives one output - and so does every golden this tree records.
#
# Usage: tools/determinism.sh [CXX1] [OUT]
#   CXX1  the compiler (default ./cxx3.exe)
#   OUT   a scratch directory (default /tmp/cxx1-determinism)
#
# Prints how many outputs were compared and names each one that differs; exits 1 if any does.
set -u
cd "$(dirname "$0")/.."
CXX1=${1:-./cxx3.exe}
OUT=${2:-/tmp/cxx1-determinism}
TARGETS="x86_64-linux x86_64-windows arm64-darwin tms6747"
LEVELS="-O0 -O2"
rm -rf "$OUT"; mkdir -p "$OUT/a" "$OUT/b"

# One line per job: the case, the target, the level.
jobs_list() {
    for src in tests/cases/*.cpp; do
        base=$(basename "$src" .cpp)
        for t in $TARGETS; do for l in $LEVELS; do echo "$base $t $l"; done; done
    done
}
run() {
    side=$1
    jobs_list | xargs -P 8 -n 3 sh -c '
        base=$0 t=$1 l=$2
        o="'"$OUT/$side"'/$base.$t$l"
        ( ulimit -t 120; "'"$CXX1"'" -S $l -arch $t "tests/cases/$base.cpp" -o "$o.s" < /dev/null > "$o.out" 2>&1; echo "exit=$?" >> "$o.out" )
        sed -i.bak "s|'"$OUT/$side"'|OUT|g" "$o.out" && rm -f "$o.out.bak"'
}
run a
run b
n=$(ls "$OUT/a" | wc -l | tr -d ' ')
diffs=$(diff -rq "$OUT/a" "$OUT/b" | sed -E 's|.*/a/([^ ]*) and .*|\1|')
if [ -n "$diffs" ]; then
    echo "$diffs" | sed 's/^/  differs /'
    echo "determinism: $n outputs compared, $(echo "$diffs" | wc -l | tr -d ' ') differ"
    exit 1
fi
echo "determinism: $n outputs compared, 0 differ"
