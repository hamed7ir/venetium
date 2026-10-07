"""Builds the VALID test files of images.html (own generated lossless files + copies of tree files) into out/valid/ and writes
out/valid.json (metadata). Expected values come from the generator's own pixel arrays (lossless) or from independent reference
images (lossy: the source image the encoder was given: libjpeg-turbo's testorig.ppm, libwebp's test_ref.ppm, skia's PNG sources).
Run: python gen_valid.py
"""
import os, json, shutil, struct, zlib, hashlib, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import imgtools as T

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ.get('IMG_OUT') or os.path.join(HERE, 'out'); VAL = os.path.join(OUT, 'valid')
TREE = os.environ.get('IMG_TREE', 'F:/cr/src').replace(chr(92), '/').rstrip('/') + '/'   # the Chromium tree the tree-sourced test files are copied from
os.makedirs(VAL, exist_ok=True)
files = []   # metadata records

def lcg(seed):
    s = seed
    while True:
        s = (s * 1103515245 + 12345) & 0x7fffffff
        yield (s >> 16) & 255

def pattern(w=64, h=48):
    """opaque RGB test pattern: gradients, a checker, a white diagonal and a noisy 16x16 corner (defeats the PNG filters)."""
    noise = lcg(42)
    pix = []
    for y in range(h):
        row = []
        for x in range(w):
            r = x * 255 // (w - 1); g = y * 255 // (h - 1)
            b = 235 if ((x // 8) + (y // 8)) % 2 == 0 else 40
            if (x + y) % 17 == 0: r, g, b = 255, 255, 255
            if x < 16 and y < 16: r, g, b = next(noise), next(noise), next(noise)
            row.append((r, g, b, 255))
        pix.append(row)
    return pix

def alpha_pattern(pix):
    """same colours, alpha 0 outside a circle (junk colour kept in the file for alpha 0), 255 inside."""
    h = len(pix); w = len(pix[0]); cx, cy, rad = (w - 1) / 2, (h - 1) / 2, 22
    out = []
    for y in range(h):
        out.append([(r, g, b, 255 if (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad else 0) for x, (r, g, b, a) in enumerate(pix[y])])
    return out

def add(id, fmt, mime, data, w, h, exact=None, ref=None, why='', source='generated (gen_valid.py)', note='', pix=None, semi=None):
    path = os.path.join(VAL, id + '.' + fmt)
    open(path, 'wb').write(data)
    rec = dict(id=id, fmt=fmt, type=mime, file='images/decode/valid/' + id + '.' + fmt, w=w, h=h, bytes=len(data),
               sha256=hashlib.sha256(data).hexdigest(), exact=('%08x' % exact) if exact is not None else None,
               ref=ref, why=why, source=source, note=note, semi=semi)
    files.append(rec)
    return rec

def semi_count(pix):
    """pixels with 0 < alpha < 255 (the canvas read-back un-premultiplies them; x86 and ARMv7 NEON round ties differently, so the colour of such a
    pixel is CPU-dependent). Recorded as 'semi' for the files that have an x86 reference (images.html shows their REGRESSION row as info when
    semi > 0); kit-tools/images/alpha_survey.py checks the field against the shipped files and the browser."""
    return sum(1 for row in pix for p in row if 0 < p[3] < 255)

def grid_white(pix, gx=8, gy=8):
    h = len(pix); w = len(pix[0]); out = []
    for j in range(gy):
        y0, y1 = j * h // gy, (j + 1) * h // gy
        for i in range(gx):
            x0, x1 = i * w // gx, (i + 1) * w // gx
            s = [0, 0, 0]; n = 0
            for y in range(y0, y1):
                for x in range(x0, x1):
                    r, g, b, a = pix[y][x]
                    for k, v in enumerate((r, g, b)): s[k] += (v * a + 255 * (255 - a)) / 255
                    n += 1
            out.append([int(v / n + 0.5) for v in s])
    return out

def ppm(p):
    d = open(p, 'rb').read(); parts = d.split(None, 4); w, h = int(parts[1]), int(parts[2]); off = len(d) - w * h * 3; b = d[off:]
    return [[(b[(y * w + x) * 3], b[(y * w + x) * 3 + 1], b[(y * w + x) * 3 + 2], 255) for x in range(w)] for y in range(h)]

# ============================================================================================ own PNG files
P = pattern(); W, H = 64, 48
PA = alpha_pattern(P)
def flat(pix, f): return [[c for p in row for c in f(p)] for row in pix]
def png_ok(png, pix):          # self-check: my own reader must give the same pixels back
    w, h, got, info = T.read_png(png)
    assert [[tuple(p) for p in r] for r in got] == [[tuple(p) for p in r] for r in pix], 'png self-check'
    return png

png = png_ok(T.write_png(W, H, flat(P, lambda p: p[:3]), 2, 8), P)
add('png-rgb8', 'png', 'image/png', png, W, H, T.checksum(P), why='RGB 8-bit, all five scanline filters (None/Sub/Up/Average/Paeth cycle)')
png = png_ok(T.write_png(W, H, flat(P, lambda p: p[:3]), 2, 8, interlace=True), P)
add('png-rgb8-adam7', 'png', 'image/png', png, W, H, T.checksum(P), why='RGB 8-bit, Adam7 interlaced (7 passes)')
png = png_ok(T.write_png(W, H, flat(PA, lambda p: p), 6, 8), PA)
add('png-rgba8', 'png', 'image/png', png, W, H, T.checksum(PA), why='RGBA 8-bit, alpha 0 or 255 (junk colour under alpha 0)')
# 16-bit RGB: sample = (v << 8) | v, so the 8-bit result is exact whether the decoder takes the high byte or rounds
png = T.write_png(W, H, flat(P, lambda p: [(p[0] << 8) | p[0], (p[1] << 8) | p[1], (p[2] << 8) | p[2]]), 2, 16)
add('png-rgb16', 'png', 'image/png', png, W, H, T.checksum(P), why='RGB 16-bit (samples v*257), reduced to 8 bit')
# gray 8
G = [[(((x * 3 + y * 2) * 5) & 255,) * 3 + (255,) for x in range(W)] for y in range(H)]
png = png_ok(T.write_png(W, H, flat(G, lambda p: [p[0]]), 0, 8), G)
add('png-gray8', 'png', 'image/png', png, W, H, T.checksum(G), why='grayscale 8-bit')
GA = [[(p[0], p[0], p[0], 255 if (x // 4 + y // 4) % 3 else 0) for x, p in enumerate(row)] for y, row in enumerate(G)]
png = png_ok(T.write_png(W, H, flat(GA, lambda p: [p[0], p[3]]), 4, 8), GA)
add('png-graya8', 'png', 'image/png', png, W, H, T.checksum(GA), why='grayscale + alpha 8-bit (alpha 0 or 255)')
# palette images
PAL256 = [((i * 37) & 255, (i * 91 + 20) & 255, (i * 53 + 7) & 255) for i in range(200)]
IDX8 = [[(x * 5 + y * 3 + (x * y) // 7) % 200 for x in range(W)] for y in range(H)]
pal_pix = lambda idx, pal, trns=None: [[pal[v] + ((trns[v] if trns and v < len(trns) else 255),) for v in r] for r in idx]
trns8 = [0] + [255] * 199
png = T.write_png(W, H, IDX8, 3, 8, plte=PAL256, trns=trns8)
pp = pal_pix(IDX8, PAL256, trns8); png_ok(png, pp)
add('png-pal8-trns', 'png', 'image/png', png, W, H, T.checksum(pp), why='8-bit palette (200 entries) + tRNS (index 0 transparent)')
PAL16 = [(i * 16, 255 - i * 16, (i * 71) & 255) for i in range(16)]
IDX4 = [[(x // 3 + y // 2 + (x ^ y)) % 16 for x in range(W)] for y in range(H)]
png = T.write_png(W, H, IDX4, 3, 4, plte=PAL16)
pp = pal_pix(IDX4, PAL16); png_ok(png, pp)
add('png-pal4', 'png', 'image/png', png, W, H, T.checksum(pp), why='4-bit palette (16 colours)')
PAL2 = [(250, 240, 10), (20, 40, 200)]
IDX1 = [[((x // 4) + (y // 4)) % 2 for x in range(W)] for y in range(H)]
png = T.write_png(W, H, IDX1, 3, 1, plte=PAL2, interlace=True)
pp = pal_pix(IDX1, PAL2); png_ok(png, pp)
add('png-pal1-adam7', 'png', 'image/png', png, W, H, T.checksum(pp), why='1-bit palette, Adam7 interlaced (sub-byte packing across passes)')
png = T.write_png(W, H, flat(P, lambda p: p[:3]), 2, 8, idat_split=700)
add('png-rgb8-multi-idat', 'png', 'image/png', png, W, H, T.checksum(P), why='RGB 8-bit with the zlib stream split over several IDAT chunks')

# ============================================================================================ own GIF files
GPAL = [((i * 53) & 255, (i * 29 + 100) & 255, (i * 97 + 31) & 255) for i in range(32)]
GIDX = [[(x // 2 + y + ((x * y) >> 5)) % 32 for x in range(W)] for y in range(H)]
gif = T.write_gif(W, H, GPAL, GIDX)
gp = [[GPAL[v] + (255,) for v in r] for r in GIDX]
w_, h_, back = T.read_gif_first(gif); assert back == gp
add('gif-static', 'gif', 'image/gif', gif, W, H, T.checksum(gp), why='GIF89a, 32-colour global palette, LZW code size grows to 12 bits and the table is cleared (3072 pixels)')
gif = T.write_gif(W, H, GPAL, GIDX, interlace=True)
add('gif-interlaced', 'gif', 'image/gif', gif, W, H, T.checksum(gp), why='GIF interlaced (4 passes)')
gp2 = [[(0, 0, 0, 0) if v == 5 else GPAL[v] + (255,) for v in r] for r in GIDX]
gif = T.write_gif(W, H, GPAL, GIDX, transparent=5)
add('gif-transparent', 'gif', 'image/gif', gif, W, H, T.checksum(gp2), why='GIF with a transparent colour index (Graphic Control Extension)')

# ============================================================================================ own BMP files
bmp = T.write_bmp(W, H, P, bpp=24)
add('bmp-24', 'bmp', 'image/bmp', bmp, W, H, T.checksum(P), why='BMP 24-bit bottom-up (BITMAPINFOHEADER)')
bmp = T.write_bmp(W, H, P, bpp=24, topdown=True)
add('bmp-24-topdown', 'bmp', 'image/bmp', bmp, W, H, T.checksum(P), why='BMP 24-bit top-down (negative height)')
bmp = T.write_bmp(W, H, P, bpp=32)
add('bmp-32', 'bmp', 'image/bmp', bmp, W, H, T.checksum(P), why='BMP 32-bit BI_RGB, alpha byte 255')
bmp = T.write_bmp(W, H, bpp=8, palette=PAL256, index_rows=IDX8)
pp8 = [[PAL256[v] + (255,) for v in r] for r in IDX8]
add('bmp-8-palette', 'bmp', 'image/bmp', bmp, W, H, T.checksum(pp8), why='BMP 8-bit palette')
bmp = T.write_bmp(W, H, bpp=4, palette=PAL16, index_rows=IDX4)
pp4 = [[PAL16[v] + (255,) for v in r] for r in IDX4]
add('bmp-4-palette', 'bmp', 'image/bmp', bmp, W, H, T.checksum(pp4), why='BMP 4-bit palette')
RIDX = [[(x // 6 + y // 3) % 7 for x in range(W)] for y in range(H)]
RPAL = [PAL256[i * 9] for i in range(7)]
bmp = T.write_bmp(W, H, bpp=8, palette=RPAL, index_rows=RIDX, rle8=True)
pp = [[RPAL[v] + (255,) for v in r] for r in RIDX]
add('bmp-rle8', 'bmp', 'image/bmp', bmp, W, H, T.checksum(pp), why='BMP 8-bit RLE-compressed (BI_RLE8)')

# ============================================================================================ own ICO files
S32 = 32
P32 = [[(r, g, b, a) for (r, g, b, a) in row[:S32]] for row in PA[:S32]]
P16 = [[PA[y * 3][x * 3] for x in range(16)] for y in range(16)]
dib32 = T.ico_bmp_entry(S32, S32, P32, bpp=32)
ico = T.write_ico([(S32, S32, 32, dib32)])
add('ico-bmp32', 'ico', 'image/x-icon', ico, S32, S32, T.checksum(P32), why='ICO, one 32x32 32-bit BMP entry with alpha + AND mask')
IPAL = PAL256[:16]
IIDX = [[(x // 2 + y) % 16 for x in range(S32)] for y in range(S32)]
Ipix = [[IPAL[IIDX[y][x]] + ((0 if (x // 4 + y // 4) % 5 == 0 else 255),) for x in range(S32)] for y in range(S32)]
dib4 = T.ico_bmp_entry(S32, S32, Ipix, bpp=4, palette=IPAL, index_rows=IIDX)
ico = T.write_ico([(S32, S32, 4, dib4)])
add('ico-bmp4-mask', 'ico', 'image/x-icon', ico, S32, S32, T.checksum(Ipix), why='ICO, 4-bit palette BMP entry; transparency only through the AND mask')
pngico = T.write_png(S32, S32, flat(P32, lambda p: p), 6, 8)
ico = T.write_ico([(S32, S32, 32, pngico)])
add('ico-png', 'ico', 'image/x-icon', ico, S32, S32, T.checksum(P32), why='ICO with a PNG-compressed 32x32 entry (ICO decoder hands it to the PNG decoder)')
dib16 = T.ico_bmp_entry(16, 16, P16, bpp=32)
ico = T.write_ico([(16, 16, 32, dib16), (S32, S32, 32, pngico)])
add('ico-multi', 'ico', 'image/x-icon', ico, S32, S32, T.checksum(P32), why='ICO with two entries (16x16 BMP, 32x32 PNG): the largest entry is the image')

# ============================================================================================ own lossless WebP files (vp8l.py: written from the spec)
import vp8l as V
def to_argb(rows): return [((a << 24) | (r << 16) | (g << 8) | b) for row in rows for (r, g, b, a) in row]
def crop(rows, w, h): return [row[:w] for row in rows[:h]]
Q = crop(P, 61, 45); QA = crop(PA, 61, 45)
def webp(id, w, h, rows, why, **kw):
    data = V.encode(w, h, to_argb(rows), **kw)
    add(id, 'webp', 'image/webp', data, w, h, T.checksum(rows), why=why, source='generated (gen_valid.py + vp8l.py, an encoder written from the WebP lossless spec)')
webp('webp-ll-literal', 64, 48, P, 'WebP lossless (VP8L), no transforms, plain prefix codes (normal code length codes with run-length tokens)')
cyc = lambda bx, by: (bx * 5 + by * 3 + bx * by) % 14
webp('webp-ll-allmodes', 64, 48, P, 'WebP lossless: subtract-green + predictor transform with 4x4 blocks cycling through all 14 predictor modes',
     transforms=[('subtract_green',), ('predictor', 2, cyc)])
cte = lambda bx, by: ((bx * 37 + by * 11) % 61 - 30 & 255, (bx * 13 + by * 29) % 71 - 35 & 255, (bx * 7 + by * 19) % 53 - 26 & 255)
webp('webp-ll-transforms', 61, 45, Q, 'WebP lossless 61x45 (not a multiple of the block size): subtract-green + predictor (8x8 blocks, all modes) + cross-colour transform',
     transforms=[('subtract_green',), ('predictor', 3, cyc), ('cross_color', 3, cte)])
webp('webp-ll-cache-lz77-meta', 61, 45, QA, 'WebP lossless with alpha: predictor + cross-colour, colour cache (6 bits), LZ77 backward references, meta prefix image with 3 prefix-code groups',
     transforms=[('predictor', 4, cyc), ('cross_color', 4, cte)], cache_bits=6, lz77=True, meta_bits=3, group_of_block=lambda bx, by: (bx + 2 * by) % 3, ngroups=3)
webp('webp-ll-cache11-lz77', 64, 48, P, 'WebP lossless: colour cache of 2048 entries (11 bits) + LZ77 (distance map codes and linear distances), no transforms',
     cache_bits=11, lz77=True)
# palettes
pal16 = [(PAL16[v] + (255,)) for v in range(16)]
rows16 = [[PAL16[v] + (255,) for v in r] for r in IDX4]
argbpal = lambda pal: [((a << 24) | (r << 16) | (g << 8) | b) for (r, g, b, a) in pal]
webp('webp-ll-palette16', 64, 48, rows16, 'WebP lossless, colour indexing with 16 colours (2 pixels bundled per byte)', transforms=[('palette', argbpal(pal16))])
pal4 = [(250, 30, 30, 255), (30, 250, 30, 255), (30, 30, 250, 255), (250, 250, 30, 255)]
IDX2 = [[(x // 5 + y // 4) % 4 for x in range(61)] for y in range(45)]
rows4 = [[pal4[v] for v in r] for r in IDX2]
webp('webp-ll-palette4', 61, 45, rows4, 'WebP lossless, colour indexing with 4 colours on a 61-pixel-wide image (4 pixels bundled, partial last byte)', transforms=[('palette', argbpal(pal4))])
pal2 = [(0, 0, 0, 0), (20, 40, 200, 255)]
rows2 = [[pal2[((x // 3) + (y // 3)) % 2] for x in range(61)] for y in range(45)]
webp('webp-ll-palette2-alpha', 61, 45, rows2, 'WebP lossless, 2 colours (one transparent): 8 pixels bundled per byte, with alpha', transforms=[('palette', argbpal(pal2))])
pal200 = [(PAL256[i] + (255,)) for i in range(200)]
rows200 = [[pal200[v] for v in r] for r in IDX8]
webp('webp-ll-palette200-predict', 64, 48, rows200, 'WebP lossless, colour indexing with 200 colours (no bundling) followed by a predictor transform on the index image',
     transforms=[('palette', argbpal(pal200)), ('predictor', 3, cyc)], cache_bits=4)

# animated WebP (VP8X + ANIM + ANMF frames, each frame a VP8L bitstream made by vp8l.py): the canvas shows frame 0
inv = [[(255 - r, 255 - g, 255 - b, a) for (r, g, b, a) in row] for row in P]
f0 = V.encode(64, 48, to_argb(P), transforms=[('subtract_green',), ('predictor', 3, cyc)], raw=True)
f1 = V.encode(64, 48, to_argb(inv), transforms=[('subtract_green',), ('predictor', 3, cyc)], raw=True)
anim = V.animated(64, 48, [(0, 0, 64, 48, f0, 100, 0), (0, 0, 64, 48, f1, 100, 0)], loops=0)
add('webp-ll-anim', 'webp', 'image/webp', anim, 64, 48, T.checksum(P), why='animated WebP (VP8X, ANIM, 2 ANMF frames of lossless data): the first frame is shown',
    source='generated (gen_valid.py + vp8l.py)')
# animated GIF / APNG from Blink's test data: first frame (independent: my GIF reader; the APNG's reference PNG)
BT = TREE + 'third_party/blink/renderer/platform/testing/data/'
gd = open(BT + 'green-red-blue-yellow-animated.gif', 'rb').read()
gw_, gh_, gpix = T.read_gif_first(gd)
add('gif-anim-3frames', 'gif', 'image/gif', gd, gw_, gh_, T.checksum(gpix), why='animated GIF, 4 frames (green, red, blue, yellow): the first frame is shown',
    source='copy of third_party/blink/renderer/platform/testing/data/green-red-blue-yellow-animated.gif; expected = my GIF reader, first frame')
ad = open(BT + 'apng00.png', 'rb').read()
aw_, ah_, apix, _ = T.read_png(open(BT + 'apng00-ref.png', 'rb').read())
add('apng-anim', 'png', 'image/png', ad, aw_, ah_, T.checksum(apix), why='animated PNG (acTL / fcTL / fdAT): the default image is shown (Rust png crate APNG path)',
    source='copy of third_party/blink/renderer/platform/testing/data/apng00.png; expected = apng00-ref.png (the reference rendering Blink ships with it), read with my PNG reader')

# ============================================================================================ tree files with exact references
S = TREE + 'third_party/skia/resources/images/'
rp_png = open(S + 'randPixels.png', 'rb').read()
rw, rh, rpix, _ = T.read_png(rp_png)
for ext, mime, why in [('png', 'image/png', 'Skia test image: 8x8 random RGB pixels (the reference)'),
                       ('gif', 'image/gif', 'same 8x8 random pixels as GIF'),
                       ('bmp', 'image/bmp', 'same 8x8 random pixels as BMP'),
                       ('webp', 'image/webp', 'same 8x8 random pixels as lossless WebP (VP8L)')]:
    d = open(S + 'randPixels.' + ext, 'rb').read()
    add('rand-' + ext, ext, mime, d, rw, rh, T.checksum(rpix), why=why, source='copy of third_party/skia/resources/images/randPixels.' + ext + ' (BSD-3, Skia); expected checksum = my PNG reader on randPixels.png')
# the two real-world sized PNG/GIF/ICO sources with alpha (Skia color wheel): exact for png (own reader), coarse for gif/ico/webp/avif
cw = T.read_png(open(S + 'color_wheel.png', 'rb').read())[2]
cwref = grid_white(cw)
add('wheel-png', 'png', 'image/png', open(S + 'color_wheel.png', 'rb').read(), 128, 128, T.checksum(cw), why='Skia colour wheel PNG 128x128 RGBA with anti-aliased alpha edges (exact checksum would need un-premultiply rounding: coarse reference used)', source='copy of third_party/skia/resources/images/color_wheel.png', ref=dict(grid=cwref, tol=4))
cw_semi = semi_count(cw); assert cw_semi > 0
files[-1]['semi'] = cw_semi   # host count from my PNG reader
files[-1]['exact'] = None   # semi-transparent pixels: the canvas un-premultiplies, so the exact value is not independent of the decoder
files[-1]['exactOp'] = '%08x' % T.checksum_op(cw)   # but the opaque pixels and the alpha channel are exact
for ext, mime, why in [('gif', 'image/gif', 'Skia colour wheel as GIF (palette, transparent index)'),
                       ('webp', 'image/webp', 'Skia colour wheel as lossless WebP (VP8L, real encoder: transforms, colour cache, LZ77)'),
                       ('avif', 'image/avif', 'Skia colour wheel as AVIF (AV1 + alpha plane)')]:
    d = open(S + 'color_wheel.' + ext, 'rb').read()
    # gif: counted from the file by my GIF reader (palette transparency is 0 or 255 only). webp: lossless, the same pixels as the PNG (the page checks that
    # opaque pixels and alpha equal the PNG's). avif: an AV1 alpha plane made from the same picture (lossy: the decoded count differs a little, the page only needs > 0).
    semi = semi_count(T.read_gif_first(d)[2]) if ext == 'gif' else cw_semi
    add('wheel-' + ext, ext, mime, d, 128, 128, None, ref=dict(grid=cwref, tol=6 if ext in ('gif', 'avif') else 4), why=why, semi=semi, source='copy of third_party/skia/resources/images/color_wheel.' + ext + ' (BSD-3, Skia); coarse reference = my PNG reader on color_wheel.png')
    if ext == 'webp':
        files[-1]['exactOp'] = '%08x' % T.checksum_op(cw)   # lossless: opaque pixels and alpha must equal the PNG's

# ============================================================================================ JPEG (libjpeg-turbo test images; reference = testorig.ppm, the encoder's input)
J = TREE + 'third_party/libjpeg_turbo/testimages/'
orig = ppm(J + 'testorig.ppm'); ogrid = grid_white(orig)
def gray_of(pix):   # JPEG luma (ITU-R BT.601) of the original
    return [[(lambda y: (y, y, y, 255))(int(0.299 * r + 0.587 * g + 0.114 * b + 0.5)) for (r, g, b, a) in row] for row in pix]
ggrid = grid_white(gray_of(orig))
for name, why, tol, g in [('testorig', 'JPEG baseline 4:2:0 (libjpeg-turbo testorig.jpg)', 4, ogrid),
                          ('testout_444_islow', 'JPEG baseline 4:4:4', 4, ogrid),
                          ('testout_422_ifast_opt', 'JPEG 4:2:2 (h2v1), optimised Huffman tables', 4, ogrid),
                          ('testout_420_islow_prog', 'JPEG progressive 4:2:0 (spectral selection + successive approximation)', 4, ogrid),
                          ('testout_3x2_ifast_prog', 'JPEG progressive, 3x2 sampling factors (generic upsampling path)', 5, ogrid),
                          ('testout_gray_islow', 'JPEG grayscale', 5, ggrid)]:
    d = open(J + name + '.jpg', 'rb').read()
    add('jpeg-' + name.replace('testout_', '').replace('testorig', 'baseline-420').replace('_', '-'), 'jpg', 'image/jpeg', d, 227, 149, None, ref=dict(grid=g, tol=tol), why=why, semi=0,   # JPEG has no alpha channel
        source='copy of third_party/libjpeg_turbo/testimages/' + name + '.jpg (IJG/BSD-3); coarse reference = testorig.ppm, the image it was encoded from')

# ============================================================================================ WebP lossy and AVIF
d = open(TREE + 'third_party/libwebp/src/examples/test.webp', 'rb').read()
add('webp-lossy', 'webp', 'image/webp', d, 128, 128, None, ref=dict(grid=grid_white(ppm(TREE + 'third_party/libwebp/src/examples/test_ref.ppm')), tol=4), semi=0,   # VP8 chunk only, no ALPH
    why='WebP lossy (VP8) 128x128, libwebp examples/test.webp', source='copy of third_party/libwebp/src/examples/test.webp (BSD-3, Google); coarse reference = examples/test_ref.ppm (dwebp output committed with libwebp)')
C = TREE + 'third_party/crabbyavif/src/tests/data/'
sc = T.read_png(open(C + 'sacre_coeur.png', 'rb').read())[2]
add('avif-8bit', 'avif', 'image/avif', open(C + 'sacre_coeur_2extents.avif', 'rb').read(), 64, 64, None, ref=dict(grid=grid_white(sc), tol=4), semi=0,   # no alpha auxiliary item in the file
    why='AVIF 8-bit 64x64 (AV1 item stored in two extents)', source='copy of third_party/crabbyavif/src/tests/data/sacre_coeur_2extents.avif (BSD-2, Google); coarse reference = sacre_coeur.png beside it')
pa = T.read_png(open(C + 'paris_icc_exif_xmp.png', 'rb').read())[2]
add('avif-10bit', 'avif', 'image/avif', open(C + 'paris_10bpc.avif', 'rb').read(), 403, 302, None, ref=dict(grid=grid_white(pa), tol=5), semi=0,   # no alpha auxiliary item in the file
    why='AVIF 10-bit 403x302 (dav1d high-bit-depth path)', source='copy of third_party/crabbyavif/src/tests/data/paris_10bpc.avif; coarse reference = paris_icc_exif_xmp.png beside it')
add('avif-8bit-icc', 'avif', 'image/avif', open(C + 'paris_icc_exif_xmp.avif', 'rb').read(), 403, 302, None, ref=dict(grid=grid_white(pa), tol=5), semi=0,   # no alpha auxiliary item in the file
    why='AVIF 8-bit 403x302 with ICC profile, Exif and XMP', source='copy of third_party/crabbyavif/src/tests/data/paris_icc_exif_xmp.avif; coarse reference = paris_icc_exif_xmp.png beside it')

json.dump(files, open(os.path.join(OUT, 'valid.json'), 'w'), indent=1)
print(len(files), 'valid files,', sum(f['bytes'] for f in files), 'bytes')
for f in files: print('%-26s %-5s %5d bytes %s' % (f['id'], f['fmt'], f['bytes'], 'exact ' + f['exact'] if f['exact'] else 'ref'))
