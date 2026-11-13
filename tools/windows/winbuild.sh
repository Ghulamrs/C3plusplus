#!/bin/sh
# winbuild.sh <tree> <name>: ship a git tree's tracked files to <work>\<name> on the Windows box and build cxx3-msvc.exe
# there (msvc\build.cmd, cl /W4 /WX) under /affinity 3FF; prints cl's warnings and errors. Capped at 9 minutes.
# WINDOWS_BOX (default windows) and CXX1_WINDOWS_WORK (default C:\Users\GRA\work) name the box and its folder.
T=$1; N=$2; B=${WINDOWS_BOX:-windows}; W=${CXX1_WINDOWS_WORK:-'C:\Users\GRA\work'}; WF=$(printf '%s' "$W" | tr '\\' '/')
L=$(mktemp -d); trap 'rm -rf "$L"' EXIT
(cd "$T" && git ls-files -z | COPYFILE_DISABLE=1 xargs -0 tar -czf "$L/t.tgz") || exit 1
scp -q -o BatchMode=yes "$L/t.tgz" "$B:$WF/wb-$N.tgz" || exit 1
ssh -o BatchMode=yes "$B" "rmdir /s /q $W\\$N 2>nul & mkdir $W\\$N && tar -C $W\\$N -xzf $W\\wb-$N.tgz && del $W\\wb-$N.tgz && set _CL_=/MP2&& start \"\" /b /wait /affinity 3FF $W\\$N\\msvc\\build.cmd" > "$L/b.log" 2>&1 &
P=$!; t=0   # the ssh may stay open after build.cmd ends: ended by its own PID
while kill -0 $P 2>/dev/null && [ $t -lt 540 ]; do grep -q 'cxx3-msvc.exe built' "$L/b.log" && { sleep 3; break; }; sleep 3; t=$((t+3)); done
kill $P 2>/dev/null; [ $t -ge 540 ] && echo "winbuild.sh: CAPPED - the build passed 9 minutes"
grep -E 'warning C|error C|error LNK|fatal error' "$L/b.log" | sed 's|^.*\\src\\|src\\|' | sort -u | head -40
grep -q 'cxx3-msvc.exe built' "$L/b.log" && echo "winbuild.sh: $N built" || { echo "winbuild.sh: $N did NOT build"; exit 1; }
