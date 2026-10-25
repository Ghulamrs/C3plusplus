#!/bin/sh
# **`-c` and `-S` + masm, the same object, byte for byte.** M4's gate (doc-cxx3/M4-PLAN.md): every case in
# tests/cases that compiles, and every unit of Compiler++, compiled for x86_64-windows in the MASM spelling two
# ways - `cxx3 -c`, whose object CoffEmitter writes in process, and `cxx3 -S` assembled by the masm named - at
# -O0 and -O2; the two objects cmp'd. A case refused on both sides leaves no object on either and counts equal.
#
# Usage: tools/coff-equal.sh MASM [CXX3] [CPP] [OUT]
#   MASM  the masm.exe to assemble the -S output with (the pre-rebase build of 6bcb5d2, or the re-based one)
#   CXX3  the cxx3.exe under test; default ./cxx3.exe
#   CPP   a Compiler++ tree (its units are CPP/Compiler++/*.cpp); default ~/cxxopt-build/cpp, skipped if absent
#   OUT   a scratch directory; default ~/cxxopt-build/coff-equal
#   LEVELS, in the environment, names the levels ("0 2" unless set); FLAGS adds to every compile (-g, say).
#
# Both objects are written to one path in turn, since S_OBJNAME under -g holds the object's absolute path.
# Prints the number of pairs compared and the number that differ, naming each; exits 1 if any does.
set -u
if [ "${1:-}" = --one ]; then
    # --one MASM CXX3 OUT NAME SRC FLAGS...
    MASM=$2 CXX3=$3 OUT=$4 NAME=$5 SRC=$6; shift 6
    rm -f "$OUT/$NAME.obj" "$OUT/$NAME.c.obj" "$OUT/$NAME.m.obj" "$OUT/$NAME.s"
    ( ulimit -t 120; "$CXX3" "$@" -c "$SRC" -o "$OUT/$NAME.obj" < /dev/null > "$OUT/$NAME.c.err" 2>&1; echo "exit=$?" >> "$OUT/$NAME.c.err" )
    [ -f "$OUT/$NAME.obj" ] && mv "$OUT/$NAME.obj" "$OUT/$NAME.c.obj"
    ( ulimit -t 120; "$CXX3" "$@" -S "$SRC" -o "$OUT/$NAME.s" < /dev/null > "$OUT/$NAME.s.err" 2>&1; echo "exit=$?" >> "$OUT/$NAME.s.err" )
    if [ -f "$OUT/$NAME.s" ]; then
        ( ulimit -t 120; "$MASM" /nologo /c /Fo "$OUT/$NAME.obj" "$OUT/$NAME.s" > "$OUT/$NAME.m.err" 2>&1; echo "exit=$?" >> "$OUT/$NAME.m.err" )
        [ -f "$OUT/$NAME.obj" ] && mv "$OUT/$NAME.obj" "$OUT/$NAME.m.obj"
    fi
    exit 0
fi

cd "$(dirname "$0")/.."
SELF=$(pwd)/tools/coff-equal.sh
MASM=$1 CXX3=${2:-./cxx3.exe} CPP=${3:-$HOME/cxxopt-build/cpp} OUT=${4:-$HOME/cxxopt-build/coff-equal}
case $MASM in /*) ;; *) MASM=$(pwd)/$MASM ;; esac
case $CXX3 in /*) ;; *) CXX3=$(pwd)/$CXX3 ;; esac
rm -rf "$OUT"; mkdir -p "$OUT"

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
LEVELS=${LEVELS:-0 2}
FLAGS=${FLAGS:-}
for src in $(sources); do
    n=$(echo "$src" | tr '/' '_' | sed 's/\.cpp$//')
    for L in $LEVELS; do
        # No blank at the line's end: xargs reads one as "the next line continues this one".
        echo "$n.O$L $src -nologo -arch x86_64-windows -masm=masm -O$L${FLAGS:+ $FLAGS}" >> "$jobs"
    done
done

perl -e 'alarm 3600; exec @ARGV' xargs -P 8 -L 1 sh "$SELF" --one "$MASM" "$CXX3" "$OUT" < "$jobs"

total=0; differ=0; objects=0
while read -r name src flags; do
    total=$((total + 1))
    c="$OUT/$name.c.obj"; m="$OUT/$name.m.obj"
    if [ -f "$c" ] && [ -f "$m" ]; then
        objects=$((objects + 1))
        cmp -s "$c" "$m" || { differ=$((differ + 1)); echo "DIFFERS $name ($src $flags)"; }
    elif [ -f "$c" ] || [ -f "$m" ]; then
        differ=$((differ + 1)); echo "ONE-SIDED $name ($src $flags): $([ -f "$c" ] && echo "-c wrote, -S+masm did not" || echo "-S+masm wrote, -c did not")"
    fi
done < "$jobs"
echo "coff-equal: $total pairs, $objects objects compared, $differ differ"
[ $differ = 0 ]
