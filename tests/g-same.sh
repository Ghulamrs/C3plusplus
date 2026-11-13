#!/bin/sh
# tests/g-same.sh [cxx3]: -g changes no instruction for tms6747 - every case compiled with -g and without at -O0 gives the same
# code once the .dw records and the lexical blocks' $blk$ labels are dropped. (-g with -O is refused by name for tms6747.)
# SAME, DIFF (the code differs), BROKE (compiles only without -g), SKIP (does not compile for tms6747); fails on DIFF or BROKE.
O=tests/out-g-same
if [ "$1" = --one ]; then X=$2; c=$3; b=$(basename "$c"); b=${b%.*}; n=$O/$b.s; g=$O/$b-g.s
    ( ulimit -t 20; $X -nologo -S -arch tms6747 -O0 "$c" -o $n ) </dev/null >/dev/null 2>&1 || { echo "SKIP $b"; exit 0; }
    ( ulimit -t 20; $X -nologo -S -arch tms6747 -O0 -g "$c" -o $g ) </dev/null >/dev/null 2>&1 || { echo "BROKE $b"; exit 0; }
    if grep -v '\.dw\|\$blk\$' $g | tr -d '\r' > $g.code; tr -d '\r' < $n | cmp -s - $g.code; then echo "SAME $b"; else echo "DIFF $b"; fi; exit 0; fi
X=${1:-./cxx3.exe}; rm -rf $O; mkdir -p $O
ls tests/cases/*.cpp tests/cases/*.c 2>/dev/null | xargs -P "${JOBS:-2}" -n 1 sh "$0" --one "$X" > $O/all.res.tmp 2>&1; mv $O/all.res.tmp $O/all.res
grep -E '^(DIFF|BROKE)' $O/all.res
echo "g-same: $(grep -c '^SAME' $O/all.res) same, $(grep -c '^DIFF' $O/all.res) differ, $(grep -c '^BROKE' $O/all.res) broken, $(grep -c '^SKIP' $O/all.res) not for tms6747"
! grep -qE '^(DIFF|BROKE)' $O/all.res
