#!/usr/bin/env python3
# Compare a run-cases.cmd output folder with the recorded answers, on the Windows box itself:
# each <case>.out against tests/cases/<case>.expected (line endings dropped), each <case>.err
# against the text of tests/cases/<case>.error. What verify-three does on the Mac, done where the
# outputs are.
#
# Every case is counted from tests/cases, not from what the folder holds: a case with a .expected
# and no .out failed to compile (its .build is there) or never ran, and both are named and fail the
# pass - "0 differ" over the cases that happened to run is no verdict. A case run-cases.cmd skips
# by its .notarget (x86_64-windows), or by its .nomasm in a masm folder, is counted as skipped.
#
#   python compare-cases.py <tree root> <winout folder> [<winout folder> ...]
import os, sys

root = sys.argv[1]
cases = os.path.join(root, 'tests', 'cases')

def skipped(n, masm):
    t = os.path.join(cases, n + '.notarget')
    if os.path.exists(t) and 'x86_64-windows' in open(t, encoding='latin-1').read(): return True
    return masm and os.path.exists(os.path.join(cases, n + '.nomasm'))

def read(p): return open(p, encoding='latin-1').read()

bad = 0
for d in sys.argv[2:]:
    if not os.path.isdir(d): print('%s: not run yet' % d); continue   # a spelling whose jobs have not started
    masm = os.path.basename(os.path.normpath(d)).lower() == 'masm'
    same = differ = refused = notrefused = failed = notrun = skip = 0
    for f in sorted(os.listdir(cases)):
        n, ext = os.path.splitext(f)
        if ext not in ('.expected', '.error'): continue
        if skipped(n, masm): skip += 1; continue
        if ext == '.expected':
            out = os.path.join(d, n + '.out')
            if not os.path.exists(out):
                if os.path.exists(os.path.join(d, n + '.build')): failed += 1; print('  compile-failed ' + n)
                else: notrun += 1; print('  not-run ' + n)
                continue
            if read(out).replace('\r', '') == read(os.path.join(cases, f)): same += 1
            else: differ += 1; print('  differ ' + n)
        else:
            err = os.path.join(d, n + '.err')
            if not os.path.exists(err): notrun += 1; print('  not-run ' + n); continue
            if read(os.path.join(cases, f)).strip() in read(err).replace('\r', ''): refused += 1
            else: notrefused += 1; print('  refusal ' + n)
    print('%s: %d same, %d differ; %d refused as recorded, %d not; %d compile-failed, %d not run, %d skipped'
          % (d, same, differ, refused, notrefused, failed, notrun, skip))
    bad += differ + notrefused + failed + notrun
sys.exit(1 if bad else 0)
