# tools/windows/lowering-why.py <tree> <out>: every tests/cases and tests/open case compiled -S with CXX1_SSA_WHY set (4 at a time, under
# the caller's affinity); writes "<case>\t<reason>" for each "outside" line, and "<case>\tERR <first line>" for a refusal.
import os, sys, subprocess, concurrent.futures as cf
tree, out = sys.argv[1], sys.argv[2]
exe = os.path.join(tree, 'cxx3-msvc.exe')
env = dict(os.environ, CXX1_SSA_WHY='1')
jobs = []
for d in ('tests/cases', 'tests/open'):
    for f in sorted(os.listdir(os.path.join(tree, d))):
        if f.endswith('.cpp') and not f.endswith('.part.cpp'): jobs.append(os.path.join(tree, d, f))
tmp = os.path.join(tree, 'why-s'); os.makedirs(tmp, exist_ok=True)
def one(p):
    name = os.path.relpath(p, tree).replace('\\', '/')
    try:
        r = subprocess.run([exe, '-S', '-o', os.path.join(tmp, os.path.basename(p) + '.s'), p], env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, errors='replace', timeout=120, cwd=os.path.dirname(p))
    except subprocess.TimeoutExpired:
        return [name + '\tTIMEOUT']
    rows = []
    for l in (r.stderr or '').splitlines():
        if l.startswith('outside '): rows.append(name + '\t' + l.split(': ', 1)[1] if ': ' in l else name + '\t' + l)
    return rows
with cf.ThreadPoolExecutor(4) as ex, open(out, 'w', encoding='utf-8') as o:
    for rows in ex.map(one, jobs):
        for r in rows: o.write(r + '\n')
print('cases', len(jobs))
