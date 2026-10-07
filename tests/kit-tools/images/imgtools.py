"""Independent image tools for the kit's images page (BATCH-X86-2 §1, page images.html).
Everything here is written from the format specifications, not from Chromium: a PNG reader and writer (all colour types, 1..16 bit,
Adam7), a GIF writer (own LZW), BMP and ICO writers, and the checksum used by the page.
Pixels are lists of rows of (r, g, b, a) tuples, 8 bits per channel, unpremultiplied.
The page's checksum is FNV-1a 32 over the RGBA bytes of getImageData(): a fully transparent pixel reads back as 0,0,0,0 (canvas stores
premultiplied pixels), so `canon()` zeroes the colour of alpha == 0 pixels before checksumming.
"""
import struct, zlib, math

# ------------------------------------------------------------------------------------------------ checksum
def fnv1a(data: bytes) -> int:
    h = 0x811c9dc5
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xffffffff
    return h

def canon(pix):
    """pix: rows of (r,g,b,a). Canvas view: alpha 0 -> (0,0,0,0)."""
    return [[(0, 0, 0, 0) if p[3] == 0 else p for p in row] for row in pix]

def rgba_bytes(pix) -> bytes:
    out = bytearray()
    for row in canon(pix):
        for r, g, b, a in row:
            out += bytes((r, g, b, a))
    return bytes(out)

def checksum(pix) -> int:
    return fnv1a(rgba_bytes(pix))

def grid(pix, gx, gy):
    """mean colour of each cell of a gx x gy grid (premultiplied-free: alpha-weighted not needed, images used are opaque)."""
    h = len(pix); w = len(pix[0])
    cells = []
    for j in range(gy):
        y0, y1 = j * h // gy, (j + 1) * h // gy
        for i in range(gx):
            x0, x1 = i * w // gx, (i + 1) * w // gx
            n = 0; s = [0, 0, 0]
            for y in range(y0, y1):
                for x in range(x0, x1):
                    r, g, b, a = pix[y][x]
                    s[0] += r; s[1] += g; s[2] += b; n += 1
            cells.append([round(v / n) for v in s])
    return cells

# ------------------------------------------------------------------------------------------------ PNG
def _chunk(t, d):
    c = struct.pack('>I', len(d)) + t + d
    return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)

def _filter_row(ft, row, prev, bpp):
    out = bytearray(len(row))
    for i in range(len(row)):
        a = row[i - bpp] if i >= bpp else 0
        b = prev[i]
        c = prev[i - bpp] if i >= bpp else 0
        x = row[i]
        if ft == 0: v = x
        elif ft == 1: v = x - a
        elif ft == 2: v = x - b
        elif ft == 3: v = x - ((a + b) >> 1)
        else: v = x - _paeth(a, b, c)
        out[i] = v & 255
    return bytes(out)

ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]

def _pack_samples(samples, depth):
    """samples: list of ints, packed MSB first into bytes (rows are byte aligned by the caller)."""
    if depth == 8: return bytes(samples)
    if depth == 16: return b''.join(struct.pack('>H', s) for s in samples)
    out = bytearray(); per = 8 // depth
    for i in range(0, len(samples), per):
        v = 0
        for k in range(per):
            s = samples[i + k] if i + k < len(samples) else 0
            v |= s << (8 - depth * (k + 1))
        out.append(v)
    return bytes(out)

def write_png(width, height, rows, color, depth, plte=None, trns=None, interlace=False, filters='cycle', extra_chunks=(), idat_split=0):
    """rows: list of rows, each a flat list of samples (channels interleaved, in file order) — for palette images the indices.
    color: 0 gray, 2 rgb, 3 palette, 4 gray+alpha, 6 rgba."""
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    bpp = max(1, ch * depth // 8)
    raw = bytearray()
    def emit_pass(prows, ri0):
        prev = bytes(len(_pack_samples(prows[0], depth))) if prows else b''
        for i, r in enumerate(prows):
            packed = _pack_samples(r, depth)
            ft = (ri0 + i) % 5 if filters == 'cycle' else (filters if isinstance(filters, int) else 0)
            raw.append(ft); raw.extend(_filter_row(ft, packed, prev, bpp)); prev = packed
    if not interlace:
        emit_pass(rows, 0)
    else:
        k = 0
        for (x0, y0, dx, dy) in ADAM7:
            prows = []
            for y in range(y0, height, dy):
                r = []
                for x in range(x0, width, dx):
                    r.extend(rows[y][x * ch:(x + 1) * ch])
                prows.append(r)
            if prows and prows[0]:
                emit_pass(prows, k); k += len(prows)
    z = zlib.compress(bytes(raw), 9)
    out = b'\x89PNG\r\n\x1a\n' + _chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, color, 0, 0, 1 if interlace else 0))
    for t, d in extra_chunks: out += _chunk(t, d)
    if plte is not None: out += _chunk(b'PLTE', b''.join(bytes(c) for c in plte))
    if trns is not None: out += _chunk(b'tRNS', bytes(trns))
    if idat_split:
        for i in range(0, len(z), idat_split): out += _chunk(b'IDAT', z[i:i + idat_split])
    else:
        out += _chunk(b'IDAT', z)
    return out + _chunk(b'IEND', b'')

def read_png(data):
    """returns (width, height, rows of (r,g,b,a) 8-bit, info). 16-bit samples are reduced by taking the high byte."""
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    p = 8; chunks = []
    while p < len(data):
        n, = struct.unpack('>I', data[p:p + 4]); t = data[p + 4:p + 8]; d = data[p + 8:p + 8 + n]
        assert zlib.crc32(t + d) & 0xffffffff == struct.unpack('>I', data[p + 8 + n:p + 12 + n])[0], 'bad crc ' + str(t)
        chunks.append((t, d)); p += 12 + n
    ih = [d for t, d in chunks if t == b'IHDR'][0]
    w, h, depth, color, _, _, inter = struct.unpack('>IIBBBBB', ih)
    plte = [d for t, d in chunks if t == b'PLTE']; plte = [tuple(plte[0][i:i + 3]) for i in range(0, len(plte[0]), 3)] if plte else None
    trns = [d for t, d in chunks if t == b'tRNS']; trns = trns[0] if trns else None
    raw = zlib.decompress(b''.join(d for t, d in chunks if t == b'IDAT'))
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    bpp = max(1, ch * depth // 8)
    pix = [[None] * w for _ in range(h)]
    pos = 0
    def unpack_row(b, n):
        if depth == 8: return list(b[:n])
        if depth == 16: return [struct.unpack('>H', b[2 * i:2 * i + 2])[0] for i in range(n)]
        per = 8 // depth; out = []
        for byte in b:
            for k in range(per): out.append((byte >> (8 - depth * (k + 1))) & ((1 << depth) - 1))
        return out[:n]
    def conv(s):
        # s: samples of one pixel
        if color == 0:
            v = s[0] if depth <= 8 else s[0] >> 8
            if depth < 8: v = v * 255 // ((1 << depth) - 1)
            return (v, v, v, 255)
        if color == 2: return tuple((x if depth == 8 else x >> 8) for x in s) + (255,)
        if color == 3:
            c = plte[s[0]]; a = trns[s[0]] if trns and s[0] < len(trns) else 255
            return c + (a,)
        if color == 4:
            v = s[0] if depth == 8 else s[0] >> 8; a = s[1] if depth == 8 else s[1] >> 8
            return (v, v, v, a)
        return tuple((x if depth == 8 else x >> 8) for x in s)
    def run_pass(pw, ph, place):
        nonlocal pos
        if pw == 0 or ph == 0: return
        rowbytes = (pw * ch * depth + 7) // 8
        prev = bytes(rowbytes)
        for y in range(ph):
            ft = raw[pos]; pos += 1
            line = bytearray(raw[pos:pos + rowbytes]); pos += rowbytes
            for i in range(rowbytes):
                a = line[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
                if ft == 1: line[i] = (line[i] + a) & 255
                elif ft == 2: line[i] = (line[i] + b) & 255
                elif ft == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
                elif ft == 4: line[i] = (line[i] + _paeth(a, b, c)) & 255
            prev = bytes(line)
            samples = unpack_row(prev, pw * ch)
            for x in range(pw): place(x, y, conv(samples[x * ch:(x + 1) * ch]))
    if not inter:
        run_pass(w, h, lambda x, y, px: pix[y].__setitem__(x, px))
    else:
        for (x0, y0, dx, dy) in ADAM7:
            pw = (w - x0 + dx - 1) // dx if w > x0 else 0; ph = (h - y0 + dy - 1) // dy if h > y0 else 0
            run_pass(pw, ph, lambda x, y, px, x0=x0, y0=y0, dx=dx, dy=dy: pix[y0 + y * dy].__setitem__(x0 + x * dx, px))
    return w, h, pix, dict(depth=depth, color=color, interlace=inter, chunks=[t.decode() for t, _ in chunks])

# ------------------------------------------------------------------------------------------------ GIF
def _lzw(indices, minbits):
    clear = 1 << minbits; eoi = clear + 1
    out = bytearray(); cur = 0; nbits = 0
    def put(code, size):
        nonlocal cur, nbits
        cur |= code << nbits; nbits += size
        while nbits >= 8: out.append(cur & 255); cur >>= 8; nbits -= 8
    size = minbits + 1; table = {}; nxt = eoi + 1
    put(clear, size)
    s = ()
    for v in indices:
        t = s + (v,)
        if len(t) == 1 or t in table:
            s = t; continue
        # emit code for s
        code = s[0] if len(s) == 1 else table[s]
        put(code, size)
        table[t] = nxt; nxt += 1
        if nxt > (1 << size) and size < 12: size += 1
        if nxt >= 4096:
            put(clear, size); table = {}; nxt = eoi + 1; size = minbits + 1
        s = (v,)
    if s:
        code = s[0] if len(s) == 1 else table[s]
        put(code, size)
        nxt += 1
        if nxt > (1 << size) and size < 12: size += 1
    put(eoi, size)
    if nbits: out.append(cur & 255)
    return bytes(out)

def write_gif(width, height, palette, index_rows, transparent=None, interlace=False, minbits=None):
    n = len(palette)
    bits = max(1, (n - 1).bit_length())
    pal = list(palette) + [(0, 0, 0)] * ((1 << bits) - n)
    out = b'GIF89a' + struct.pack('<HHBBB', width, height, 0x80 | (7 << 4) | (bits - 1), 0, 0)
    out += b''.join(bytes(c) for c in pal)
    if transparent is not None:
        out += b'\x21\xf9\x04' + bytes((0x01 | (1 << 2),)) + struct.pack('<H', 0) + bytes((transparent,)) + b'\x00'
    rows = index_rows
    if interlace:
        order = list(range(0, height, 8)) + list(range(4, height, 8)) + list(range(2, height, 4)) + list(range(1, height, 2))
        rows = [index_rows[y] for y in order]
    flat = [v for r in rows for v in r]
    mb = max(2, bits) if minbits is None else minbits
    data = _lzw(flat, mb)
    out += b'\x2c' + struct.pack('<HHHHB', 0, 0, width, height, 0x40 if interlace else 0) + bytes((mb,))
    for i in range(0, len(data), 255):
        blk = data[i:i + 255]; out += bytes((len(blk),)) + blk
    return out + b'\x00' + b'\x3b'

def read_gif_first(data):
    """a straightforward GIF reader (first frame, no interlace special-casing beyond the flag) used to check write_gif."""
    w, h, flags = struct.unpack('<HHB', data[6:11]); p = 13
    gct = None
    if flags & 0x80:
        n = 2 << (flags & 7); gct = [tuple(data[p + 3 * i:p + 3 * i + 3]) for i in range(n)]; p += 3 * n
    trans = None
    while True:
        b = data[p]; p += 1
        if b == 0x21:
            lab = data[p]; p += 1
            if lab == 0xf9 and data[p] == 4:
                if data[p + 1] & 1: trans = data[p + 4]
            while data[p]: p += data[p] + 1
            p += 1
        elif b == 0x2c:
            ix, iy, iw, ih, fl = struct.unpack('<HHHHB', data[p:p + 9]); p += 9
            mb = data[p]; p += 1
            blob = bytearray()
            while data[p]: blob += data[p + 1:p + 1 + data[p]]; p += data[p] + 1
            p += 1
            idx = _unlzw(bytes(blob), mb, iw * ih)
            rows = [idx[i * iw:(i + 1) * iw] for i in range(ih)]
            if fl & 0x40:
                order = list(range(0, ih, 8)) + list(range(4, ih, 8)) + list(range(2, ih, 4)) + list(range(1, ih, 2))
                out = [None] * ih
                for src, dst in enumerate(order): out[dst] = rows[src]
                rows = out
            pix = [[(0, 0, 0, 0) if v == trans else gct[v] + (255,) for v in r] for r in rows]
            return iw, ih, pix
        else: break

def _unlzw(data, minbits, count):
    clear = 1 << minbits; eoi = clear + 1
    size = minbits + 1; table = {i: (i,) for i in range(clear)}; nxt = eoi + 1
    out = []; prev = None; cur = 0; nb = 0; pos = 0
    while len(out) < count:
        while nb < size:
            if pos >= len(data): return out
            cur |= data[pos] << nb; nb += 8; pos += 1
        code = cur & ((1 << size) - 1); cur >>= size; nb -= size
        if code == clear:
            size = minbits + 1; table = {i: (i,) for i in range(clear)}; nxt = eoi + 1; prev = None; continue
        if code == eoi: break
        if prev is None:
            ent = table[code]
        else:
            ent = table[code] if code in table else prev + (prev[0],)
            table[nxt] = prev + (ent[0],); nxt += 1
            if nxt >= (1 << size) and size < 12: size += 1
        out.extend(ent); prev = ent
    return out

# ------------------------------------------------------------------------------------------------ BMP / ICO
def write_bmp(width, height, pix=None, bpp=24, topdown=False, palette=None, index_rows=None, rle8=False):
    """pix: rows of (r,g,b,a) used for bpp 24/32; for 8/4 bpp: palette + index_rows."""
    rows = list(range(height)) if topdown else list(range(height - 1, -1, -1))
    body = bytearray()
    comp = 0
    if bpp in (24, 32):
        for y in rows:
            line = bytearray()
            for (r, g, b, a) in pix[y]:
                line += bytes((b, g, r)) if bpp == 24 else bytes((b, g, r, 255))
            while len(line) % 4: line.append(0)
            body += line
    elif bpp == 8 and rle8:
        comp = 1
        for y in rows:
            r = index_rows[y]; i = 0
            while i < width:
                n = 1
                while i + n < width and r[i + n] == r[i] and n < 255: n += 1
                body += bytes((n, r[i])); i += n
            body += b'\x00\x00'
        body[-2:] = b'\x00\x01'   # the last end-of-line becomes end-of-bitmap
        # (valid: 00 01 ends the bitmap; the last row's EOL is merged into it)
    elif bpp in (8, 4, 1):
        for y in rows:
            r = index_rows[y]; line = bytearray()
            if bpp == 8: line = bytearray(r)
            elif bpp == 4:
                for i in range(0, width, 2): line.append((r[i] << 4) | (r[i + 1] if i + 1 < width else 0))
            else:
                for i in range(0, width, 8):
                    v = 0
                    for k in range(8): v |= (r[i + k] if i + k < width else 0) << (7 - k)
                    line.append(v)
            while len(line) % 4: line.append(0)
            body += line
    pal = b''
    ncol = 0
    if palette is not None:
        ncol = len(palette) if bpp == 8 else (1 << bpp)
        pl = list(palette) + [(0, 0, 0)] * (ncol - len(palette))
        pal = b''.join(bytes((b, g, r, 0)) for (r, g, b) in pl)
    hdr = struct.pack('<IiiHHIIiiII', 40, width, -height if topdown else height, 1, bpp, comp, len(body), 2835, 2835, ncol, 0)
    off = 14 + 40 + len(pal)
    return b'BM' + struct.pack('<IHHI', off + len(body), 0, 0, off) + hdr + pal + bytes(body)

def ico_bmp_entry(width, height, pix, bpp=32, palette=None, index_rows=None):
    """DIB for an ICO: header height = 2*h, pixels bottom-up, then the 1-bpp AND mask (1 = transparent)."""
    xor = bytearray(); mask = bytearray()
    for y in range(height - 1, -1, -1):
        line = bytearray()
        if bpp == 32:
            for (r, g, b, a) in pix[y]: line += bytes((b, g, r, a))
        elif bpp == 8:
            line = bytearray(index_rows[y])
        else:   # 4-bit: two pixels per byte
            r = index_rows[y]
            for i in range(0, width, 2): line.append((r[i] << 4) | (r[i + 1] if i + 1 < width else 0))
        while len(line) % 4: line.append(0)
        xor += line
        m = bytearray()
        for i in range(0, width, 8):
            v = 0
            for k in range(8):
                if i + k < width and pix[y][i + k][3] == 0: v |= 1 << (7 - k)
                elif i + k >= width: v |= 1 << (7 - k)
            m.append(v)
        while len(m) % 4: m.append(0)
        mask += m
    pal = b''
    ncol = 0
    if palette is not None:
        ncol = 1 << bpp
        pl = list(palette) + [(0, 0, 0)] * (ncol - len(palette))
        pal = b''.join(bytes((b, g, r, 0)) for (r, g, b) in pl)
    hdr = struct.pack('<IiiHHIIiiII', 40, width, height * 2, 1, bpp, 0, len(xor) + len(mask), 0, 0, ncol, 0)
    return hdr + pal + bytes(xor) + bytes(mask)

def write_ico(entries):
    """entries: list of (width, height, bitcount, image_bytes)."""
    out = struct.pack('<HHH', 0, 1, len(entries))
    off = 6 + 16 * len(entries)
    for (w, h, bc, data) in entries:
        out += struct.pack('<BBBBHHII', w % 256, h % 256, 0, 0, 1, bc, len(data), off); off += len(data)
    for e in entries: out += e[3]
    return out


def canon_op(pix):
    """canvas view where only fully opaque pixels keep their colour: a == 255 -> (r,g,b,255); otherwise (0,0,0,a).
    Used for images with anti-aliased alpha edges: the canvas un-premultiplies them with rounding, so their colour is not compared."""
    return [[p if p[3] == 255 else (0, 0, 0, p[3]) for p in row] for row in pix]

def checksum_op(pix):
    out = bytearray()
    for row in canon_op(pix):
        for r, g, b, a in row: out += bytes((r, g, b, a))
    return fnv1a(bytes(out))
