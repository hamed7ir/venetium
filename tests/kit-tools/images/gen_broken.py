"""Builds the BROKEN test files of images.html into out/broken/ and out/broken.json. Each broken file is a small, named edit of a
valid file (so the valid sibling is the control), or a tree file made for a decoder regression test.
For every file the record says which decoder path it should reach ("reaches") — derived from the code: libjpeg-turbo's marker reader
(jdmarker.c) and Blink's JPEG decoder (setjmp in JPEGImageReader::Decode; error_exit = longjmp).
Run after gen_valid.py: python gen_broken.py
"""
import os, json, struct, zlib, hashlib, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import imgtools as T, jpegseg

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ.get('IMG_OUT') or os.path.join(HERE, 'out'); VAL = os.path.join(OUT, 'valid'); BRK = os.path.join(OUT, 'broken')
TREE = os.environ.get('IMG_TREE', 'F:/cr/src').replace(chr(92), '/').rstrip('/') + '/'   # the Chromium tree the tree-sourced test files are copied from
os.makedirs(BRK, exist_ok=True)
valid = {v['id']: v for v in json.load(open(os.path.join(OUT, 'valid.json')))}
def vbytes(id): return open(os.path.join(VAL, id + '.' + valid[id]['fmt']), 'rb').read()
cases = []

def add(id, fmt, mime, data, base, reaches, allowed, why, source='generated (gen_broken.py)', phase=''):
    path = os.path.join(BRK, id + '.' + fmt)
    open(path, 'wb').write(data)
    cases.append(dict(id=id, fmt=fmt, type=mime, file='images/decode/broken/' + id + '.' + fmt, bytes=len(data),
                      sha256=hashlib.sha256(data).hexdigest(), base=base, reaches=reaches, allowed=allowed, why=why, source=source, phase=phase))

# outcome classes: error (onerror), empty (loaded, size known, no pixel drawn), partial (top rows right, rest empty),
#                  degraded (whole image but coarse: progressive prefix), corrupt (pixels present but wrong), complete (identical to the valid sibling)
# 'corrupt' is allowed for NO file: a decoder that returns same-size garbage for a damaged stream has not failed cleanly (BATCH-DEVICE-1).
# The six files without a valid sibling and with error/empty/content all allowed are no-crash / no-hang probes: the page names their checks SURVIVES.
CLEAN = ['error', 'empty', 'partial', 'degraded']

# ============================================================================================ JPEG
J = vbytes('jpeg-baseline-420')
segs = jpegseg.segments(J)
byid = [s for s in segs if s[0] != 'scan']
def seg(marker, n=0):
    return [s for s in segs if s[0] == marker][n]
sos = seg(0xda); scan_start = sos[1] + sos[2]
sc = [s for s in segs if s[0] == 'scan'][0]
scan_end = sc[1] + sc[2]
dht0 = seg(0xc4, 0); sof = seg(0xc0); dqt0 = seg(0xdb, 0); app0 = seg(0xe0)
M = 'image/jpeg'

# --- truncated
add('jpeg-trunc-scan', 'jpg', M, J[:scan_start + (scan_end - scan_start) // 2], 'jpeg-baseline-420', 'jpeg_read_scanlines suspends at the end of the data (no error_exit); Blink then keeps the rows decoded so far',
    ['partial', 'error', 'empty'], 'baseline JPEG cut in the middle of the entropy-coded scan', phase='scan (suspension)')
add('jpeg-trunc-scan-early', 'jpg', M, J[:scan_start + 40], 'jpeg-baseline-420', 'same, with only the first 40 bytes of scan data',
    ['partial', 'error', 'empty'], 'baseline JPEG cut right after the first MCUs', phase='scan (suspension)')
add('jpeg-trunc-header', 'jpg', M, J[:dht0[1] + 20], 'jpeg-baseline-420', 'jpeg_read_header suspends inside the first DHT: size never known, Blink fails the image',
    ['error'], 'JPEG cut inside the first Huffman table (before the scan)', phase='header (suspension)')
P = vbytes('jpeg-420-islow-prog')
psegs = jpegseg.segments(P)
pscans = [s for s in psegs if s[0] == 'scan']
cut = pscans[4][1] + pscans[4][2] // 2
add('jpeg-prog-trunc', 'jpg', M, P[:cut], 'jpeg-420-islow-prog', 'progressive decode stops after some scans: the image so far (coarse) is shown; Blink uses jpeg_start_output/jpeg_consume_input',
    ['degraded', 'partial', 'error', 'empty'], 'progressive JPEG cut in the 5th scan', phase='scan (suspension)')

# --- bad Huffman table
def patch(d, off, new): return d[:off] + new + d[off + len(new):]
# DHT segment layout: FF C4 | len(2) | Tc/Th (1) | 16 counts | symbols
t0 = dht0[1] + 4            # Tc/Th byte
counts = list(J[t0 + 1:t0 + 17]); nsym = sum(counts)
assert dht0[2] == 2 + 2 + 1 + 16 + nsym, 'unexpected first DHT layout'
bad = bytearray(J)
bad[t0 + 16] = 200          # 16-bit code length count of 200: the table claims 200+ more symbols than the segment holds
add('jpeg-badhuff-count', 'jpg', M, bytes(bad), 'jpeg-baseline-420', 'get_dht(): count > length-17 -> ERREXIT(JERR_BAD_HUFF_TABLE) inside jpeg_read_header -> error_exit -> longjmp to JPEGImageReader::Decode',
    ['error'], 'first DHT claims more symbols than it holds (rejected while reading the header)', phase='header (error_exit)')
# Kraft violation: keep the segment length consistent, but ask for 3 codes of length 1 (only 2 exist)
c2 = list(counts); c2[0] += 3
rem = 3
for i in range(15, -1, -1):
    take = min(rem, c2[i] if i else 0)
    if i and take: c2[i] -= take; rem -= take
assert sum(c2) == nsym and rem == 0
bad = bytearray(J); bad[t0 + 1:t0 + 17] = bytes(c2)
add('jpeg-badhuff-kraft', 'jpg', M, bytes(bad), 'jpeg-baseline-420', 'header parses (size known); jpeg_start_decompress -> jpeg_make_d_derived_tbl: code space overflow -> ERREXIT(JERR_BAD_HUFF_TABLE) -> longjmp',
    ['empty', 'error'], 'first DHT lists 3 codes of length 1 (over-subscribed): header is fine, building the decode table fails', phase='start_decompress (error_exit)')
bad = bytearray(J)
bad[t0 + 17] = 0xff          # first DC symbol 255: a DC table may only hold categories 0..15
add('jpeg-badhuff-symbol', 'jpg', M, bytes(bad), 'jpeg-baseline-420', 'jpeg_make_d_derived_tbl (DC table): symbol > 15 -> ERREXIT(JERR_BAD_HUFF_TABLE) -> longjmp',
    ['empty', 'error'], 'DC Huffman table holds symbol 255 (header fine, table build fails)', phase='start_decompress (error_exit)')

# --- bad marker
unk = J[:app0[1] + app0[2]] + b'\xff\xf5' + J[app0[1] + app0[2]:]
add('jpeg-badmarker-unknown', 'jpg', M, unk, 'jpeg-baseline-420', 'read_markers(): reserved marker JPG5 (FF F5) -> default: ERREXIT1(JERR_UNKNOWN_MARKER) in jpeg_read_header -> longjmp',
    ['error'], 'a reserved marker (FF F5) between APP0 and DQT', phase='header (error_exit)')
bad = bytearray(J); bad[sof[1] + 1] = 0xc5
add('jpeg-badmarker-sof5', 'jpg', M, bytes(bad), 'jpeg-baseline-420', 'read_markers(): SOF5 (differential sequential) -> ERREXIT1(JERR_SOF_UNSUPPORTED) -> longjmp',
    ['error'], 'frame header marker changed to SOF5 (unsupported frame type)', phase='header (error_exit)')
bad = bytearray(J); bad[dqt0[1] + 4] = 0x07          # Pq/Tq byte: table index 7 (>3)
add('jpeg-badmarker-dqt-index', 'jpg', M, bytes(bad), 'jpeg-baseline-420', 'get_dqt(): table index 7 -> ERREXIT1(JERR_DQT_INDEX) -> longjmp',
    ['error'], 'DQT segment names quantisation table 7', phase='header (error_exit)')
bad = bytearray(J); bad[sof[1] + 9] = 0x00          # number of components 0
add('jpeg-badmarker-sof-components', 'jpg', M, bytes(bad), 'jpeg-baseline-420', 'get_sof(): 0 components -> ERREXIT(JERR_EMPTY_IMAGE)/BAD_LENGTH -> longjmp',
    ['error'], 'frame header says 0 components', phase='header (error_exit)')
bad = bytearray(J); bad[app0[1] + 2:app0[1] + 4] = b'\xff\xff'      # APP0 claims a 65535-byte segment
add('jpeg-badmarker-seglen', 'jpg', M, bytes(bad), 'jpeg-baseline-420', 'APP0 length 65535 runs past the data: skip_input_data/fill_input_buffer suspend, the data never completes the header -> Blink fails the image',
    ['error'], 'APP0 length field 0xFFFF (segment longer than the file)', phase='header (suspension)')
bad = bytearray(J); bad[sos[1] + 4] = 0x09          # SOS: component count 9
add('jpeg-badmarker-sos', 'jpg', M, bytes(bad), 'jpeg-baseline-420', 'get_sos(): bad component count / length -> ERREXIT(JERR_BAD_LENGTH) -> longjmp',
    ['error'], 'scan header says 9 components', phase='header (error_exit)')

# --- errors in the middle of a progressive stream: the size is known and some scans are in, then a marker / table is bad
pdht = [s for s in psegs if s[0] == 0xc4]
mid = pdht[4][1]                       # a DHT between scans, after the 4th scan
add('jpeg-prog-badmarker-midstream', 'jpg', M, P[:mid] + b'\xff\xf5' + P[mid:], 'jpeg-420-islow-prog', 'jpeg_consume_input -> read_markers: reserved marker (FF F5) between scans -> ERREXIT1(JERR_UNKNOWN_MARKER) -> longjmp, after the size is known',
    ['empty', 'degraded', 'partial', 'error'], 'progressive JPEG with a reserved marker between the 4th and 5th scan', phase='consume_input (error_exit, mid-stream)')
bad = bytearray(P); tt = mid + 4; bad[tt + 16] = 250
add('jpeg-prog-badhuff-midstream', 'jpg', M, bytes(bad), 'jpeg-420-islow-prog', 'jpeg_consume_input -> get_dht: count > length -> ERREXIT(JERR_BAD_HUFF_TABLE) -> longjmp, after the size is known',
    ['empty', 'degraded', 'partial', 'error'], 'progressive JPEG whose 5th scan has a Huffman table claiming 250+ symbols', phase='consume_input (error_exit, mid-stream)')

# --- valid-but-unsupported files from libjpeg-turbo / Blink test data: error_exit after the header
JT = TREE + 'third_party/libjpeg_turbo/testimages/'
add('jpeg-arithmetic', 'jpg', M, open(JT + 'testimgari.jpg', 'rb').read(), None, 'arithmetic coding is not compiled in (D_ARITH_CODING_SUPPORTED off in src/jconfig.h): jinit_master_decompress -> ERREXIT(JERR_ARITH_NOTIMPL) -> longjmp',
    ['empty', 'error'], 'a valid arithmetic-coded JPEG (libjpeg-turbo testimgari.jpg): size known, decode refused', source='copy of third_party/libjpeg_turbo/testimages/testimgari.jpg', phase='start_decompress (error_exit)')
add('jpeg-12bit', 'jpg', M, open(JT + 'testorig12.jpg', 'rb').read(), None, '12-bit samples through the 8-bit API: jpeg_read_scanlines -> ERREXIT1(JERR_BAD_PRECISION) -> longjmp',
    ['empty', 'error'], 'a valid 12-bit JPEG (libjpeg-turbo testorig12.jpg): Blink decodes 8-bit only', source='copy of third_party/libjpeg_turbo/testimages/testorig12.jpg', phase='read_scanlines (error_exit)')
add('jpeg-many-scans', 'jpg', M, open(TREE + 'third_party/blink/renderer/platform/image-decoders/testing/many-progressive-scans.jpg', 'rb').read(), None,
    'progress monitor (ProgressMonitor in jpeg_image_decoder.cc) calls error_exit() itself once the input has 100 scans -> longjmp',
    ['empty', 'error'], 'progressive JPEG with a very large number of scans (Blink regression file for crbug 642462)', source='copy of third_party/blink/renderer/platform/image-decoders/testing/many-progressive-scans.jpg', phase='consume_input (error_exit from the progress monitor)')

# ============================================================================================ PNG
PN = vbytes('png-rgb8')
def chunks(d):
    p = 8; out = []
    while p < len(d):
        n = struct.unpack('>I', d[p:p + 4])[0]; out.append((d[p + 4:p + 8], p, n)); p += 12 + n
    return out
ch = chunks(PN); idat = [c for c in ch if c[0] == b'IDAT'][0]
M = 'image/png'
add('png-trunc-idat', 'png', M, PN[:idat[1] + 8 + idat[2] // 2], 'png-rgb8', 'Rust png crate (SkPngRustCodec): input ends inside IDAT -> incomplete; rows decoded so far stay',
    ['partial', 'error', 'empty'], 'PNG cut in the middle of the IDAT data', phase='IDAT (end of data)')
add('png-trunc-header', 'png', M, PN[:20], 'png-rgb8', 'cut inside IHDR: size never known',
    ['error'], 'PNG cut inside the IHDR chunk', phase='IHDR (end of data)')
PM = vbytes('png-rgb8-multi-idat')
chm = chunks(PM); idats = [c for c in chm if c[0] == b'IDAT']
assert len(idats) >= 4
bad = bytearray(PM); bad[idats[0][1] + 8 + idats[0][2] + 1] ^= 0xff
add('png-crc-idat-first', 'png', M, bytes(bad), 'png-rgb8-multi-idat', 'first of several IDAT chunks has a wrong CRC-32: the png crate reports CrcMismatch when that chunk ends, mid-image',
    ['error', 'partial', 'empty'], 'PNG whose first IDAT chunk has a wrong CRC (the image data continues in later chunks)', phase='IDAT (CRC, mid-image)')
bad = bytearray(PM); bad[idats[-1][1] + 8 + idats[-1][2] + 1] ^= 0xff
add('png-crc-idat-last', 'png', M, bytes(bad), 'png-rgb8-multi-idat', 'last IDAT chunk has a wrong CRC-32: all rows may already be out before the check, so the image can complete or fail (no crash either way)',
    ['complete', 'error', 'partial', 'empty'], 'PNG whose last IDAT chunk has a wrong CRC', phase='IDAT (CRC, end)')
ihdr = ch[0]
bad = bytearray(PN); bad[ihdr[1] + 8 + 13] ^= 0x55
add('png-crc-ihdr', 'png', M, bytes(bad), 'png-rgb8', 'IHDR CRC-32 wrong: rejected before the size is known',
    ['error'], 'PNG whose IHDR chunk has a wrong CRC', phase='IHDR (CRC)')
# inflate error with a correct CRC: flip bytes inside the zlib stream and fix the chunk CRC
z = bytearray(PN[idat[1] + 8:idat[1] + 8 + idat[2]])
for i in (len(z) // 2, len(z) // 2 + 1, len(z) // 2 + 2): z[i] ^= 0xa5
bad = PN[:idat[1] + 8] + bytes(z) + struct.pack('>I', zlib.crc32(b'IDAT' + bytes(z)) & 0xffffffff) + PN[idat[1] + 12 + idat[2]:]
add('png-bad-zlib', 'png', M, bad, 'png-rgb8', 'CRC fine, deflate stream corrupt in the middle: inflate error after some rows',
    ['error', 'partial', 'empty'], 'PNG whose zlib data is corrupt halfway (CRC recomputed)', phase='IDAT (inflate)')
raw = bytearray(zlib.decompress(PN[idat[1] + 8:idat[1] + 8 + idat[2]]))
rowlen = 1 + W * 3 if (W := 64) else 0
raw[20 * rowlen] = 9            # filter type 9 on row 20
z2 = zlib.compress(bytes(raw), 9)
bad = PN[:idat[1]] + struct.pack('>I', len(z2)) + b'IDAT' + z2 + struct.pack('>I', zlib.crc32(b'IDAT' + z2) & 0xffffffff) + PN[idat[1] + 12 + idat[2]:]
add('png-bad-filter', 'png', M, bad, 'png-rgb8', 'scanline filter type 9 on row 20: invalid filter -> decoder error after 20 rows',
    ['error', 'partial', 'empty'], 'PNG with an invalid filter byte on one row', phase='IDAT (filter)')
ih = bytearray(PN[ihdr[1] + 8:ihdr[1] + 8 + 13]); ih[0:4] = struct.pack('>I', 0x7fffffff); ih[4:8] = struct.pack('>I', 0x7fffffff)
bad = PN[:ihdr[1] + 8] + bytes(ih) + struct.pack('>I', zlib.crc32(b'IHDR' + bytes(ih)) & 0xffffffff) + PN[ihdr[1] + 12 + 13:]
add('png-huge-dims', 'png', M, bad, 'png-rgb8', '2147483647 x 2147483647 pixels in IHDR: must be refused (size limit), not allocated',
    ['error'], 'PNG whose IHDR claims 2^31-1 x 2^31-1 pixels', phase='IHDR (limits)')
add('png-critical-before-ihdr', 'png', M, open(TREE + 'third_party/blink/renderer/platform/image-decoders/testing/private-critical-chunk-before-ihdr.png', 'rb').read(), None,
    'a critical private chunk before IHDR: the decoder fails with no size (Blink PNGTests.CriticalPrivateChunkBeforeIHDR)', ['error'], 'PNG with a critical chunk before IHDR (Blink regression file)',
    source='copy of third_party/blink/renderer/platform/image-decoders/testing/private-critical-chunk-before-ihdr.png', phase='before IHDR')

# ============================================================================================ GIF
GF = vbytes('gif-static')
M = 'image/gif'
# find the image descriptor / LZW data
p = GF.index(b'\x2c')
lzw_start = p + 10 + 1            # after the descriptor and the LZW minimum code size byte
add('gif-trunc-lzw', 'gif', M, GF[:lzw_start + 600], 'gif-static', 'Wuffs GIF decoder (SkWuffsCodec): data ends inside the LZW stream: rows so far are kept',
    ['partial', 'error', 'empty'], 'GIF cut in the middle of the image data', phase='LZW (end of data)')
add('gif-trunc-header', 'gif', M, GF[:9], 'gif-static', 'cut inside the header: size never known', ['error'], 'GIF cut inside the logical screen descriptor', phase='header (end of data)')
bad = bytearray(GF); bad[p + 10] = 13
add('gif-bad-lzw-minsize', 'gif', M, bytes(bad), 'gif-static', 'LZW minimum code size 13 (> 12): Wuffs rejects the frame', ['error', 'empty'], 'GIF with LZW minimum code size 13', phase='LZW (header)')
GT = TREE + 'third_party/blink/renderer/platform/image-decoders/testing/'
for name, why in [('bad-initial-code', 'LZW stream starts with an invalid code (Blink GIFImageDecoderTest.badInitialCode: decode fails)'),
                  ('bad-code', 'LZW code beyond the dictionary (Blink GIFImageDecoderTest.badCode: decode fails)'),
                  ('broken', 'frame that cannot be decoded (Blink GIFImageDecoderTest.brokenSecondFrame)')]:
    add('gif-' + name, 'gif', M, open(GT + name + '.gif', 'rb').read(), None, 'Wuffs LZW decoder error; no setjmp', ['error', 'empty', 'content'], why,
        source='copy of third_party/blink/renderer/platform/image-decoders/testing/' + name + '.gif', phase='LZW (error)')

# ============================================================================================ WebP
M = 'image/webp'
WL = vbytes('webp-lossy')
add('webp-trunc-lossy', 'webp', M, WL[:len(WL) * 6 // 10], 'webp-lossy', 'libwebp incremental decoder (WebPIDecode) stops where the data ends: partial rows',
    ['partial', 'error', 'empty'], 'lossy WebP (VP8) cut at 60 %', phase='VP8 (end of data)')
bad = bytearray(WL); i = WL.index(b'\x9d\x01\x2a'); bad[i + 2] = 0x2b
add('webp-bad-vp8-startcode', 'webp', M, bytes(bad), 'webp-lossy', 'VP8 key-frame start code 9D 01 2A broken: VP8GetInfo rejects the stream',
    ['error'], 'lossy WebP with a broken VP8 start code', phase='VP8 header')
WW = vbytes('wheel-webp')
add('webp-trunc-lossless', 'webp', M, WW[:len(WW) // 2], 'wheel-webp', 'VP8L decoder runs out of bits: partial image', ['partial', 'error', 'empty'], 'lossless WebP (VP8L) cut at 50 %', phase='VP8L (end of data)')
RW = vbytes('rand-webp'); bad = bytearray(RW); i = RW.index(b'VP8L') + 8; assert bad[i] == 0x2f; bad[i] = 0x2e
add('webp-bad-vp8l-signature', 'webp', M, bytes(bad), 'rand-webp', 'VP8L signature byte 0x2F changed: rejected as not a WebP bitstream', ['error'], 'lossless WebP with a wrong VP8L signature byte', phase='VP8L header')
bad = bytearray(WW); i = WW.index(b'VP8L') + 8 + 5
for k in range(i, i + 24): bad[k] ^= 0xff
add('webp-corrupt-vp8l-codes', 'webp', M, bytes(bad), 'wheel-webp', 'the first bytes after the VP8L header (transforms and Huffman code lengths) are inverted: invalid prefix codes -> bitstream error',
    ['error', 'empty', 'partial'], 'lossless WebP with corrupted transform/prefix-code data', phase='VP8L (prefix codes)')

# own lossless files with structurally valid but semantically invalid streams (vp8l.py, bad=...): libwebp's own error returns
import vp8l as V
simple = [0xff000000 | ((x * 4) << 16) | ((y * 5) << 8) | ((x ^ y) & 255) for y in range(48) for x in range(64)]
add('webp-bad-backref', 'webp', M, V.encode(64, 48, simple, bad='backref'), 'webp-ll-literal', 'VP8L: the first token is a backward reference to before the start of the image -> bitstream error in DecodeImageData',
    ['error', 'empty'], 'lossless WebP whose first symbol copies from before the image', source='generated (gen_broken.py + vp8l.py)', phase='VP8L (backward reference)')
add('webp-bad-cache-bits', 'webp', M, V.encode(64, 48, simple, bad='cache12'), 'webp-ll-literal', 'VP8L: colour cache size 2^12 (allowed: 2^1 .. 2^11) -> rejected while reading the image header',
    ['error', 'empty'], 'lossless WebP with an out-of-range colour cache size', source='generated (gen_broken.py + vp8l.py)', phase='VP8L (header)')
add('webp-bad-vp8l-version', 'webp', M, V.encode(64, 48, simple, bad='version'), 'webp-ll-literal', 'VP8L version bits 1 (must be 0) -> VP8LCheckSignature fails',
    ['error'], 'lossless WebP with version number 1', source='generated (gen_broken.py + vp8l.py)', phase='VP8L (header)')
WM = vbytes('webp-ll-cache-lz77-meta')
add('webp-trunc-vp8l-meta', 'webp', M, WM[:len(WM) * 55 // 100], 'webp-ll-cache-lz77-meta', 'VP8L with meta prefix codes, colour cache and LZ77, cut at 55 %: the bit reader runs out inside the pixel data',
    ['partial', 'error', 'empty'], 'lossless WebP (all features) cut at 55 %', source='generated (gen_broken.py + vp8l.py)', phase='VP8L (end of data)')

WA = vbytes('webp-ll-anim')
add('webp-anim-trunc', 'webp', M, WA[:len(WA) * 70 // 100], 'webp-ll-anim', 'animated WebP cut inside the second frame: the first frame is complete, the demuxer reports the frame as incomplete',
    ['complete', 'partial', 'error', 'empty'], 'animated WebP cut at 70 % (inside frame 2)', source='generated (gen_broken.py)', phase='ANMF (end of data)')
# Blink regression files that must not crash the decoder (any stable outcome is clean)
BT = TREE + 'third_party/blink/renderer/platform/image-decoders/testing/'
for name, why, ph in [('apng-with-malformed-2nd-frame', 'APNG whose 2nd frame is malformed: frame 1 decodes, the 2nd fails (Blink PNGTests.RecoveringToReadFirstFrameAfterSecondFrameFailure)', 'fdAT (malformed frame)'),
                      ('actl-num-frames-0', 'APNG with acTL num_frames = 0 (Blink PNGTests.ActlZero: must not crash)', 'acTL'),
                      ('plte-weirdness', 'PNG with a PLTE chunk after the image data (Blink PNGTests.PlteAfterInitialImageData: must not crash)', 'PLTE after IDAT')]:
    add('png-' + name, 'png', 'image/png', open(BT + name + '.png', 'rb').read(), None, 'Rust png crate / SkPngRustCodec error handling; no setjmp', ['content', 'error', 'empty'], why,
        source='copy of third_party/blink/renderer/platform/image-decoders/testing/' + name + '.png', phase=ph)

# ============================================================================================ AVIF
M = 'image/avif'
AV = vbytes('avif-8bit')
add('avif-trunc', 'avif', M, AV[:len(AV) // 2], 'avif-8bit', 'crabbyavif: the item data lies past the end of the file -> error (no frame)', ['error', 'empty', 'partial'], 'AVIF cut at 50 % (inside the image data)', phase='container')
add('avif-trunc-meta', 'avif', M, AV[:150], 'avif-8bit', 'crabbyavif: file ends inside the meta box', ['error'], 'AVIF cut inside the meta box', phase='container')
bad = bytearray(AV); i = AV.index(b'ispe') + 4 + 4; bad[i:i + 4] = b'\x00\x00\x00\x00'
add('avif-bad-ispe', 'avif', M, bytes(bad), 'avif-8bit', 'ispe (image spatial extents) width 0: invalid container', ['error'], 'AVIF whose ispe box says width 0', phase='container')
i = AV.index(b'mdat') + 4
bad = bytearray(AV); bad[i] = 0x80            # forbidden bit set in the first OBU header
add('avif-bad-obu', 'avif', M, bytes(bad), 'avif-8bit', 'dav1d: invalid OBU header (forbidden bit set) in the first item byte -> decode error',
    ['error', 'empty', 'partial'], 'AVIF whose first AV1 OBU header has the forbidden bit set', phase='AV1 decode (dav1d)')

# ============================================================================================ BMP and ICO
BM = vbytes('bmp-24')
M = 'image/bmp'
add('bmp-trunc', 'bmp', M, BM[:54 + 64 * 3 * 20], 'bmp-24', 'Blink BMPImageReader (not the Rust decoder: kRustyBmpFeature is off): data ends after 20 rows (bottom-up)', ['partial', 'error', 'empty'], 'BMP cut after 20 of 48 rows', phase='pixel data (end of data)')
bad = bytearray(BM); bad[28:30] = struct.pack('<H', 7)
add('bmp-bad-bpp', 'bmp', M, bytes(bad), 'bmp-24', 'bits per pixel 7 is not a BMP depth: rejected', ['error'], 'BMP with 7 bits per pixel', phase='header')
bad = bytearray(BM); bad[18:22] = struct.pack('<i', 0x7fffffff)
add('bmp-huge-dims', 'bmp', M, bytes(bad), 'bmp-24', 'width 2^31-1: refused by the size limit', ['error'], 'BMP whose width is 2147483647', phase='header')
IC = vbytes('ico-bmp32')
M = 'image/x-icon'
add('ico-trunc', 'ico', M, IC[:len(IC) // 2], 'ico-bmp32', 'ICO whose single entry is cut: Blink ICO decoder / BMPImageReader stop at the end of the data', ['partial', 'error', 'empty'], 'ICO cut in the middle of the 32-bit image', phase='entry data')
bad = bytearray(IC); bad[6 + 12:6 + 16] = struct.pack('<I', 0x00fffff0)
add('ico-bad-offset', 'ico', M, bytes(bad), 'ico-bmp32', 'directory entry points far past the end of the file', ['error', 'empty'], 'ICO whose directory entry offset lies outside the file', phase='directory')

json.dump(cases, open(os.path.join(OUT, 'broken.json'), 'w'), indent=1)
print(len(cases), 'broken files,', sum(c['bytes'] for c in cases), 'bytes')
for c in cases: print('%-30s %-5s %6d  %s' % (c['id'], c['fmt'], c['bytes'], c['phase']))
