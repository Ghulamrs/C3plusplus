#!/bin/sh
# self-host-stages.sh <name> <flags>: self-host.cmd over the built tree <work>\<name> in the default spelling - the
# project's masm and LINK beside cxx3-msvc.exe (run-cases.cmd :tools) - stages "0 1", "2", "3" as box jobs, each
# capped at 25 minutes; a cut stage is resumed (SELFHOST_RESUME=1) up to twice. Prints self-host.cmd's counts and
# differ.txt. SELFHOST_GNU=1 takes clang as the assembler instead (the GNU spelling M3b.6-M3b.8 measured).
N=$1; F=$2; B=${WINDOWS_BOX:-windows}; W=${CXX1_WINDOWS_WORK:-'C:\Users\GRA\work'}; R="$W\\$N"
CL=${CXX1_WINDOWS_CLANG:-'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang.exe'}
AS=; [ "${SELFHOST_GNU:-}" = 1 ] && AS="set \"CPP11_AS=$CL\"&& "
[ -n "$AS" ] || perl -e 'alarm 540; exec @ARGV' ssh -o BatchMode=yes "$B" "set CXX1_MASM_SRC=$W\\verify-tools\\MASM&& set CXX1_LINK_SRC=$W\\verify-tools\\LINK&& start \"\" /b /wait /affinity 3FF $R\\tools\\windows\\run-cases.cmd :tools $R" 2>&1 | tr -d '\r' | tail -3
job() {   # $1 stages, $2 SELFHOST_RESUME; output to the caller, ended by its own PID at 1500 s
    ssh -o BatchMode=yes "$B" "$AS set _CL_=/MP2&& set SELFHOST_RESUME=$2&& start \"\" /b /wait /affinity 3FF cmd /c \"$R\\tools\\windows\\self-host.cmd $R \"$F\" \"$1\"\"" 2>&1 | tr -d '\r' &
    P=$!; t=0; while kill -0 $P 2>/dev/null && [ $t -lt 1500 ]; do sleep 5; t=$((t+5)); done
    kill $P 2>/dev/null; [ $t -ge 1500 ] && { echo "self-host-stages.sh: stages $1 cut at 25 minutes"; return 124; }; wait $P
}
for st in "0 1" 2 3; do
    echo "== [$F] stages $st $(date +%H:%M)"; job "$st" ""; r=$?
    for try in 1 2; do [ $r -eq 124 ] || break; job "${st##* }" 1; r=$?; done
done
echo "== [$F] self-host done $(date +%H:%M)"
