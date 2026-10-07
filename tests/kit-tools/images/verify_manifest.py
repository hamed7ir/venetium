"""Checks manifest/images.tsv of the kit against the files on disk (STRUCTURAL, no browser needed).
Usage: python verify_manifest.py [kit folder]      (default: the record's kit, ../../kit of this script)
It checks: the header row 'path<TAB>sha256<TAB>source'; LF line endings, UTF-8 without BOM; three columns in every row; no path that
is absolute, has '..' or points outside the kit; no duplicate path; the sha256 of every listed file equals the file on disk; the page
(images.html), its lib file, its two data files and every file under images/decode/ are listed and nothing else is; every base64 record
in data/images-valid.js and data/images-broken.js hashes to the row of its file. Exit code 1 and a list on any problem.
"""
import os, sys, re, json, base64, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
KIT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.normpath(os.path.join(HERE, '..', '..', 'kit'))
problems = []
def bad(m): problems.append(m)
raw = open(os.path.join(KIT, 'manifest', 'images.tsv'), 'rb').read()
if raw.startswith(b'\xef\xbb\xbf'): bad('BOM')
if b'\r' in raw: bad('CR in the manifest')
text = raw.decode('utf-8')
lines = text.split('\n')
if lines[-1] != '': bad('no final LF')
lines = [l for l in lines if l != ''] if lines[-1] == '' else lines
if lines[0] != 'path\tsha256\tsource': bad('header row is %r' % lines[0])
rows = {}
for n, l in enumerate(lines[1:], 2):
    c = l.split('\t')
    if len(c) != 3: bad('line %d has %d columns' % (n, len(c))); continue
    p, h, src = c
    if p.startswith('/') or p.startswith('\\') or re.match(r'^[A-Za-z]:', p) or '..' in p.split('/') or '\\' in p: bad('line %d: path %r is not a clean path inside the kit' % (n, p))
    if not re.fullmatch(r'[0-9a-f]{64}', h): bad('line %d: sha256 %r' % (n, h))
    if not src.strip(): bad('line %d: empty source' % n)
    if p in rows: bad('duplicate path ' + p)
    rows[p] = h
    f = os.path.join(KIT, p)
    if not os.path.isfile(f): bad('listed file missing: ' + p); continue
    if hashlib.sha256(open(f, 'rb').read()).hexdigest() != h: bad('sha256 differs from the disk: ' + p)
owned = {'images.html', 'lib/images-lib.js', 'data/images-valid.js', 'data/images-broken.js'}
for root, _, names in os.walk(os.path.join(KIT, 'images', 'decode')):
    for nme in names:
        owned.add(os.path.relpath(os.path.join(root, nme), KIT).replace(os.sep, '/'))
for p in sorted(owned - set(rows)): bad('not listed: ' + p)
for p in sorted(set(rows) - owned): bad('listed but not an images file: ' + p)
n = 0
for name, var, kind in (('valid', 'IMG_VALID', 'valid'), ('broken', 'IMG_BROKEN', 'broken')):
    s = open(os.path.join(KIT, 'data', 'images-%s.js' % name), encoding='utf-8').read()
    m = re.search(r'window\.%s = (\[.*\]);\s*$' % var, s, re.S)
    if not m: bad('cannot parse data/images-%s.js' % name); continue
    for rec in json.loads(m.group(1)):
        n += 1
        p = 'images/decode/%s/%s.%s' % (kind, rec['id'], rec['fmt'])
        h = hashlib.sha256(base64.b64decode(rec['b64'])).hexdigest()
        if rows.get(p) != h: bad('data record %s does not hash to its manifest row (%s)' % (rec['id'], p))
print('manifest rows: %d, data records: %d, files under images/decode: %d' % (len(rows), n, len([p for p in owned if p.startswith('images/decode/')])))
if problems:
    print('PROBLEMS:'); [print('  ' + p) for p in problems]; sys.exit(1)
print('manifest OK')
