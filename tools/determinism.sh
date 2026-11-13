#!/bin/sh
# **Two runs, the same bytes.** The determinism gate of the design (section 7): every case in
# tests/cases compiled twice for all four targets at -O0 and -O2, the assembly, the diagnostics
# and the exit status of the two runs diffed. Self-hosting needs it - stage 2 and stage 3 can only
# agree if one compiler gives one output - and so does every golden this tree records.
#
# Usage: tools/determinism.sh [CXX1] [OUT]
#   CXX1  the compiler (default ./cxx3.exe)
#   OUT   a scratch directory (default /tmp/cxx1-determinism)
#   DET_TARGETS   the targets, if not all four (the -g object gate runs "x86_64-linux x86_64-windows")
#   DET_CASES     a glob of the cases, if not all (a box job is 25 minutes at most: split by it)
#
# Prints how many outputs were compared and names each one that differs; exits 1 if any does.
set -u
cd "$(dirname "$0")/.."
CXX1=${1:-./cxx3.exe}
OUT=${2:-/tmp/cxx1-determinism}
TARGETS=${DET_TARGETS:-"x86_64-linux x86_64-windows arm64-darwin tms6747"}
# No C6000 work on the Mac: tms6747 is the boxes'.
[ "$(uname -s)" = Darwin ] && [ -z "${DET_TARGETS:-}" ] && TARGETS="x86_64-linux x86_64-windows arm64-darwin"
LEVELS="-O0 -O2"
rm -rf "$OUT"; mkdir -p "$OUT/a" "$OUT/b"

# One line per job: the case, the target, the level.
jobs_list() {
    for src in tests/cases/${DET_CASES:-*}.cpp; do
        base=$(basename "$src" .cpp)
        for t in $TARGETS; do for l in $LEVELS; do echo "$base $t $l"; done; done
    done
}
# **The MIR dump is compared too** (M3a, RP2a condition 2): for the two x86 targets at each level, `-dump=mir` of the
# same compile goes to a file of its own, so the IR a pass left - not only the text written from it - is the same bytes.
run() {
    side=$1
    jobs_list | xargs -P 8 -n 3 sh -c '
        base=$0 t=$1 l=$2
        o="'"$OUT/$side"'/$base.$t$l"
        ( ulimit -t 120; "'"$CXX1"'" -S $l -arch $t "tests/cases/$base.cpp" -o "$o.s" < /dev/null > "$o.out" 2>&1; echo "exit=$?" >> "$o.out" )
        sed -i.bak "s|'"$OUT/$side"'|OUT|g" "$o.out" && rm -f "$o.out.bak"
        case $t in x86_64-*)
            ( ulimit -t 120; "'"$CXX1"'" -dump=mir -S $l -arch $t "tests/cases/$base.cpp" -o "$o.mir.s" < /dev/null > "$o.mir" 2>/dev/null )
            # **And the -g object** (M4.8), debug records included: a function of the source, not of the -o path.
            ( ulimit -t 120; "'"$CXX1"'" -nologo -g -c $l -arch $t "tests/cases/$base.cpp" -o "$o.g.obj" < /dev/null > /dev/null 2>&1 ) ;;
        esac
        # **And the SSA dump** (M3b.1): the IR the lowering made, before any selection, the same bytes twice.
        case $l in -O0)
            ( ulimit -t 120; "'"$CXX1"'" -dump=ssa -S -arch $t "tests/cases/$base.cpp" -o "$o.ssa.s" < /dev/null > "$o.ssa" 2>/dev/null )
            # **And the typed tree** (R12): the only input of the lowering, the same bytes twice.
            ( ulimit -t 120; "'"$CXX1"'" -dump=typed -S -arch $t "tests/cases/$base.cpp" -o "$o.typed.s" < /dev/null > "$o.typed" 2>/dev/null ) ;;
        esac'
}
run a
run b
n=$(ls "$OUT/a" | wc -l | tr -d ' ')
m=$(ls "$OUT/a" | grep -c '\.mir$'); g=$(ls "$OUT/a" | grep -c '\.g\.obj$'); k=$(ls "$OUT/a" | grep -c '\.ssa$'); y=$(ls "$OUT/a" | grep -c '\.typed$')
diffs=$(diff -rq "$OUT/a" "$OUT/b" | sed -E 's|.*/a/([^ ]*) and .*|\1|')
if [ -n "$diffs" ]; then
    echo "$diffs" | sed 's/^/  differs /'
    echo "determinism: $n outputs compared ($m of them -dump=mir), $(echo "$diffs" | wc -l | tr -d ' ') differ"
    exit 1
fi
echo "determinism: $n outputs compared ($m of them -dump=mir, $k -dump=ssa, $y -dump=typed, $g -g objects), 0 differ"
