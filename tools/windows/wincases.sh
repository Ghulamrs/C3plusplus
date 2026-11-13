#!/bin/sh
# wincases.sh <name> <flags> <spelling> <half>: one Windows job over the tree <work>\<name> already built - shards 1-7
# (half 1) or 8-14 (half 2) of fourteen, under <flags>, in one spelling (gnu or masm), WINLANES at a time (default 2),
# /affinity 3FF, capped at 25 minutes. gnu 1 starts the pass; the other three keep what the jobs before them wrote.
# After masm 2 (or with COMPARE=1) compare-cases.py prints the pass's one verdict: "<out>: N same, 0 differ; ...".
N=$1; F=$2; SP=$3; H=$4; B=${WINDOWS_BOX:-windows}; W=${CXX1_WINDOWS_WORK:-'C:\Users\GRA\work'}
CL=${CXX1_WINDOWS_CLANG:-'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang.exe'}
FIRST=$(( H == 1 ? 1 : 8 )); LAST=$(( H == 1 ? 7 : 14 )); KEEP=1; [ "$SP" = gnu ] && [ "$H" = 1 ] && KEEP=0
perl -e 'alarm 1500; exec @ARGV' ssh -o BatchMode=yes "$B" "set CXX1_CASES_LANES=${WINLANES:-2}&& set CXX1_PAR_FIRST=$FIRST&& set CXX1_PAR_LAST=$LAST&& set CXX1_CASES_KEEP=$KEEP&& set CXX1_MASM_SRC=$W\\verify-tools\\MASM&& set CXX1_LINK_SRC=$W\\verify-tools\\LINK&& set \"CPP11_AS=$CL\"&& set CXX1_CASES_SPELLINGS=$SP&& set CXX1_CASES_COMPILER=$W\\$N\\cxx3-msvc.exe&& set CXX1_CASES_FLAGS=$F&& start \"\" /b /wait /affinity 3FF $W\\$N\\tools\\windows\\run-cases.cmd $W\\$N >$W\\$N-rc-$SP$H.log 2>&1" > /dev/null 2>&1
[ $? -ge 128 ] && echo "wincases.sh: $SP $H at $F CAPPED at 25 minutes"
[ "$SP$H" = masm2 ] || [ "${COMPARE:-}" = 1 ] || exit 0
perl -e 'alarm 300; exec @ARGV' ssh -o BatchMode=yes "$B" "python $W\\$N\\tools\\windows\\compare-cases.py $W\\$N $W\\$N\\winout $W\\$N\\winout\\masm" 2>&1 | tr -d '\r' | awk '/^  /{d[++n]=$0; next} {print} END{for(i=1;i<=n&&i<=40;i++) print d[i]}'
