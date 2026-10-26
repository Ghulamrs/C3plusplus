#!/bin/sh
# **The Windows cases without a Windows box** (2026-10-26): the stand-in for tools/windows/run-cases.cmd. Every case
# with a .expected is compiled for x86_64-windows here - by `cxx3 -c`, or by `-S` and the masm named - then linked
# on the Linux box by the project's LINK, with the driver's own Windows link line, against the Microsoft CRT and SDK
# libraries `xwin` 0.10.0 unpacked into ~/xwin/splat (the user accepted Microsoft's terms; the libraries never leave
# the box), and run under Wine 6.0.3 in the `c3wine` container (~/xwin/wine/Dockerfile). Output, carriage returns
# stripped, against the .expected; a failure is run once more and called FLAKY if it then passes.
# What it is not: cl, cdb, Microsoft's link.exe or a real Windows kernel - the sign-off stays a Windows box's. Wine
# 6.0's unwinder fails noexcept-terminates and temporary-unwind from any cxx3, M3's base 7d593d5 included.
#
# Usage: tools/wine-cases.sh TREE CXX3 LEVEL OUT [MASM]   (TREE: the tree whose tests/cases are run)
S=$(cd "$(dirname "$0")" && pwd); TREE=$1; CXX=$2; LVL=$3; OUT=$4; MASM=${5:-}
K=~/Documents/Claude/myMorningWalk.pem; B=ec2-user@52.202.164.123
rm -rf "$OUT"; mkdir -p "$OUT/cases"; nobuild=0; skipped=0
obj() {
    if [ -n "$MASM" ]; then "$CXX" -nologo -arch x86_64-windows $LVL $3 -S "$1" -o "$2.asm" && "$MASM" /nologo /c /Fo "$2" "$2.asm" >/dev/null && rm -f "$2.asm"
    else "$CXX" -nologo -arch x86_64-windows $LVL $3 -c "$1" -o "$2"; fi
}
for e in "$TREE"/tests/cases/*.expected; do
    n=$(basename "$e" .expected); base="$TREE/tests/cases/$n"
    if [ -f "$base.notarget" ] && grep -q x86_64-windows "$base.notarget"; then skipped=$((skipped + 1)); continue; fi
    if [ -f "$base.nomasm" ]; then skipped=$((skipped + 1)); continue; fi
    flags=""; [ -f "$base.flags" ] && flags=$(cat "$base.flags")
    obj "$base.cpp" "$OUT/$n.obj" "$flags" 2>/dev/null || { nobuild=$((nobuild + 1)); echo "DID NOT BUILD $n"; continue; }
    if [ -f "$base.part.cpp" ]; then obj "$base.part.cpp" "$OUT/$n.part.obj" "$flags" 2>/dev/null || { nobuild=$((nobuild + 1)); rm -f "$OUT/$n.obj"; continue; }; fi
    cp "$e" "$OUT/cases/"
done
echo "built on the Mac: $nobuild did not build, $skipped skipped"
T=$(basename "$OUT")
COPYFILE_DISABLE=1 tar -C "$OUT" -czf "/tmp/$T.tgz" . && scp -q -i $K "/tmp/$T.tgz" "$B:xwin/$T.tgz" && rm "/tmp/$T.tgz" && ssh -i $K -o BatchMode=yes $B "
rm -rf ~/xwin/$T && mkdir ~/xwin/$T && tar -C ~/xwin/$T -xzf ~/xwin/$T.tgz 2>/dev/null && rm ~/xwin/$T.tgz && cd ~/xwin/$T &&
L='/libpath:\$HOME/xwin/splat/crt/lib/x86_64 /libpath:\$HOME/xwin/splat/sdk/lib/um/x86_64 /libpath:\$HOME/xwin/splat/sdk/lib/ucrt/x86_64'
for o in \$(ls *.obj | grep -v '\.part\.obj\$'); do n=\${o%.obj}; p=''; [ -f \$n.part.obj ] && p=\$n.part.obj
  ~/xwin/bin/link.exe /nologo /subsystem:console /stack:8388608 /out:\$n.exe \$o \$p libcmt.lib libcpmt.lib libucrt.lib libvcruntime.lib kernel32.lib legacy_stdio_definitions.lib \$(eval echo \$L) > \$n.link 2>&1 || echo LINKFAIL \$n; done
docker run --rm -v \$PWD:/w c3wine sh -c 'cd /w; for x in *.exe; do timeout 20 /usr/lib/wine/wine64 ./\$x > \${x%.exe}.out 2>&1 < /dev/null; done'
pass=0; fail=0; for x in *.exe; do n=\${x%.exe}
  if tr -d '\r' < \$n.out | cmp -s - cases/\$n.expected; then pass=\$((pass+1)); else
    docker run --rm -v \$PWD:/w c3wine sh -c \"cd /w; timeout 20 /usr/lib/wine/wine64 ./\$x > \$n.out2 2>&1 < /dev/null\"
    if tr -d '\r' < \$n.out2 | cmp -s - cases/\$n.expected; then pass=\$((pass+1)); echo \"FLAKY \$n (passed on a second run)\"; else fail=\$((fail+1)); echo \"FAIL \$n\"; fi; fi; done
echo \"wine $LVL: \$pass passed, \$fail failed\""
