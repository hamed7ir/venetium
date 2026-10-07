"""Survey of the semi-transparent pixels (0 < alpha < 255) of the VALID test files (development tool, not shipped; BATCH-DEVICE-1 follow-up).
Why: the canvas read-back un-premultiplies a semi-transparent pixel, and that rounding differs between x86 (ties to even) and ARMv7 NEON
(ties up; Skia SkSwizzler_opts.inc, pixel_round_as_RP), so a full-RGBA checksum of such a picture is not a stable expectation across CPUs.
images.html therefore judges the 'REGRESSION vs the x86 build' checksum only for files whose record says semi == 0, and shows it as an
info row for files with semi > 0. This tool says which files those are, and checks the 'semi' field of data/images-valid.js against it.

Host side (no browser; decodes the shipped files with the readers of imgtools.py and small container parsers):
  png      imgtools.read_png (default image of an APNG)       gif   imgtools.read_gif_first       jpg   no alpha channel
  bmp      32-bit: the alpha bytes of the pixel array; other depths have no alpha
  ico      PNG entry: read_png; BMP entry: 32-bit alpha bytes (AND mask and palette entries: alpha 0/255 only); the largest entry is the image
  webp     chunk level: VP8 (lossy) has no alpha; VP8L has an alpha_is_used bit; VP8X has an alpha flag and ANMF frames hold VP8L/ALPH data
  avif     alpha auxiliary item (urn:mpeg:mpegB:cicp:systems:auxiliary:alpha) present or not
  Lossless VP8L pixels and AV1 alpha planes are not decoded on the host: the count is None there (the browser column covers them).
Browser side (--browser): a scratch page counts, per file, the pixels with alpha 0, 255 and in between after <img> -> canvas -> getImageData
(alpha is not changed by the un-premultiply, only colour is). Where the host decoded the file, the two counts must agree.

Usage: python alpha_survey.py [--browser] [--kit <kit folder>]
  IMG_OUT        scratch folder (default: out/ beside this script); the probe page and its result go to <IMG_OUT>/probe/
  NODE           node executable (default: node on the PATH)
  VENETIUM_EXE   the browser under test (read by probe.js)
Exit code 1 when the 'semi' field of a record contradicts the survey (a count > 0 where the survey says none or the other way round).
"""
import os, sys, re, json, struct, base64, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import imgtools as T
args = sys.argv[1:]
opt = lambda n, d=None: args[args.index('--' + n) + 1] if '--' + n in args else d
KIT = os.path.abspath(opt('kit', os.path.normpath(os.path.join(HERE, '..', '..', 'kit'))))
OUT = os.environ.get('IMG_OUT') or os.path.join(HERE, 'out')
NODE = os.environ.get('NODE') or 'node'

s = open(os.path.join(KIT, 'data', 'images-valid.js'), encoding='utf-8').read()
recs = json.loads(re.search(r'window\.IMG_VALID = (\[.*\]);\s*$', s, re.S).group(1))

def semi_of_pix(pix): return sum(1 for row in pix for p in row if 0 < p[3] < 255)
def semi_of_alpha(al): return sum(1 for a in al if 0 < a < 255)

def dib_alpha(dib, off, mask_rows):
    """alpha values of a 32-bit DIB (BITMAPINFOHEADER at dib[0:], pixel array at dib[off:]), or None when the bit count has no alpha channel"""
    hs, bw, bh, planes, bpp, comp = struct.unpack('<IiiHHI', dib[:20])
    if bpp != 32: return None
    n = abs(bw) * (abs(bh) // 2 if mask_rows else abs(bh))
    return [dib[off + 4 * i + 3] for i in range(n)]

def host(rec, data):
    """(semi count or None, note)"""
    f = rec['fmt']
    if f == 'jpg': return 0, 'JPEG: no alpha channel'
    if f == 'png':
        _, _, pix, info = T.read_png(data); return semi_of_pix(pix), 'read_png (color type %d)' % info['color']
    if f == 'gif':
        _, _, pix = T.read_gif_first(data); return semi_of_pix(pix), 'read_gif_first (GIF alpha is 0 or 255 by format)'
    if f == 'bmp':
        al = dib_alpha(data[14:], struct.unpack('<I', data[10:14])[0] - 14, False)
        if al is None: return 0, 'BMP, %d bit: no alpha channel' % struct.unpack('<H', data[28:30])[0]
        return semi_of_alpha(al), 'BMP 32 bit: alpha bytes of the pixel array'
    if f == 'ico':
        n, = struct.unpack('<H', data[4:6]); best = None
        for i in range(n):
            w, h, _, _, _, _, size, off = struct.unpack('<BBBBHHII', data[6 + 16 * i:22 + 16 * i])
            w = w or 256
            if best is None or w > best[0]: best = (w, off, size)
        _, off, size = best; e = data[off:off + size]
        if e[:8] == b'\x89PNG\r\n\x1a\n':
            _, _, pix, info = T.read_png(e); return semi_of_pix(pix), 'ICO PNG entry, read_png'
        al = dib_alpha(e, struct.unpack('<I', e[:4])[0], True)
        if al is None: return 0, 'ICO BMP entry without a 32-bit alpha channel (AND mask only: alpha 0 or 255)'
        return semi_of_alpha(al), 'ICO BMP 32-bit entry: alpha bytes'
    if f == 'webp':
        p = 12; has = False; kinds = []
        while p + 8 <= len(data):
            t = data[p:p + 4]; n, = struct.unpack('<I', data[p + 4:p + 8]); body = data[p + 8:p + 8 + n]
            kinds.append(t.decode('latin1').strip())
            if t == b'VP8X' and body[0] & 0x10: has = True
            if t == b'ALPH': has = True
            if t == b'VP8L' and body[0] == 0x2f and (struct.unpack('<I', body[1:5])[0] >> 28) & 1: has = True
            if t == b'ANMF':      # frames: sub-chunks start after the 16-byte frame header
                q = 16
                while q + 8 <= len(body):
                    t2 = body[q:q + 4]; n2, = struct.unpack('<I', body[q + 4:q + 8])
                    if t2 == b'ALPH': has = True
                    if t2 == b'VP8L' and body[q + 8] == 0x2f and (struct.unpack('<I', body[q + 9:q + 13])[0] >> 28) & 1: has = True
                    q += 8 + n2 + (n2 & 1)
            p += 8 + n + (n & 1)
        if not has: return 0, 'WebP chunks %s: no alpha' % '/'.join(kinds)
        return None, 'WebP chunks %s: has alpha (VP8L / ALPH is not decoded on the host)' % '/'.join(kinds)
    if f == 'avif':
        if b'urn:mpeg:mpegB:cicp:systems:auxiliary:alpha' in data or b'urn:mpeg:hevc:2015:auxid:1' in data:
            return None, 'AVIF: alpha auxiliary item present (AV1 alpha plane is not decoded on the host)'
        return 0, 'AVIF: no alpha auxiliary item'
    return None, 'unknown format'

rows = []
for r in recs:
    data = base64.b64decode(r['b64'])
    with open(os.path.join(KIT, 'images', 'decode', 'valid', r['id'] + '.' + r['fmt']), 'rb') as fh: assert fh.read() == data, r['id']
    c, note = host(r, data)
    rows.append(dict(id=r['id'], fmt=r['fmt'], host=c, note=note, field=r.get('semi'), x86=bool(r.get('x86'))))

browser = {}
if '--browser' in args:
    probe_dir = os.path.join(OUT, 'probe'); os.makedirs(probe_dir, exist_ok=True)
    items = [dict(name=r['id'], type=r['type'], b64=r['b64']) for r in recs]
    page = '''<!doctype html><meta charset="utf-8"><title>alpha survey</title><body><script>
const ITEMS = %s;
const bytes = b64 => { const bin = atob(b64), u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i); return u; };
(async () => {
  const res = {};
  for (const it of ITEMS) {
    const url = URL.createObjectURL(new Blob([bytes(it.b64)], { type: it.type })), img = new Image();
    const ev = await new Promise(ok => { img.onload = () => ok('load'); img.onerror = () => ok('error'); setTimeout(() => ok('timeout'), 8000); img.src = url; });
    const r = { ev, w: img.naturalWidth, h: img.naturalHeight, z: 0, f: 0, s: 0 }; res[it.name] = r;
    if (ev !== 'load') continue;
    const c = document.createElement('canvas'); c.width = r.w; c.height = r.h;
    const x = c.getContext('2d', { willReadFrequently: true }); x.drawImage(img, 0, 0);
    const d = x.getImageData(0, 0, r.w, r.h).data;
    for (let i = 3; i < d.length; i += 4) { if (d[i] === 0) r.z++; else if (d[i] === 255) r.f++; else r.s++; }
  }
  window.__probe = res;
})();
</script>''' % json.dumps(items)
    html = os.path.join(probe_dir, 'alpha.html'); open(html, 'w').write(page)
    subprocess.check_call([NODE, os.path.join(HERE, 'probe.js'), html, os.path.join(probe_dir, 'alpha')], stdout=subprocess.DEVNULL)
    browser = json.load(open(os.path.join(probe_dir, 'alpha.json')))

problems = []
print('%-26s %-5s %-9s %-9s %-6s %s' % ('id', 'fmt', 'host', 'browser', 'field', 'note'))
for r in rows:
    b = browser.get(r['id']) if browser else None
    bs = (b['s'] if b['ev'] == 'load' else 'ev=' + b['ev']) if b else '-'
    print('%-26s %-5s %-9s %-9s %-6s %s' % (r['id'], r['fmt'], 'unknown' if r['host'] is None else r['host'], bs, '' if r['field'] is None else r['field'], r['note']))
    if b and b['ev'] != 'load': problems.append('%s did not load in the browser (%s)' % (r['id'], b['ev']))
    if b and r['host'] is not None and b['ev'] == 'load' and b['s'] != r['host']: problems.append('%s: host counted %s semi-transparent pixels, the browser %s' % (r['id'], r['host'], b['s']))
    truth = r['host'] if r['host'] is not None else (b['s'] if b and b['ev'] == 'load' else None)
    if r['field'] is not None:
        if truth is None: problems.append('%s: field semi = %s but the survey has no count (run with --browser)' % (r['id'], r['field']))
        elif (r['field'] > 0) != (truth > 0): problems.append('%s: field semi = %s contradicts the survey (%s)' % (r['id'], r['field'], truth))
    if r['x86'] and r['field'] is None: problems.append('%s has an x86 reference but no semi field' % r['id'])
semis = [r['id'] for r in rows if (r['host'] if r['host'] is not None else ((browser.get(r['id']) or {}).get('s') or 0)) > 0]
print('files with 0 < alpha < 255 pixels (%s): %s' % ('host and browser' if browser else 'host side only; unknown count where the host cannot decode', ', '.join(semis) or 'none'))
unknown = [r['id'] for r in rows if r['host'] is None]
if unknown and not browser: print('host cannot decode (run with --browser): ' + ', '.join(unknown))
if problems:
    print('PROBLEMS:'); [print('  ' + p) for p in problems]; sys.exit(1)
print('survey OK')
