#!/usr/bin/env python3
# release-check on the Windows box: the C6000 does not run on the Mac. The sample is the list in
# tools/c6747/release-check-sample.txt; cxx3, asm6x and vm6747 are the box's own; release-check.cmd runs TI's
# assembler, dis6x, the linker and the C6747 simulator; the comparison happens here. The gate is TI's simulator
# against .expected - and where VM6747 matches .expected and the simulator does not, the emulator hid it.
#
#   python release-check-box.py <tree root> <cxx3.exe> <asm6x.exe> [<vm6747.exe>]
import os, re, shutil, subprocess, sys, time

root, cxx, asm = sys.argv[1], sys.argv[2], sys.argv[3]
vm = sys.argv[4] if len(sys.argv) > 4 else None
t0 = time.time()
listing = open(os.path.join(root, 'tools', 'c6747', 'release-check-sample.txt'), encoding='utf-8').read()
sample = [l.split('|') for l in listing.split('\n') if l.count('|') == 2 and not l.startswith('#')]
cases = os.path.join(root, 'tests', 'cases')
for f in sorted(os.listdir(cases)):          # D10: the lambda cases the C6000 does not refuse
    if f.startswith('lambda') and f.endswith('.expected'):
        n = f[:-len('.expected')]
        nt = os.path.join(cases, n + '.notarget')
        if os.path.exists(nt) and any(l.startswith('tms6747') for l in open(nt, encoding='utf-8')):
            continue
        sample.append([n, 'tests/cases/' + n + '.cpp', 'tests/cases/' + f])
levels = ['O1', 'O2', 'Os']

work = os.path.join(root, 'release-check-box')
shutil.rmtree(work, ignore_errors=True)
rc = os.path.join(work, 'rc')
os.makedirs(rc)
for f in os.listdir(cases):
    if f.endswith('.h'): shutil.copy(os.path.join(cases, f), work)
for f in ['runca.js', 'par-one.cmd', 'c6747ca-windows.ccxml', 'C6747.cmd', 'release-check.cmd']:
    shutil.copy(os.path.join(root, 'tools', 'c6747', f), rc)

names, builderr = [], 0
print('release-check-box: building the sample (cxx3 + asm6x on this box), %d programs x %s' % (len(sample), ' '.join(levels)))
for n, src, exp in sample:
    shutil.copy(os.path.join(root, src), os.path.join(work, n + '.cpp'))
    ok = True
    for L in levels:
        s = os.path.join(rc, '%s.%s.s' % (n, L))
        a = subprocess.run([cxx, '-arch', 'tms6747', '-nologo', '-' + L, '-S', os.path.join(work, n + '.cpp'), '-o', s],
                           capture_output=True, text=True)
        b = a.returncode == 0 and subprocess.run([asm, '--no_compress', s, '-o', os.path.join(rc, '%s.%s.asm6x.obj' % (n, L))],
                                                 capture_output=True, text=True)
        if a.returncode != 0 or b.returncode != 0:
            print('release-check-box: did not build %s at -%s:' % (n, L))
            print('      ' + (a.stdout + a.stderr + (b.stdout + b.stderr if b else '')).strip()[:300])
            ok = False
    if ok: names.append((n, exp))
    else: builderr += 1
    if ok and vm:       # the emulator's own answer, beside, at the run level
        e = subprocess.run([vm, os.path.join(rc, '%s.O2.s' % n)], capture_output=True, text=True, stdin=subprocess.DEVNULL)
        open(os.path.join(work, n + '.vm'), 'w').write((e.stdout + e.stderr).replace('\r', ''))

print('release-check-box: TI assembler, dis6x, linker and C6747 simulator')
subprocess.run(['cmd', '/c', os.path.join(rc, 'release-check.cmd')], cwd=rc)
back = os.path.join(rc, 'out')
if not os.path.exists(os.path.join(back, 'release-check.done')):
    print('release-check-box: the TI half did not finish'); sys.exit(1)

def read(p):
    try: return open(p, encoding='latin-1').read().replace('\r', '')
    except OSError: return None

sys.path.insert(0, os.path.join(root, 'tools', 'c6747'))
norm = os.path.join(root, 'tools', 'c6747', 'norm-dis.py')
def normalised(p):
    return subprocess.run([sys.executable, norm, p], capture_output=True, text=True).stdout

print('\n1. asm6x vs TI 8.2.2 encoding - advisory:')
encdiff = 0
for n, _ in names:
    row = []
    for L in levels:
        ti, a6 = os.path.join(back, '%s.%s.ti.dis' % (n, L)), os.path.join(back, '%s.%s.asm6x.dis' % (n, L))
        t, a = read(ti), read(a6)
        if t is None or a is None or t.startswith('TI-ASM-FAILED') or a.startswith('NO-ASM6X-OBJ'):
            row.append('ERR'); builderr += 1; continue
        if normalised(ti) == normalised(a6): row.append(' ok')
        else: row.append('DIFF'); encdiff += 1
    print('   %-34s %s' % (n, ' '.join(row)))

print('\n2. TI C6747 simulator vs .expected - the gate (run at -O2); VM6747 beside it:')
runfail = hidden = 0
for n, exp in names:
    r = read(os.path.join(back, n + '.result'))
    if r is None: print('   %-34s NO RESULT' % n); runfail += 1; continue
    f = dict(kv.split('=', 1) for kv in r.split() if '=' in kv)
    if 'link' in f and f['link'] == 'FAILED': print('   %-34s LINK FAILED' % n); runfail += 1; continue
    if 'count' not in f: print('   %-34s SIMULATOR DID NOT RUN' % n); runfail += 1; continue
    out = read(os.path.join(back, n + '.stdout')) or ''
    want = read(os.path.join(root, exp))
    ok = out == want
    emu = read(os.path.join(work, n + '.vm'))
    emu_ok = '-' if emu is None else ('ok' if emu == want else 'FAIL')
    pc_ok = not f.get('pc') or f.get('pc') == f.get('exit')
    print('   %-34s %-12s sim %-5s emu %-5s %s' % (n, f['count'], 'ok' if ok else 'FAIL', emu_ok, 'ok' if pc_ok else 'pc!=C$$EXIT'))
    if not ok:
        runfail += 1
        print('        expected: %r' % want[:120]); print('        simulator: %r' % out[:120])
        if emu_ok == 'ok': hidden += 1; print('        but VM6747 matches .expected - the emulator is hiding this')
    if not pc_ok: runfail += 1

secs = int(time.time() - t0)
if encdiff: print('note: %d advisory encoding rendering differences (not gating)' % encdiff)
if builderr == 0 and runfail == 0:
    print('release-check-box: PASS in %ds - every sample runs on TI\'s C6747 simulator as its .expected says' % secs)
    sys.exit(0)
print('release-check-box: FAIL in %ds - %d build, %d run; %d a defect the emulator hides' % (secs, builderr, runfail, hidden))
sys.exit(1)
