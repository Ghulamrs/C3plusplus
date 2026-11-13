# tools/windows/lowering-why-match.py <EXCLUSIONS.md> <why.txt>: each reason of the ```owed block, and whether a case's outside line matches it
# (<...> as any text, a line given to its most specific reason); prints "reached <n> <first case>" or "not reached", then the outside lines no listed reason matched.
import re, sys
doc = open(sys.argv[1], encoding='utf-8').read().split('\n')
i = doc.index('```owed') + 1
owed = []
while doc[i] != '```': owed.append(doc[i]); i += 1
rx = [(o, re.compile('^' + '.*'.join(re.escape(p) for p in o.split('<...>')) + '$', re.S)) for o in owed]
rows = [l.rstrip('\n').split('\t', 1) for l in open(sys.argv[2], encoding='utf-8') if '\t' in l]
hit = {o: [] for o in owed}; unmatched = []
for case, why in rows:
    m = [o for o, r in rx if r.match(why)]
    if m: hit[max(m, key=lambda o: len(o.replace('<...>', '')))].append(case)   # the most specific reason
    if not m: unmatched.append(case + '\t' + why)
n = sum(1 for o in owed if hit[o])
print(f'owed reasons {len(owed)}; reached {n}; not reached {len(owed) - n}; outside lines {len(rows)}')
for o in owed:
    cs = sorted(set(hit[o]))
    print(('reached %d %s' % (len(cs), cs[0]) if cs else 'not reached') + '\t' + o)
for u in sorted(set(unmatched)): print('unmatched\t' + u)
