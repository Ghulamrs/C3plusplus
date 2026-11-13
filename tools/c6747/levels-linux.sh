#!/bin/sh
# levels-linux.sh <cxx3.exe> <tag> [out] - tools/c6747-levels' cpp11 rows on the Linux CCS 5.5 box: each kernel of
# tools/c6747/bench at -O1, -O2 and -Os, assembled by ~/ti6x's asm6x --compress, linked by cl6x 7.4.4 against
# rts6740_elf_eh.lib and run on the C6747 cycle-accurate simulator through DSS (each run capped at 180 s). <tag> ti
# builds the kernels with cl6x 7.4.4 at -O1 and -O2 instead. Prints "<tag>-<level> <kernel> count=..." per run.
set -u
W=$(cd "$(dirname "$0")" && pwd)
T=$1; G=$2; O=${3:-$W/out/levels-$G}
CCS=${CCS55:-$HOME/ti/ccsv5}; CG=$CCS/tools/compiler/c6000_7.4.4
LIB=${C6747_EHLIB:-$HOME/c6747-lib}; ASM=${ASM6X:-$HOME/ti6x/ASM6x/build/asm6x.exe}
[ -x "$CG/bin/cl6x" ] && [ -f "$LIB/rts6740_elf_eh.lib" ] || { echo "levels-linux: no CCS 5.5 or no runtime here"; exit 2; }
mkdir -p "$O" && cd "$O" || exit 2
LINK="-z --heap_size=0x800 --stack_size=0x800 -i$LIB --rom_model $W/C6747.cmd -lrts6740_elf_eh.lib"
run() {   # run <.out> <stem> <label> <kernel>
    r=$(timeout 180 "$CCS/ccs_base/scripting/bin/dss.sh" "$W/runca.js" "$W/c6747ca-linux.ccxml" "$1" "$2.stdout" 0 2>&1 | grep '^RESULT')
    tr -d '\r' < "$2.stdout" 2>/dev/null | cmp -s - "$W/bench/$4.expected" && v=expected || v=DIFFERS
    echo "$3 ${r#RESULT } $v"
}
for src in "$W"/bench/*.c "$W"/bench/*.cpp; do
    n=$(basename "$src"); n=${n%.*}
    if [ "$G" = ti ]; then
        for lv in O1 O2; do
            "$CG/bin/cl6x" -mv6740 --abi=eabi -$lv --symdebug:none -I"$CG/include" --obj_directory=. "$src" $LINK -o $n.$lv.out \
                > $n.$lv.log 2>&1 && run "$PWD/$n.$lv.out" $n.$lv "ti744-$lv $n" $n || echo "ti744-$lv $n build=FAILED"
        done; continue
    fi
    cp "$src" $n.cpp   # cpp11 compiles C++: a .c goes in under a .cpp name
    for lv in O1 O2 Os; do
        timeout 180 "$T" -arch tms6747 -nologo -$lv -S $n.cpp -o $n.$lv.s > $n.$lv.log 2>&1 &&
        timeout 180 "$ASM" --compress $n.$lv.s -o $n.$lv.obj >> $n.$lv.log 2>&1 &&
        "$CG/bin/cl6x" -mv6740 --abi=eabi $n.$lv.obj $LINK -o $n.$lv.out >> $n.$lv.log 2>&1 &&
        run "$PWD/$n.$lv.out" $n.$lv "$G-$lv $n" $n || echo "$G-$lv $n build=FAILED (see $O/$n.$lv.log)"
    done
done
