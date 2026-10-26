#!/bin/sh
# **What a Windows object can be held to without a Windows box.** Every case compiled for x86_64-windows at -O0
# and -O2 in both spellings - `cxx3 -c` (the MASM spelling, assembled in process) and `-S -masm=gnu` assembled by
# clang - and three checks run over what came out:
#
#   tools/win-unwind-check     .pdata, UNWIND_INFO against the prologue's bytes, the FH3 tables (every object)
#   tools/win-layout-check     vftables, vbtables, RTTI and throw records beside clang's object of the same source
#   tools/mangled-names        the Microsoft names beside clang's, the x86_64-windows row of tests/names.sh
#
#   tests/win-structure.sh                every case; JOBS= workers (the machine's processors by default)
#   tests/win-structure.sh case-name...   those cases alone
#
# A case refused for the target (<case>.notarget) is skipped and said; a case no MASM can write (<case>.nomasm)
# goes through the GNU spelling alone. A layout difference in a case whose .nonames (x86_64-windows line) or .nocl
# records a difference is printed under "recorded" and does not fail the run - the record may or may not be about
# it, and that is for the reader; one in a case with no record fails. The objects stay in tests/out-winstructure,
# with every report beside them (<case>.layout, <case>.names, unwind.report; `tools/win-unwind-check -v` on an
# object lists its notes). In `make test` since 2026-10-26: 36 seconds on a 12-processor Mac; it skips itself
# where there is no clang++, saying so, as names.sh does. What it cannot see - that the code runs - is the run
# suite's, on a Windows box or under Wine.
set -u
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cxx3.exe}"
case "$CXX1" in /*) ;; *) CXX1="$(pwd)/$CXX1" ;; esac
OUT=tests/out-winstructure
CLANG=${CLANG:-clang++}
TARGET=x86_64-pc-windows-msvc

if [ "${1:-}" = --one ]; then
    base=$2; src=tests/cases/$base.cpp
    log="$OUT/$base.log"; : > "$log"
    if [ -f "tests/cases/$base.notarget" ] && grep -q "^x86_64-windows\b" "tests/cases/$base.notarget"; then
        echo "skip: $(grep '^x86_64-windows\b' "tests/cases/$base.notarget" | sed 's/^x86_64-windows[[:space:]]*//')" > "$log"
        exit 0
    fi
    nomasm=; [ -f "tests/cases/${base%.part}.nomasm" ] && nomasm=yes
    for L in 0 2; do
        if [ -z "$nomasm" ]; then
            ( ulimit -t 60; "$CXX1" -nologo -arch x86_64-windows -masm=masm -O$L -c "$src" -o "$OUT/$base.O$L.masm.obj" \
                < /dev/null > /dev/null 2>&1 ) || echo "refused: -masm=masm -O$L" >> "$log"
        fi
        if ( ulimit -t 60; "$CXX1" -nologo -arch x86_64-windows -masm=gnu -O$L -S "$src" -o "$OUT/$base.O$L.gnu.s" \
                < /dev/null > /dev/null 2>&1 ); then
            "$CLANG" -target $TARGET -c "$OUT/$base.O$L.gnu.s" -o "$OUT/$base.O$L.gnu.obj" > "$OUT/$base.O$L.gnu.err" 2>&1 \
                || echo "clang refused the GNU spelling at -O$L: $(head -1 "$OUT/$base.O$L.gnu.err")" >> "$log"
        else
            echo "refused: -masm=gnu -O$L" >> "$log"
        fi
    done
    # clang's object of the same source, the reference the layout check reads; its refusal is not a fault here.
    ( ulimit -t 60; "$CLANG" -x c++ -std=c++11 -fexceptions -frtti -w -target $TARGET -O0 -c "$src" -o "$OUT/$base.ref.obj" \
        < /dev/null > /dev/null 2>&1 ) || echo "clang refused the source" >> "$log"
    if [ -f "$OUT/$base.O0.masm.obj" ] && [ -f "$OUT/$base.ref.obj" ]; then
        python3 tools/win-layout-check "$OUT/$base.O0.masm.obj" "$OUT/$base.ref.obj" > "$OUT/$base.layout" 2>&1 || true
    elif [ -f "$OUT/$base.O0.gnu.obj" ] && [ -f "$OUT/$base.ref.obj" ]; then
        python3 tools/win-layout-check "$OUT/$base.O0.gnu.obj" "$OUT/$base.ref.obj" > "$OUT/$base.layout" 2>&1 || true
    fi
    TARGETS=x86_64-windows tools/mangled-names "$src" > "$OUT/$base.names" 2>&1 || echo "names: differ" >> "$log"
    exit 0
fi

rm -rf "$OUT"; mkdir -p "$OUT"
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
cases() {
    if [ $# -gt 0 ]; then for c in "$@"; do echo "$c"; done; return; fi
    for src in tests/cases/*.cpp; do
        case "$(basename "$src")" in *" "[0-9]*) continue;; esac
        base=$(basename "$src" .cpp)
        [ -f "tests/cases/$base.error" ] && continue
        echo "$base"
    done
}
if ! command -v "$CLANG" > /dev/null 2>&1; then
    echo "win-structure.sh: skipped - no $CLANG to assemble the GNU spelling and compile the reference"
    exit 0
fi
cases "$@" | xargs -P "$JOBS" -n 1 sh "$0" --one

status=0
skipped=0; refused=0
for base in $(cases "$@"); do
    [ -f "$OUT/$base.log" ] || continue
    if grep -q '^skip:' "$OUT/$base.log"; then skipped=$((skipped + 1)); continue; fi
    if grep -q '^refused:' "$OUT/$base.log"; then refused=$((refused + 1)); fi
    grep -v '^refused:\|^names: differ\|^clang refused the source' "$OUT/$base.log" | sed "s/^/$base: /"
    grep -q '^clang refused the GNU' "$OUT/$base.log" && status=1
done
echo "win-structure.sh: $skipped cases skipped for the target, $refused refusals (the run suite's to judge)"

# The unwind check, over every object of both spellings and levels at once.
ls "$OUT"/*.masm.obj "$OUT"/*.gnu.obj > "$OUT/objects" 2>/dev/null
if [ -s "$OUT/objects" ]; then
    xargs python3 tools/win-unwind-check < "$OUT/objects" > "$OUT/unwind.report" 2>&1 || status=1
    grep -v '^win-unwind-check:' "$OUT/unwind.report"
    grep '^win-unwind-check:' "$OUT/unwind.report" | sed 's/^win-unwind-check:/win-structure.sh: unwind:/'
fi

# The layout comparison: a difference in a case with a recorded Windows names difference is said and not counted.
compared=0; differ=0; recorded=0
for base in $(cases "$@"); do
    f="$OUT/$base.layout"
    [ -f "$f" ] || continue
    compared=$((compared + 1))
    n=$(grep -c -v '^win-layout-check:' "$f")
    [ "$n" = 0 ] && continue
    excuse=
    [ -f "tests/cases/$base.nonames" ] && grep -q '^x86_64-windows\b' "tests/cases/$base.nonames" && excuse=.nonames
    [ -f "tests/cases/$base.nocl" ] && excuse="${excuse:+$excuse and }.nocl"
    if [ -n "$excuse" ]; then
        recorded=$((recorded + 1))
        grep -v '^win-layout-check:' "$f" | sed "s/^/recorded ($excuse) /"
    else
        differ=$((differ + 1)); status=1
        grep -v '^win-layout-check:' "$f"
    fi
done
echo "win-structure.sh: layout: $compared cases beside clang's object, $differ differ, $recorded differ under a recorded names difference"

# The names, x86_64-windows only; tools/mangled-names prints each difference and its own skips.
agree=0; names_differ=0; nskip=0
for base in $(cases "$@"); do
    f="$OUT/$base.names"
    [ -f "$f" ] || continue
    if grep -q 'skipped' "$f"; then nskip=$((nskip + 1))
    elif grep -q 'all agreeing' "$f"; then agree=$((agree + 1))
    elif grep -q 'refused it' "$f"; then :
    else names_differ=$((names_differ + 1)); status=1; sed "s/^/$base: /" "$f"
    fi
done
echo "win-structure.sh: names: $agree cases agreeing with clang, $names_differ differing, $nskip under a .nonames"
exit $status
