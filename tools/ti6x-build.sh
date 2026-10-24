#!/bin/sh
# ti6x-build.sh [dir] - the C6000 tools built on the Linux box from their sources, since the C6000 does not run on
# the Mac: ASM6x, LNK6x, VM6747 and SIM6747 with their own Makefiles, then RTS6x's libraries with them and a cxx3.
# <dir> (default ~/ti6x) holds the five repositories' sources, shipped from the Mac without their build output.
# CPP11 names the cxx3 for RTS6x (default: the self-host tree's stage 0). Run under the box's memory cap:
#   systemd-run --user --scope -q -p MemoryMax=1200M tools/ti6x-build.sh
set -u
T=${1:-$HOME/ti6x}
CPP11=${CPP11:-$HOME/selfhost-tree/cxx3.exe}
export TMPDIR=$T/tmp; mkdir -p "$TMPDIR"     # the box's /tmp is a small tmpfs other work fills
cd "$T" || exit 1
for t in ASM6x LNK6x VM6747 SIM6747; do
    echo "== $t"; ( cd "$t" && make -j2 > build.log 2>&1; echo "$t exit $?"; tail -3 build.log )
done
echo "== RTS6x"; ( cd RTS6x && make -j2 CPP11="$CPP11" ASM6X="$T/ASM6x/build/asm6x.exe" > build.log 2>&1; echo "RTS6x exit $?"; tail -3 build.log )
for x in ASM6x/build/asm6x.exe LNK6x/build/lnk6x.exe VM6747/vm6747.exe SIM6747/sim6747.exe; do "$T/$x" --version 2>&1 | head -1; done
ls "$T"/RTS6x/build/*.lib 2>/dev/null
echo build-all-done
