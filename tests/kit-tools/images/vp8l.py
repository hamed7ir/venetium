"""A small WebP lossless (VP8L) ENCODER, written from webp-lossless-bitstream-spec.txt (libwebp/src/doc), to make lossless WebP test files
whose expected pixels are known by construction. It is deliberately not an optimising encoder: it exists to put every part of the
bitstream through the decoder — all four transforms (predictor with all 14 modes, cross-colour, subtract-green, colour indexing with pixel
bundling), the colour cache, LZ77 backward references (distance map codes and linear distances), simple and normal prefix codes with
run-length coded code lengths, and a meta prefix (entropy) image with several prefix-code groups.
Pixels are ARGB 32-bit ints, scan-line order.
"""
import struct, heapq

# ---------------------------------------------------------------------------------------------------------------- bits
class BitWriter:
    def __init__(self): self.acc = 0; self.n = 0; self.out = bytearray()
    def put(self, v, nbits):
        assert 0 <= v < (1 << nbits) if nbits else v == 0, (v, nbits)
        self.acc |= v << self.n; self.n += nbits
        while self.n >= 8: self.out.append(self.acc & 255); self.acc >>= 8; self.n -= 8
    def bytes(self):
        o = bytes(self.out)
        if self.n: o += bytes((self.acc & 255,))
        return o

# ---------------------------------------------------------------------------------------------------------------- prefix codes
def code_lengths(freq, limit):
    """Huffman code lengths (0 = unused) limited to `limit` bits. Needs >= 2 used symbols."""
    used = [i for i, f in enumerate(freq) if f > 0]
    assert len(used) >= 2
    f = list(freq)
    while True:
        heap = [(f[i], i, (i,)) for i in used]
        heapq.heapify(heap)
        lens = {i: 0 for i in used}
        uid = len(f)
        while len(heap) > 1:
            a = heapq.heappop(heap); b = heapq.heappop(heap)
            for s in a[2] + b[2]: lens[s] += 1
            heapq.heappush(heap, (a[0] + b[0], uid, a[2] + b[2])); uid += 1
        if max(lens.values()) <= limit: break
        f = [((x + 1) // 2 if x else 0) if x else 0 for x in f]
        f = [max(x, 1) if freq[i] else 0 for i, x in enumerate(f)]
    out = [0] * len(freq)
    for s, l in lens.items(): out[s] = l
    return out

def canonical(lengths):
    """code for each symbol, bit-reversed so that it can be written LSB-first (deflate style)."""
    maxl = max(lengths) if lengths else 0
    codes = [0] * len(lengths)
    code = 0
    for l in range(1, maxl + 1):
        for s, sl in enumerate(lengths):
            if sl == l:
                r = 0
                for k in range(l): r |= ((code >> k) & 1) << (l - 1 - k)
                codes[s] = r; code += 1
        code <<= 1
    return codes

class Prefix:
    """one prefix code over an alphabet: built from symbol frequencies, writes its definition and symbols."""
    def __init__(self, freq, alphabet):
        self.alphabet = alphabet
        used = [i for i, f in enumerate(freq) if f > 0]
        self.single = None
        if len(used) == 0:
            self.single = 0; self.lengths = [0] * alphabet
        elif len(used) == 1:
            self.single = used[0]; self.lengths = [0] * alphabet
        else:
            self.lengths = code_lengths(freq + [0] * (alphabet - len(freq)), 15)
            self.codes = canonical(self.lengths)
    def write_def(self, bw, force_normal=False):
        if self.single is not None:
            s = self.single
            if s < 256 and not force_normal:
                bw.put(1, 1); bw.put(0, 1)                       # simple code, one symbol
                if s < 2: bw.put(0, 1); bw.put(s, 1)             # first symbol in 1 bit
                else: bw.put(1, 1); bw.put(s, 8)
                return
            # a single leaf coded with the normal code length code: its length is 1, bits per symbol are 0
            lengths = [0] * self.alphabet; lengths[s] = 1
            self._write_normal(bw, lengths); return
        used = [i for i, l in enumerate(self.lengths) if l]
        if len(used) == 2 and max(used) < 256 and not force_normal and self.lengths[used[0]] == 1:
            bw.put(1, 1); bw.put(1, 1)                           # simple code, two symbols (both length 1)
            if used[0] < 2: bw.put(0, 1); bw.put(used[0], 1)
            else: bw.put(1, 1); bw.put(used[0], 8)
            bw.put(used[1], 8)
            return
        self._write_normal(bw, self.lengths)
    def _write_normal(self, bw, lengths):
        def tokenize(lens):
            toks = []; prev = 8; i = 0           # tokens: literal lengths, 16 = repeat previous non-zero, 17 / 18 = zero runs
            while i < len(lens):
                v = lens[i]; j = i
                while j < len(lens) and lens[j] == v: j += 1
                run = j - i
                if v == 0:
                    while run >= 3:
                        k = min(run, 138)
                        if k >= 11: toks.append((18, k - 11, 7))
                        else: toks.append((17, k - 3, 3))
                        run -= k
                    for _ in range(run): toks.append((0, 0, 0))
                else:
                    if v != prev: toks.append((v, 0, 0)); prev = v; run -= 1
                    while run >= 3:
                        k = min(run, 6); toks.append((16, k - 3, 2)); run -= k
                    for _ in range(run): toks.append((v, 0, 0))
                i = j
            return toks
        n = len(lengths)
        while n > 1 and lengths[n - 1] == 0: n -= 1            # trailing zeros need not be sent when max_symbol is used
        toks = tokenize(lengths[:n]); use_max = n < len(lengths) and len(toks) >= 2
        if not use_max: toks = tokenize(lengths)
        cf = [0] * 19
        for t in toks: cf[t[0]] += 1
        used = [x for x in range(19) if cf[x]]
        if len(used) == 1:
            cl = [0] * 19; cl[used[0]] = 1; ccodes = None       # a single leaf: zero bits per token
        else:
            cl = code_lengths(cf, 7); ccodes = canonical(cl)
        order = [17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
        num = 19
        while num > 4 and cl[order[num - 1]] == 0: num -= 1
        bw.put(0, 1)                                             # normal code length code
        bw.put(num - 4, 4)
        for k in range(num): bw.put(cl[order[k]], 3)
        if use_max:                                              # max_symbol = the number of tokens (a repeat token counts once)
            ntok = len(toks); nbits = 2
            while ntok - 2 >= (1 << nbits): nbits += 2
            assert nbits <= 16
            bw.put(1, 1); bw.put((nbits - 2) // 2, 3); bw.put(ntok - 2, nbits)
        else:
            bw.put(0, 1)
        for (x, extra, eb) in toks:
            if ccodes is not None: bw.put(ccodes[x], cl[x])
            if eb: bw.put(extra, eb)
    def write(self, bw, sym):
        if self.single is not None: assert sym == self.single; return
        l = self.lengths[sym]; assert l, 'symbol %d has no code' % sym
        bw.put(self.codes[sym], l)

# ---------------------------------------------------------------------------------------------------------------- LZ77 prefix coding
def prefix_encode(v):
    """value >= 1 -> (prefix symbol, extra bits count, extra bits value)"""
    n = v - 1
    if n < 4: return n, 0, 0
    hb = n.bit_length() - 1
    sb = (n >> (hb - 1)) & 1
    eb = hb - 1
    return 2 * hb + sb, eb, n & ((1 << eb) - 1)

DISTANCE_MAP = [(0, 1), (1, 0), (1, 1), (-1, 1), (0, 2), (2, 0), (1, 2), (-1, 2), (2, 1), (-2, 1), (2, 2), (-2, 2), (0, 3), (3, 0), (1, 3), (-1, 3), (3, 1), (-3, 1), (2, 3), (-2, 3), (3, 2),
                (-3, 2), (0, 4), (4, 0), (1, 4), (-1, 4), (4, 1), (-4, 1), (3, 3), (-3, 3), (2, 4), (-2, 4), (4, 2), (-4, 2), (0, 5), (3, 4), (-3, 4), (4, 3), (-4, 3), (5, 0), (1, 5), (-1, 5),
                (5, 1), (-5, 1), (2, 5), (-2, 5), (5, 2), (-5, 2), (4, 4), (-4, 4), (3, 5), (-3, 5), (5, 3), (-5, 3), (0, 6), (6, 0), (1, 6), (-1, 6), (6, 1), (-6, 1), (2, 6), (-2, 6), (6, 2),
                (-6, 2), (4, 5), (-4, 5), (5, 4), (-5, 4), (3, 6), (-3, 6), (6, 3), (-6, 3), (0, 7), (7, 0), (1, 7), (-1, 7), (5, 5), (-5, 5), (7, 1), (-7, 1), (4, 6), (-4, 6), (6, 4), (-6, 4),
                (2, 7), (-2, 7), (7, 2), (-7, 2), (3, 7), (-3, 7), (7, 3), (-7, 3), (5, 6), (-5, 6), (6, 5), (-6, 5), (8, 0), (4, 7), (-4, 7), (7, 4), (-7, 4), (8, 1), (8, 2), (6, 6), (-6, 6),
                (8, 3), (5, 7), (-5, 7), (7, 5), (-7, 5), (8, 4), (6, 7), (-6, 7), (7, 6), (-7, 6), (8, 5), (7, 7), (-7, 7), (8, 6), (8, 7)]
assert len(DISTANCE_MAP) == 120

def distance_code(d, width):
    for i, (xi, yi) in enumerate(DISTANCE_MAP):
        dd = xi + yi * width
        if dd < 1: dd = 1
        if dd == d: return i + 1
    return d + 120

def plane_distance(code, width):
    if code > 120: return code - 120
    xi, yi = DISTANCE_MAP[code - 1]
    d = xi + yi * width
    return d if d >= 1 else 1

# ---------------------------------------------------------------------------------------------------------------- tokens
def cache_key(argb, bits): return ((0x1e35a7bd * argb) & 0xffffffff) >> (32 - bits)

def make_tokens(pix, width, cache_bits, lz77, dist_candidates=None, bad_backref=False):
    """-> list of (kind, a, b, pos): ('lit', argb), ('cache', idx), ('copy', length, dist_code); pos = start pixel index"""
    n = len(pix); toks = []; i = 0
    if bad_backref: toks.append(('copy', 5, distance_code(9, width), 0))      # a copy from before the start of the image: invalid
    cache = [0] * (1 << cache_bits) if cache_bits else None
    cands = dist_candidates or [1, 2, 3, 4, 8, 16, width - 1, width, width + 1, 2 * width, 3 * width, 7 * width + 3]
    cands = [d for d in cands if d >= 1]
    while i < n:
        best = (0, 0)
        if lz77:
            for d in cands:
                if d > i: continue
                k = 0
                while i + k < n and k < 4096 and pix[i + k] == pix[i + k - d]: k += 1
                if k > best[0]: best = (k, d)
        if best[0] >= 3:
            toks.append(('copy', best[0], distance_code(best[1], width), i))
            for k in range(best[0]):
                if cache is not None: cache[cache_key(pix[i + k], cache_bits)] = pix[i + k]
            i += best[0]
            continue
        p = pix[i]
        if cache is not None:
            key = cache_key(p, cache_bits)
            if cache[key] == p and (i % 3) != 1:     # mostly use the cache when it hits (every 3rd hit stays a literal: both paths run)
                toks.append(('cache', key, 0, i))
            else:
                toks.append(('lit', p, 0, i))
            cache[key] = p
        else:
            toks.append(('lit', p, 0, i))
        i += 1
    return toks

# ---------------------------------------------------------------------------------------------------------------- entropy-coded image
def write_image(bw, pix, width, height, level0, cache_bits=0, lz77=False, meta_bits=None, group_of_block=None, ngroups=1, bad_backref=False, bad_cache_bits=None):
    """writes color-cache-info, [meta prefix], the prefix codes and the data of one ARGB image."""
    toks = make_tokens(pix, width, cache_bits, lz77, bad_backref=bad_backref)
    if bad_cache_bits is not None: bw.put(1, 1); bw.put(bad_cache_bits, 4)       # out of range (1..11): invalid
    elif cache_bits: bw.put(1, 1); bw.put(cache_bits, 4)
    else: bw.put(0, 1)
    groups_of_pos = None
    if level0:
        if meta_bits:
            bw.put(1, 1)
            bw.put(meta_bits - 2, 3)
            bwid = (width + (1 << meta_bits) - 1) >> meta_bits; bhei = (height + (1 << meta_bits) - 1) >> meta_bits
            eimg = [0xff000000 | (group_of_block(bx, by) << 8) for by in range(bhei) for bx in range(bwid)]
            assert max((p >> 8) & 0xffff for p in eimg) == ngroups - 1, 'every group must be used (the number of groups is max+1)'
            write_image(bw, eimg, bwid, bhei, False)
            groups_of_pos = lambda pos: group_of_block((pos % width) >> meta_bits, (pos // width) >> meta_bits)
        else:
            bw.put(0, 1)
    ng = ngroups if (level0 and meta_bits) else 1
    cache_size = (1 << cache_bits) if cache_bits else 0
    gal = 256 + 24 + cache_size
    freqs = [[[0] * gal, [0] * 256, [0] * 256, [0] * 256, [0] * 40] for _ in range(ng)]
    for (kind, a, b, pos) in toks:
        g = groups_of_pos(pos) if groups_of_pos else 0
        F = freqs[g]
        if kind == 'lit':
            F[0][(a >> 8) & 255] += 1; F[1][(a >> 16) & 255] += 1; F[2][a & 255] += 1; F[3][(a >> 24) & 255] += 1
        elif kind == 'cache':
            F[0][280 + a] += 1
        else:
            ls, _, _ = prefix_encode(a); F[0][256 + ls] += 1
            ds, _, _ = prefix_encode(b); F[4][ds] += 1
    codes = []
    for g in range(ng):
        F = freqs[g]
        P5 = [Prefix(F[0], gal), Prefix(F[1], 256), Prefix(F[2], 256), Prefix(F[3], 256), Prefix(F[4], 40)]
        for p in P5: p.write_def(bw)
        codes.append(P5)
    for (kind, a, b, pos) in toks:
        g = groups_of_pos(pos) if groups_of_pos else 0
        P = codes[g]
        if kind == 'lit':
            P[0].write(bw, (a >> 8) & 255); P[1].write(bw, (a >> 16) & 255); P[2].write(bw, a & 255); P[3].write(bw, (a >> 24) & 255)
        elif kind == 'cache':
            P[0].write(bw, 280 + a)
        else:
            ls, leb, lev = prefix_encode(a); P[0].write(bw, 256 + ls); bw.put(lev, leb)
            ds, deb, dev = prefix_encode(b); P[4].write(bw, ds); bw.put(dev, deb)
    return toks

# ---------------------------------------------------------------------------------------------------------------- transforms (forward)
A = lambda p: (p >> 24) & 255
R = lambda p: (p >> 16) & 255
G = lambda p: (p >> 8) & 255
B = lambda p: p & 255
def argb(a, r, g, b): return ((a & 255) << 24) | ((r & 255) << 16) | ((g & 255) << 8) | (b & 255)
def avg2(x, y): return argb((A(x) + A(y)) // 2, (R(x) + R(y)) // 2, (G(x) + G(y)) // 2, (B(x) + B(y)) // 2)
def clamp(v): return 0 if v < 0 else 255 if v > 255 else v
def select(L, T, TL):
    pa = A(L) + A(T) - A(TL); pr = R(L) + R(T) - R(TL); pg = G(L) + G(T) - G(TL); pb = B(L) + B(T) - B(TL)
    pL = abs(pa - A(L)) + abs(pr - R(L)) + abs(pg - G(L)) + abs(pb - B(L))
    pT = abs(pa - A(T)) + abs(pr - R(T)) + abs(pg - G(T)) + abs(pb - B(T))
    return L if pL < pT else T
def cas_full(L, T, TL): return argb(*[clamp(f(L) + f(T) - f(TL)) for f in (A, R, G, B)])
def tdiv2(x): return int(x / 2)         # C integer division: truncates toward zero
def cas_half(a, b): return argb(*[clamp(f(a) + tdiv2(f(a) - f(b))) for f in (A, R, G, B)])

def predict(mode, L, T, TR, TL):
    if mode == 0: return 0xff000000
    if mode == 1: return L
    if mode == 2: return T
    if mode == 3: return TR
    if mode == 4: return TL
    if mode == 5: return avg2(avg2(L, TR), T)
    if mode == 6: return avg2(L, TL)
    if mode == 7: return avg2(L, T)
    if mode == 8: return avg2(TL, T)
    if mode == 9: return avg2(T, TR)
    if mode == 10: return avg2(avg2(L, TL), avg2(T, TR))
    if mode == 11: return select(L, T, TL)
    if mode == 12: return cas_full(L, T, TL)
    if mode == 13: return cas_half(avg2(L, T), TL)
    raise ValueError(mode)

def sub_pix(x, y): return argb(A(x) - A(y), R(x) - R(y), G(x) - G(y), B(x) - B(y))

def t_subtract_green(pix, w, h):
    return [argb(A(p), R(p) - G(p), G(p), B(p) - G(p)) for p in pix]

def t_predictor(pix, w, h, size_bits, mode_of_block):
    bw_ = (w + (1 << size_bits) - 1) >> size_bits; bh_ = (h + (1 << size_bits) - 1) >> size_bits
    modes = [mode_of_block(bx, by) for by in range(bh_) for bx in range(bw_)]
    out = [0] * len(pix)
    for y in range(h):
        for x in range(w):
            i = y * w + x
            if y == 0 and x == 0: pred = 0xff000000
            elif y == 0: pred = pix[i - 1]
            elif x == 0: pred = pix[i - w]
            else:
                m = modes[(y >> size_bits) * bw_ + (x >> size_bits)]
                L = pix[i - 1]; T = pix[i - w]; TL = pix[i - w - 1]
                TR = pix[i - w + 1]            # for the rightmost column this index is the first pixel of the current row
                pred = predict(m, L, T, TR, TL)
            out[i] = sub_pix(pix[i], pred)
    sub = [argb(255, 0, m, 0) for m in modes]
    return out, sub, bw_, bh_

def s8(v): return v - 256 if v > 127 else v
def cdelta(t, c): return (s8(t) * s8(c)) >> 5

def t_cross_color(pix, w, h, size_bits, elem_of_block):
    bw_ = (w + (1 << size_bits) - 1) >> size_bits; bh_ = (h + (1 << size_bits) - 1) >> size_bits
    elems = [elem_of_block(bx, by) for by in range(bh_) for bx in range(bw_)]   # (green_to_red, green_to_blue, red_to_blue)
    out = []
    for y in range(h):
        for x in range(w):
            p = pix[y * w + x]; g2r, g2b, r2b = elems[(y >> size_bits) * bw_ + (x >> size_bits)]
            nr = (R(p) - cdelta(g2r, G(p))) & 255
            nb = (B(p) - cdelta(g2b, G(p)) - cdelta(r2b, R(p))) & 255
            out.append(argb(A(p), nr, G(p), nb))
    sub = [argb(255, r2b, g2b, g2r) for (g2r, g2b, r2b) in elems]
    return out, sub, bw_, bh_

def t_palette(pix, w, h, palette):
    n = len(palette); idx = {c: i for i, c in enumerate(palette)}
    wb = 3 if n <= 2 else 2 if n <= 4 else 1 if n <= 16 else 0
    pw = (w + (1 << wb) - 1) >> wb
    bpp = 8 >> wb
    out = []
    for y in range(h):
        for px in range(pw):
            v = 0
            for k in range(1 << wb):
                x = px * (1 << wb) + k
                if x < w: v |= idx[pix[y * w + x]] << (bpp * k)
            out.append(0xff000000 | (v << 8))
    return out, pw, wb

# ---------------------------------------------------------------------------------------------------------------- the encoder
def encode(width, height, pix, transforms=(), cache_bits=0, lz77=False, meta_bits=None, group_of_block=None, ngroups=1, alpha_used=None, sub_lz77=False, bad=None, raw=False):
    """transforms: list in bitstream order, each ('subtract_green',) / ('predictor', size_bits, mode_of_block) / ('cross_color', size_bits, elem_of_block) / ('palette', [argb...])."""
    bw = BitWriter()
    if alpha_used is None: alpha_used = any(A(p) != 255 for p in pix)
    bw.put(0x2f, 8); bw.put(width - 1, 14); bw.put(height - 1, 14); bw.put(1 if alpha_used else 0, 1); bw.put(1 if bad == 'version' else 0, 3)
    cur = list(pix); cw = width
    for t in transforms:
        bw.put(1, 1)
        if t[0] == 'subtract_green':
            bw.put(2, 2); cur = t_subtract_green(cur, cw, height)
        elif t[0] == 'predictor':
            bw.put(0, 2); bw.put(t[1] - 2, 3)
            cur, sub, sbw, sbh = t_predictor(cur, cw, height, t[1], t[2])
            write_image(bw, sub, sbw, sbh, False, lz77=sub_lz77)
        elif t[0] == 'cross_color':
            bw.put(1, 2); bw.put(t[1] - 2, 3)
            cur, sub, sbw, sbh = t_cross_color(cur, cw, height, t[1], t[2])
            write_image(bw, sub, sbw, sbh, False, lz77=sub_lz77)
        elif t[0] == 'palette':
            pal = t[1]; assert 1 <= len(pal) <= 256
            bw.put(3, 2); bw.put(len(pal) - 1, 8)
            deltas = [pal[0]] + [sub_pix(pal[i], pal[i - 1]) for i in range(1, len(pal))]
            write_image(bw, deltas, len(pal), 1, False)
            cur, cw, _ = t_palette(cur, cw, height, pal)
        else: raise ValueError(t)
    bw.put(0, 1)                                                       # no more transforms
    write_image(bw, cur, cw, height, True, cache_bits, lz77, meta_bits, group_of_block, ngroups, bad_backref=(bad == 'backref'), bad_cache_bits=(12 if bad == 'cache12' else None))
    payload = bw.bytes()
    if raw: return payload
    chunk = b'VP8L' + struct.pack('<I', len(payload)) + payload + (b'\x00' if len(payload) & 1 else b'')
    return b'RIFF' + struct.pack('<I', 4 + len(chunk)) + b'WEBP' + chunk


def animated(width, height, frames, loops=0, bgcolor=0xffffffff):
    """extended-format animated WebP. frames: list of (x, y, w, h, vp8l_payload, duration_ms, flags) — x, y even; flags bit0 = dispose to background, bit1 = no blending."""
    def chunk(tag, data): return tag + struct.pack('<I', len(data)) + data + (b'\x00' if len(data) & 1 else b'')
    le24 = lambda v: struct.pack('<I', v)[:3]
    body = chunk(b'VP8X', struct.pack('<B', 0x12) + b'\x00\x00\x00' + le24(width - 1) + le24(height - 1))     # animation + alpha flags
    body += chunk(b'ANIM', struct.pack('<IH', bgcolor, loops))
    for (x, y, w, h, payload, dur, flags) in frames:
        body += chunk(b'ANMF', le24(x // 2) + le24(y // 2) + le24(w - 1) + le24(h - 1) + le24(dur) + bytes((flags,)) + chunk(b'VP8L', payload))
    return b'RIFF' + struct.pack('<I', 4 + len(body)) + b'WEBP' + body
