#!/bin/sh
# **Two compilers, the same assembly, byte for byte.** The gate for a change
# that is meant to reorganize the optimizer without changing what it emits:
# every case in tests/cases that compiles, and every unit of Compiler++,
# through both compilers at -O0, -O1 and -O2 for x86_64-windows in the GNU and
# the MASM spellings, and at -O0, -O1 and -O2 for x86_64-linux (-O0 added for M3a, whose MIR holder runs at
# every level - doc-cxx3/M3-PLAN.md); the outputs diffed,
# the diagnostics and exit status with them.
#
# Usage: tools/identical.sh REF NEW [CPP] [OUT]
#   REF  the reference cxx3.exe (a build of the untouched base)
#   NEW  the cxx3.exe under test
#   CPP  a Compiler++ tree (its units are CPP/Compiler++/*.cpp); default
#        ~/cxxopt-build/cpp, skipped if it is not there
#   OUT  a scratch directory; default ~/cxxopt-build/identical
#   REF_FLAGS and NEW_FLAGS, in the environment, are put in front of each side's flags: REF_FLAGS=-front=ssa
#   holds a NEW whose only path is the SSA lowering (M3b.7, which refuses the flag) against the M3b.6 binary under it.
#   LEVELS, in the environment, names the levels compared ("0 1 2" unless
#   set): a step that changes -O1 and -O2 on purpose is gated with LEVELS=0,
#   -O0 being the reference the other two are diffed against.
#   ARCHS, in the environment, names the targets ("x86_64-windows x86_64-linux" unless set; x86_64-windows is both
#   spellings): ARCHS="arm64-darwin tms6747" holds the targets the deletion excepted against the M3b.6 binary.
#   JOBS (2 unless set) compiles run at once: the Linux box has 2 cores; the Windows box may take 8.
#   TYPED=1, in the environment, adds the optional leg (R12): `-dump=typed` of every source once per target, the
#   typed tree compared beside the assembly - 0 differ for a commit that claims no front-end change.
#
# Prints the number of outputs compared and the number that differ, and names
# each one that does; exits 1 if any does. Runs the compiles eight at a time,
# each under a CPU limit, and the whole thing under a thirty-minute alarm.
#
# Called with one argument, `--one`, it is its own helper: one job, both sides.
set -u
if [ "${1:-}" = --one ]; then
    # --one REF NEW OUT NAME SRC FLAGS...
    REF=$2 NEW=$3 OUT=$4 NAME=$5 SRC=$6; shift 6
    # A diagnostic names the compiler by its own path and a header by its tree's lib/, which differ between the
    # two sides by construction, and indents its caret by that path; all three are made the same before the diff.
    # Under Git's bash the tree is /c/... here and C:\... or C:/... in what a cl-built compiler prints: all three go.
    tree() {
        d=$(dirname "$1"); printf '%s\n' "s|$1|cxx1|g" "s|$d/|<tree>/|g"
        command -v cygpath > /dev/null 2>&1 || return 0
        printf '%s\n' "s|$(cygpath -w "$d" | sed 's/\\/\\\\/g')[\\/]|<tree>/|g" "s|$(cygpath -m "$d")/|<tree>/|g"
    }
    tree "$REF" > "$OUT/$NAME.ref.sed"; tree "$NEW" > "$OUT/$NAME.new.sed"
    ( ulimit -t 60 2> /dev/null; "$REF" ${REF_FLAGS:-} "$@" "$SRC" -o "$OUT/$NAME.ref.s" < /dev/null 2>&1; echo "exit=$?" ) |
        sed -f "$OUT/$NAME.ref.sed" -e "s/^ *^/^/" > "$OUT/$NAME.ref.err"
    ( ulimit -t 60 2> /dev/null; "$NEW" ${NEW_FLAGS:-} "$@" "$SRC" -o "$OUT/$NAME.new.s" < /dev/null 2>&1; echo "exit=$?" ) |
        sed -f "$OUT/$NAME.new.sed" -e "s/^ *^/^/" > "$OUT/$NAME.new.err"
    rm -f "$OUT/$NAME.ref.sed" "$OUT/$NAME.new.sed"
    exit 0
fi

cd "$(dirname "$0")/.."
SELF=$(pwd)/tools/identical.sh
REF=$1 NEW=$2 CPP=${3:-$HOME/cxxopt-build/cpp} OUT=${4:-$HOME/cxxopt-build/identical}
case $REF in /*) ;; *) REF=$(pwd)/$REF ;; esac
case $NEW in /*) ;; *) NEW=$(pwd)/$NEW ;; esac
rm -rf "$OUT"; mkdir -p "$OUT"

# One line per job: the output's name, the source, then the flags - the shape
# xargs -L 1 hands the helper as arguments.
jobs="$OUT/jobs"
: > "$jobs"
sources() {
    for src in tests/cases/*.cpp; do
        base=$(basename "$src" .cpp)
        [ -f "tests/cases/$base.error" ] && continue
        echo "$src"
    done
    [ -d "$CPP/Compiler++" ] && ls "$CPP"/Compiler++/*.cpp
}
LEVELS=${LEVELS:-0 1 2}
for src in $(sources); do
    n=$(echo "$src" | tr '/' '_' | sed 's/\.cpp$//')
    for L in $LEVELS; do for A in ${ARCHS:-x86_64-windows x86_64-linux}; do
        case $A in
        x86_64-windows)
            echo "$n.win.gnu.O$L $src -nologo -arch x86_64-windows -masm=gnu -O$L -S" >> "$jobs"
            echo "$n.win.masm.O$L $src -nologo -arch x86_64-windows -masm=masm -O$L -S" >> "$jobs" ;;
        x86_64-linux) echo "$n.linux.O$L $src -nologo -arch x86_64-linux -O$L -S" >> "$jobs" ;;
        *) echo "$n.$A.O$L $src -nologo -arch $A -O$L -S" >> "$jobs" ;;
        esac
    done; done
    [ "${TYPED:-}" = 1 ] && for A in ${ARCHS:-x86_64-windows x86_64-linux}; do
        echo "$n.$A.typed $src -nologo -arch $A -dump=typed -S" >> "$jobs"
    done
done

perl -e 'alarm 1800; exec @ARGV' xargs -P "${JOBS:-2}" -L 1 sh "$SELF" --one "$REF" "$NEW" "$OUT" < "$jobs"

total=0; differ=0
while read -r name src flags; do
    total=$((total + 1))
    same=1
    # A compile refused on both sides leaves no assembly on either: equal.
    for side in s err; do
        r="$OUT/$name.ref.$side"; n="$OUT/$name.new.$side"
        if [ -f "$r" ] || [ -f "$n" ]; then cmp -s "$r" "$n" 2>/dev/null || same=0; fi
    done
    if [ $same = 0 ]; then
        differ=$((differ + 1))
        echo "DIFFERS $name ($src $flags)"
    fi
done < "$jobs"
echo "identical: $total compared, $differ differ"
[ $differ = 0 ]
