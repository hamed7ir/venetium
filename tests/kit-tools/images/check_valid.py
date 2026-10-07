"""Development check of the VALID test files (not part of the shipped kit): decodes every file of out/valid in the browser under test
through a scratch page (mkprobe.py) driven by probe.js, and compares size, exact checksum and the coarse grid with the generator's data.
Run after gen_valid.py:  python check_valid.py
  IMG_OUT        the generator output folder (default: out/ beside this script)
  NODE           node executable (default: node on the PATH; the record's is third_party/node/win/node.exe of the Chromium tree)
  VENETIUM_EXE   the browser under test (read by probe.js)
Scratch files (pv.html, pv.json, pv.png) go to <IMG_OUT>/probe/.
"""
import json, subprocess, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ.get('IMG_OUT') or os.path.join(HERE, 'out')
NODE = os.environ.get('NODE') or 'node'
PROBE = os.path.join(OUT, 'probe'); os.makedirs(PROBE, exist_ok=True)
valid = json.load(open(os.path.join(OUT, 'valid.json')))
files = [os.path.join(OUT, 'valid', v['id'] + '.' + v['fmt']) for v in valid]
subprocess.check_call([sys.executable, os.path.join(HERE, 'mkprobe.py'), os.path.join(PROBE, 'pv.html')] + files)
subprocess.check_call([NODE, os.path.join(HERE, 'probe.js'), os.path.join(PROBE, 'pv.html'), os.path.join(PROBE, 'pv')], stdout=subprocess.DEVNULL)
res = json.load(open(os.path.join(PROBE, 'pv.json')))
bad = 0
for v in valid:
    r = res.get(v['id'] + '.' + v['fmt'])
    if not r: print('MISSING', v['id']); bad += 1; continue
    msg = []
    if r.get('ev') != 'load': msg.append('ev=' + str(r.get('ev')))
    if (r.get('w'), r.get('h')) != (v['w'], v['h']): msg.append('size %sx%s want %sx%s' % (r.get('w'), r.get('h'), v['w'], v['h']))
    if v['exact'] and r.get('sum') != v['exact']: msg.append('sum %s want %s' % (r.get('sum'), v['exact']))
    if v['ref']:
        mx = max(abs(a - b) for ga, gb in zip(r['grid'], v['ref']['grid']) for a, b in zip(ga, gb))
        msg.append('grid max diff %d (tol %d)%s' % (mx, v['ref']['tol'], '' if mx <= v['ref']['tol'] else ' **FAIL**'))
        if mx > v['ref']['tol']: bad += 1
    print('%-24s %-8s sum %-9s %s' % (v['id'], r.get('decode'), r.get('sum'), '; '.join(msg)))
    if v['exact'] and r.get('sum') != v['exact']: bad += 1
print('bad', bad)
