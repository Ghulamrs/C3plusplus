#!/usr/bin/env python3
# Compare a run-cases.cmd output folder with the recorded answers, on the Windows box itself:
# each <case>.out against tests/cases/<case>.expected (line endings dropped), each <case>.err
# against the text of tests/cases/<case>.error. What verify-three does on the Mac, done where the
# outputs are.
#
#   python compare-cases.py <tree root> <winout folder> [<winout folder> ...]
import os, sys

root = sys.argv[1]
cases = os.path.join(root, 'tests', 'cases')
bad = 0
for d in sys.argv[2:]:
    same = differ = refused = notrefused = 0
    for f in sorted(os.listdir(d)):
        n, ext = os.path.splitext(f)
        if ext == '.out' and os.path.exists(os.path.join(cases, n + '.expected')):
            got = open(os.path.join(d, f), encoding='latin-1').read().replace('\r', '')
            want = open(os.path.join(cases, n + '.expected'), encoding='latin-1').read()
            if got == want: same += 1
            else: differ += 1; print('  differ ' + n)
        elif ext == '.err' and os.path.exists(os.path.join(cases, n + '.error')):
            got = open(os.path.join(d, f), encoding='latin-1').read().replace('\r', '')
            want = open(os.path.join(cases, n + '.error'), encoding='latin-1').read().strip()
            if want in got: refused += 1
            else: notrefused += 1; print('  refusal ' + n)
    print('%s: %d same, %d differ; %d refused as recorded, %d not' % (d, same, differ, refused, notrefused))
    bad += differ + notrefused
sys.exit(1 if bad else 0)
