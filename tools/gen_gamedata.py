"""python tools/gen_gamedata.py: writes engine/src/gamedata_tab.h from Petari's GameEventFlagTable.cpp / GameEventValueChecker.cpp (the RMGE01 rows)."""
import re

P = 'D:/smg-ps2/vendor/Petari/src/Game/System/'
src = open(P + 'GameEventFlagTable.cpp', encoding='utf-8').read()
body = src[src.index('sGameEventFlagStatic[] = {') + len('sGameEventFlagStatic[] = {'):src.index('};', src.index('sGameEventFlagStatic[]'))]
rows = re.findall(r'\{(.*?)\}', body, re.S)
out = ['/* written from Petari src/Game/System/GameEventFlagTable.cpp (sGameEventFlagStatic) and GameEventValueChecker.cpp',
       ' * (cGameEventValueTable, RMGE01): {name, type, save flag, condition 1, condition 2, string 1, string 2} */',
       'static const GdFlag gd_flags[] = {']
for r in rows:
    parts = [p.strip() for p in re.split(r',(?=(?:[^"]*"[^"]*")*[^"]*$)', r.replace('\n', ' '))]
    name, typ, save, c1, c2, _8, s1, s2 = parts
    typ = 'GT_' + typ.split('::Type_')[1].upper()
    s1 = 'NULL' if s1 == '0' else s1
    s2 = 'NULL' if s2 == '0' else s2
    out.append(f'    {{{name}, {typ}, {save}, {c1}, {c2}, {s1}, {s2}}},')
out.append('};')
vsrc = open(P + 'GameEventValueChecker.cpp', encoding='utf-8').read()
vbody = vsrc[vsrc.index('cGameEventValueTable[] = {'):vsrc.index('};', vsrc.index('cGameEventValueTable[]'))]
vbody = re.sub(r'#if \(VERSION == RMGK01\).*?#endif', '', vbody, flags=re.S)
vals = re.findall(r'\{("[^"]*"),\s*([^}]*)\}', vbody)
out.append('static const GdValue gd_values[] = {')
for n, v in vals:
    v = v.strip().replace('FindingLuigiEventScheduler::STATE_NULL', '0xFF00')
    out.append(f'    {{{n}, {v}}},')
out.append('};')
open('D:/smg-ps2/engine/src/gamedata_tab.h', 'wb').write(('\n'.join(out) + '\n').encode())
print(len(rows), 'flags', len(vals), 'values')
