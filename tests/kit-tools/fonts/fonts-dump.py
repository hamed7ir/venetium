# Step 1 of 2 for venetium\tests\kit\data\fonts-strings.js (page fonts.html). Not part of the kit; it only documents how the data was made.
#   python fonts-dump.py --out <paks-dir> [--build F:/cr/src/out/x86-rt21] [--src F:/cr/src]     (python 3)
# Extracts the text messages of every built GRIT pak <build>/gen/chrome/branded_strings_<locale>.pak into <paks-dir>/<locale>/<IDS_NAME>
# with the tree's own tools/grit/pak_util.py (extract -t; sha256 of the script used:
# 4323d43825fe59fc950e7f19922b08e487643c662b0bf7c921b36be877e0d3ed). <paks-dir> is what fonts-gen.py --paks reads. The _FEMININE / _MASCULINE /
# _NEUTER gender variants are skipped. Reads the build and the source tree only; writes only under --out (PYTHONDONTWRITEBYTECODE=1 for the child).
import os, sys, subprocess, glob, json, argparse

ap = argparse.ArgumentParser(description='dump the branded_strings paks of a build (input of fonts-gen.py)')
ap.add_argument('--out', required=True, help='folder that receives one sub-folder per locale (created)')
ap.add_argument('--build', default='F:/cr/src/out/x86-rt21', help='build output folder (default: the x86-rt21 build)')
ap.add_argument('--src', default='F:/cr/src', help='source tree that holds tools/grit/pak_util.py')
a = ap.parse_args()
B = a.build.replace('\\', '/').rstrip('/') + '/gen/chrome'
OUT = a.out.replace('\\', '/').rstrip('/')
env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
res = {}
for p in sorted(glob.glob(B + '/branded_strings_*.pak')):
    n = os.path.basename(p)[len('branded_strings_'):-4]
    if n.endswith(('_FEMININE', '_MASCULINE', '_NEUTER')):
        continue
    tmp = f'{OUT}/{n}'
    os.makedirs(tmp, exist_ok=True)
    r = subprocess.run([sys.executable, f'{a.src}/tools/grit/pak_util.py', 'extract', '-t', '-o', tmp, p], capture_output=True, text=True, env=env)
    if r.returncode:
        print(n, 'ERR', r.stderr[:200])
        continue
    res[n] = len(os.listdir(tmp))
print(json.dumps(res))
print('locales dumped:', len(res))
