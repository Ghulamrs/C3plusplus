#!/bin/sh
# **The two doors to the encoder, one object.** Every case compiled for x86_64-windows with `-c` twice: through the
# structural door, where an instruction reaches the encoder library as data (M4.4), and through the text door
# (`-coff=text`), where the library reads the MASM text as masm does - and the two objects cmp'd. The text door is
# masm's own code, so this holds the structural door to what `-S` + masm would write, with no masm.exe needed.
#
#   tests/coff.sh            every case at -O0 and -O2, on any machine cxx3 builds on
#
# A case refused for this target leaves no object on either side and counts as equal; the refusal is the run suite's
# to judge. tools/coff-equal.sh is the wider gate, against an external masm, for a release.
set -u
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cxx3.exe}"
OUT=tests/out-coff
rm -rf "$OUT"; mkdir -p "$OUT"
pairs=0; objects=0; differ=0
for src in tests/cases/*.cpp; do
    base=$(basename "$src" .cpp)
    [ -f "tests/cases/$base.error" ] && continue
    for L in 0 2; do
        pairs=$((pairs + 1))
        s="$OUT/$base.O$L.s.obj"; t="$OUT/$base.O$L.t.obj"
        ( ulimit -t 60; $CXX1 -nologo -arch x86_64-windows -masm=masm -O$L -c "$src" -o "$s" < /dev/null > /dev/null 2>&1 )
        ( ulimit -t 60; $CXX1 -nologo -arch x86_64-windows -masm=masm -O$L -coff=text -c "$src" -o "$t" < /dev/null > /dev/null 2>&1 )
        if [ -f "$s" ] && [ -f "$t" ]; then
            objects=$((objects + 1))
            cmp -s "$s" "$t" || { differ=$((differ + 1)); echo "DIFFERS $base -O$L"; }
        elif [ -f "$s" ] || [ -f "$t" ]; then
            differ=$((differ + 1)); echo "ONE-SIDED $base -O$L"
        fi
    done
done
echo "coff.sh: $pairs pairs, $objects objects compared, $differ differ"
[ $differ = 0 ]
