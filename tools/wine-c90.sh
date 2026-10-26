#!/bin/sh
# **c90's cases on x86_64-windows without a Windows box** (M7.4, doc-cxx3/M7-PLAN.md): the stand-in for the run leg of
# `tests/c90.sh -arch x86_64-windows`, which on the Mac only compiles and assembles. Every case of tests/c90/cases and
# every directory of tests/c90/multi is compiled here by `cxx3 -std=c90 -c -arch x86_64-windows` (the MASM spelling, the
# in-process assembler), then on the Linux box linked by the project's LINK against the Microsoft CRT and SDK libraries
# xwin unpacked into ~/xwin/splat (as tools/wine-cases.sh links), and run under Wine 6.0.3 in the `c3wine` container.
# The reference is the box's own gcc over the same sources, as tests/c90.sh's is: stdout (carriage returns stripped) and
# the exit status must agree, and the status must be the case's `// expect: N`; a failure is run once more and called FLAKY
# if it then passes (Wine 6.0's start-up). Skipped by name, each list's reason beside
# it: tests/c90/tms6747-lp64.txt (a reference that needs a 64-bit long, which the Windows target has not),
# tests/c90/tms6747-msvc.txt (what Microsoft's CRT prints differently), tests/c90/linux-only.txt.
# What it is not: cl, cdb, Microsoft's link.exe or a Windows kernel - the sign-off stays a Windows box's.
#
# Usage: tools/wine-c90.sh TREE CXX3 LEVEL OUT      (LEVEL: -O0 or -O2; OUT: a scratch directory, its basename the box's)
S=$(cd "$(dirname "$0")" && pwd); TREE=$1; CXX=$2; LVL=$3; OUT=$4
K=~/Documents/Claude/myMorningWalk.pem; B=ec2-user@52.202.164.123
rm -rf "$OUT"; mkdir -p "$OUT/src"; nobuild=0; skipped=0; built=0
listed() { grep -q "^${1#multi.}[[:space:]]" "$TREE/tests/c90/tms6747-lp64.txt" "$TREE/tests/c90/tms6747-msvc.txt" "$TREE/tests/c90/linux-only.txt"; }
obj() { ( ulimit -t 20; "$CXX" -nologo -std=c90 -arch x86_64-windows $LVL -c "$1" -o "$2" < /dev/null ) 2>/dev/null; }
one() {  # one <name> <main.c> <src>...
    n=$1; main=$2; shift 2
    if listed "$n"; then skipped=$((skipped + 1)); return; fi
    expect=$(sed -n '1s|^// expect: *\([0-9-]*\).*|\1|p' "$main"); [ -n "$expect" ] || return
    mkdir -p "$OUT/src/$n"; i=0; objs=""
    case "$n" in multi.*) cp "$(dirname "$main")"/* "$OUT/src/$n/" ;; *) cp "$main" "$TREE/tests/c90/cases/pp_helper.h" "$OUT/src/$n/" ;; esac
    for s in "$@"; do
        if ! obj "$s" "$OUT/$n.$i.obj"; then nobuild=$((nobuild + 1)); echo "DID NOT BUILD $n ($(basename "$s"))"; rm -f $objs; return; fi
        objs="$objs $OUT/$n.$i.obj"; i=$((i + 1))
    done
    echo "$expect" > "$OUT/src/$n/expect"; built=$((built + 1))
}
for c in "$TREE"/tests/c90/cases/*.c; do one "$(basename "$c" .c)" "$c" "$c"; done
for d in "$TREE"/tests/c90/multi/*/; do one "multi.$(basename "$d")" "$d/main.c" "$d"/*.c; done
echo "built on the Mac: $built programs, $nobuild did not build, $skipped skipped by list"
T=$(basename "$OUT")
COPYFILE_DISABLE=1 tar -C "$OUT" -czf "/tmp/$T.tgz" . && scp -q -i $K "/tmp/$T.tgz" "$B:xwin/$T.tgz" && rm "/tmp/$T.tgz" && ssh -i $K -o BatchMode=yes $B "
rm -rf ~/xwin/$T && mkdir ~/xwin/$T && tar -C ~/xwin/$T -xzf ~/xwin/$T.tgz 2>/dev/null && rm ~/xwin/$T.tgz && cd ~/xwin/$T &&
L='/libpath:\$HOME/xwin/splat/crt/lib/x86_64 /libpath:\$HOME/xwin/splat/sdk/lib/um/x86_64 /libpath:\$HOME/xwin/splat/sdk/lib/ucrt/x86_64'
for d in src/*/; do n=\$(basename \$d)
  gcc -w \$d/*.c -o \$n.ref -lm > \$n.gcc 2>&1 || { echo REFFAIL \$n; continue; }
  ( ulimit -t 5; ./\$n.ref < /dev/null ) > \$n.ref.out 2>/dev/null; echo \$? > \$n.ref.rc
  ~/xwin/bin/link.exe /nologo /subsystem:console /stack:8388608 /out:\$n.exe \$n.*.obj libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib legacy_stdio_definitions.lib \$(eval echo \$L) > \$n.link 2>&1 || echo LINKFAIL \$n; done
docker run --rm -v \$PWD:/w c3wine sh -c 'cd /w; for x in *.exe; do timeout 20 /usr/lib/wine/wine64 ./\$x > \${x%.exe}.out 2>/dev/null < /dev/null; echo \$? > \${x%.exe}.rc; done'
ok() { tr -d '\r' < \$1.out | cmp -s - \$1.ref.out && [ \"\$(cat \$1.rc)\" = \"\$(cat \$1.ref.rc)\" ] && [ \"\$(cat \$1.rc)\" = \"\$(cat src/\$1/expect)\" ]; }
pass=0; fail=0; for x in *.exe; do n=\${x%.exe}
  if ok \$n; then pass=\$((pass+1)); else
    docker run --rm -v \$PWD:/w c3wine sh -c \"cd /w; timeout 20 /usr/lib/wine/wine64 ./\$x > \$n.out 2>/dev/null < /dev/null; echo \\\$? > \$n.rc\"
    if ok \$n; then pass=\$((pass+1)); echo \"FLAKY \$n (passed on a second run)\";
    else fail=\$((fail+1)); echo \"FAIL \$n (wine rc=\$(cat \$n.rc), gcc rc=\$(cat \$n.ref.rc), expect \$(cat src/\$n/expect))\"; tr -d '\r' < \$n.out | diff - \$n.ref.out | head -4 | sed 's/^/    /'; fi; fi; done
echo \"wine c90 $LVL: \$pass passed, \$fail failed\""
