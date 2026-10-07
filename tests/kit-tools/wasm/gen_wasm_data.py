# Generates venetium/tests/kit/data/wasm-data.js (+ media/wasm-s10.wasm, media/wasm-ext.wasm) for wasm.html (BATCH-X86-2 section 1,
# page wasm; BATCH-DEVICE-1: i64 shift/rotate amounts 31/32/33, narrow/extending/unaligned/float memory script; follow-up: the host's record of
# the phases / step counts / operator groups the page must execute, ext.expect and ext.mem.expect).
#   python gen_wasm_data.py && node node_check.js && python gen_wasm_data.py && python manifest.py
#   (pass 1 writes the data file; node_check.js replays the kit's own lib + data in Node = V8 on x64 and fails unless EVERY digest, spin,
#   memloop, memory probe and memory-script step agrees with the python answers, writing node-answers.json; pass 2 merges Node's answers
#   into the data file (host_node_x64); manifest.py then rewrites manifest/wasm.tsv.)  Works from any cwd; every path is relative to
#   this file.  Node: F:/cr/src/third_party/node/win/node.exe (v24.x), or set KIT_NODE.  No bytecode cache is written (see below).
# Inputs: the ORIGINAL s10/jit/wasm.js (run unchanged in Node: module bytes + V8-x64 answers), s10/jit/expected.py (the host's
# python answers for wasm_i32 / wasm_i64), s10/jit/expected.txt and node-x64.txt (stored copies of the same answers).
# The EXTRA module ("ext") is new: i32/i64 operators (the ones a 32-bit target lowers to register pairs or libcalls), memory,
# and a hot loop. Its answers are computed here, in python, from first principles (exact integers; i64->f32 rounded directly,
# not through f64), and cross-checked against Node (V8 on x64) by node_check.js.
import sys, os, json, base64, struct, math, hashlib, subprocess
sys.dont_write_bytecode = True        # importing s10/jit/expected.py must not leave a __pycache__ in the record

HERE = os.path.dirname(os.path.abspath(__file__))
KIT = os.path.normpath(HERE + '/../../kit')                  # venetium/tests/kit
REPO = os.path.normpath(HERE + '/../../../..')                # the repository root (it holds s10/jit)
TOOL = 'venetium/tests/kit-tools/wasm/gen_wasm_data.py'      # how the generated files name this tool
NODE = os.environ.get('KIT_NODE', 'F:/cr/src/third_party/node/win/node.exe')
_nv = subprocess.check_output([NODE, '--version']).decode().strip()
assert _nv.startswith('v24.'), 'expected Node v24 (the kit data was produced with v24.12.0), got ' + _nv
sys.path.insert(0, REPO + '/s10/jit')
import expected as S10

# ---------------------------------------------------------------- the s10 module and answers (ground truth)
gt = json.loads(subprocess.check_output([NODE, HERE + '/s10_ground_truth.js']))
s10_bytes = bytes(gt['bytes'])
node_ans = dict(l.split('=', 1) for l in gt['printed'] if '=' in l)
txt = dict(l.strip().split('=', 1) for l in open(REPO + '/s10/jit/expected.txt', encoding='utf-8') if '=' in l)
txt_node = dict(l.strip().split('=', 1) for l in open(REPO + '/s10/jit/node-x64.txt', encoding='utf-8') if '=' in l)
for k in ('wasm_i32', 'wasm_i64'):
    assert S10.E[k] == txt[k] == txt_node[k] == node_ans[k], (k, S10.E[k], txt[k], txt_node[k], node_ans[k])

# ---------------------------------------------------------------- wasm assembler (python)
def leb(n):
    out = []
    while True:
        b = n & 0x7f; n >>= 7
        if n: out.append(b | 0x80)
        else: out.append(b); return out
def sleb(n):
    out = []
    while True:
        b = n & 0x7f; n >>= 7
        if (n == 0 and not (b & 0x40)) or (n == -1 and (b & 0x40)): out.append(b); return out
        out.append(b | 0x80)
def section(i, b): return [i] + leb(len(b)) + list(b)
def name(s): return leb(len(s)) + [ord(c) for c in s]
I32, I64 = 0x7f, 0x7e
VT = {'I': I32, 'L': I64}

# ---------------------------------------------------------------- reference semantics (exact integers)
M64 = (1 << 64) - 1; M32 = (1 << 32) - 1
def s64(x): x &= M64; return x - (1 << 64) if x >> 63 else x
def s32(x): x &= M32; return x - (1 << 32) if x >> 31 else x
def u64(x): return x & M64
def u32(x): return x & M32
class Trap(Exception): pass
def tdiv(a, b): q = abs(a) // abs(b); return q if (a < 0) == (b < 0) else -q
def dbits(f): return struct.unpack('<q', struct.pack('<d', f))[0]
def bitsd(b): return struct.unpack('<d', struct.pack('<q', b))[0]
def f32_bits_from_int(n):      # round-to-nearest-even straight from the integer (no double rounding)
    if n == 0: return 0
    sign = 0x80000000 if n < 0 else 0
    m = abs(n); e = m.bit_length() - 1
    if e <= 23: return sign | ((e + 127) << 23) | ((m << (23 - e)) & 0x7fffff)
    sh = e - 23; q = m >> sh; r = m & ((1 << sh) - 1); half = 1 << (sh - 1)
    if r > half or (r == half and (q & 1)): q += 1
    if q == (1 << 24): q >>= 1; e += 1
    return sign | ((e + 127) << 23) | (q & 0x7fffff)
def f32_bits_via_double(n): return struct.unpack('<I', struct.pack('<f', float(n)))[0]
def sext(x, bits): x &= (1 << bits) - 1; return x - (1 << bits) if x >> (bits - 1) else x
def need(c):
    if not c: raise Trap()
def trunc_checked(b, lo, hi):
    x = bitsd(b); need(not math.isnan(x) and not math.isinf(x)); t = math.trunc(x); need(lo <= t < hi); return t
def trunc_sat(b, lo, hi_incl):
    x = bitsd(b)
    if math.isnan(x): return 0
    if math.isinf(x): return hi_incl if x > 0 else lo
    return min(max(math.trunc(x), lo), hi_incl)

# group, wasm name, export name, signature, opcode bytes (after the parameters are loaded), inputs, python semantics
OPS = []
def op(group, nm, sig, code, inputs, fn, export=None): OPS.append(dict(group=group, name=nm, fn=export or nm.replace('.', '_'), sig=sig, code=code, inputs=inputs, ref=fn))
for nm, c, f in [
    ('add', 0x7c, lambda a, b: s64(a + b)), ('sub', 0x7d, lambda a, b: s64(a - b)), ('mul', 0x7e, lambda a, b: s64(a * b)),
    ('div_s', 0x7f, lambda a, b: (need(b != 0 and not (a == -2**63 and b == -1)), s64(tdiv(a, b)))[1]),
    ('div_u', 0x80, lambda a, b: (need(b != 0), s64(u64(a) // u64(b)))[1]),
    ('rem_s', 0x81, lambda a, b: (need(b != 0), s64(a - b * tdiv(a, b)))[1]),
    ('rem_u', 0x82, lambda a, b: (need(b != 0), s64(u64(a) % u64(b)))[1]),
    ('and', 0x83, lambda a, b: s64(u64(a) & u64(b))), ('or', 0x84, lambda a, b: s64(u64(a) | u64(b))), ('xor', 0x85, lambda a, b: s64(u64(a) ^ u64(b))),
    ('shl', 0x86, lambda a, b: s64(u64(a) << (u64(b) % 64))), ('shr_s', 0x87, lambda a, b: s64(a >> (u64(b) % 64))),
    ('shr_u', 0x88, lambda a, b: s64(u64(a) >> (u64(b) % 64))),
    ('rotl', 0x89, lambda a, b: s64(((u64(a) << (u64(b) % 64)) | (u64(a) >> (64 - u64(b) % 64))) & M64)),
    ('rotr', 0x8a, lambda a, b: s64(((u64(a) >> (u64(b) % 64)) | (u64(a) << (64 - u64(b) % 64))) & M64)),
]: op('i64-arith', 'i64.' + nm, 'LL>L', [c], 'V64', f)
for nm, c, f in [
    ('eq', 0x51, lambda a, b: int(a == b)), ('ne', 0x52, lambda a, b: int(a != b)),
    ('lt_s', 0x53, lambda a, b: int(a < b)), ('lt_u', 0x54, lambda a, b: int(u64(a) < u64(b))),
    ('gt_s', 0x55, lambda a, b: int(a > b)), ('gt_u', 0x56, lambda a, b: int(u64(a) > u64(b))),
    ('le_s', 0x57, lambda a, b: int(a <= b)), ('le_u', 0x58, lambda a, b: int(u64(a) <= u64(b))),
    ('ge_s', 0x59, lambda a, b: int(a >= b)), ('ge_u', 0x5a, lambda a, b: int(u64(a) >= u64(b))),
]: op('i64-cmp', 'i64.' + nm, 'LL>I', [c], 'V64', f)
op('i64-unary', 'i64.clz', 'L>L', [0x79], 'V64', lambda a: 64 - u64(a).bit_length())
op('i64-unary', 'i64.ctz', 'L>L', [0x7a], 'V64', lambda a: 64 if a == 0 else (u64(a) & -u64(a)).bit_length() - 1)
op('i64-unary', 'i64.popcnt', 'L>L', [0x7b], 'V64', lambda a: bin(u64(a)).count('1'))
op('i64-unary', 'i64.eqz', 'L>I', [0x50], 'V64', lambda a: int(a == 0))
op('i64-unary', 'i32.wrap_i64', 'L>I', [0xa7], 'V64', lambda a: s32(a))
op('i64-unary', 'i64.extend_i32_s', 'I>L', [0xac], 'V32', lambda a: a)
op('i64-unary', 'i64.extend_i32_u', 'I>L', [0xad], 'V32', lambda a: u32(a))
op('i64-unary', 'i64.extend8_s', 'L>L', [0xc2], 'V64', lambda a: sext(a, 8))
op('i64-unary', 'i64.extend16_s', 'L>L', [0xc3], 'V64', lambda a: sext(a, 16))
op('i64-unary', 'i64.extend32_s', 'L>L', [0xc4], 'V64', lambda a: sext(a, 32))
# conversions: results/inputs travel as i64 bit patterns so every value is an exact BigInt in JS
op('i64-conv', 'f64.convert_i64_s', 'L>L', [0xb9, 0xbd], 'V64', lambda a: dbits(float(a)))
op('i64-conv', 'f64.convert_i64_u', 'L>L', [0xba, 0xbd], 'V64', lambda a: dbits(float(u64(a))))
op('i64-conv', 'f32.convert_i64_s', 'L>I', [0xb4, 0xbc], 'V64', lambda a: s32(f32_bits_from_int(a)))
op('i64-conv', 'f32.convert_i64_u', 'L>I', [0xb5, 0xbc], 'V64', lambda a: s32(f32_bits_from_int(u64(a))))
op('i64-conv', 'i64.trunc_f64_s', 'L>L', [0xbf, 0xb0], 'VF', lambda b: s64(trunc_checked(b, -2**63, 2**63)))
op('i64-conv', 'i64.trunc_f64_u', 'L>L', [0xbf, 0xb1], 'VF', lambda b: s64(trunc_checked(b, 0, 2**64)))
op('i64-conv', 'i64.trunc_sat_f64_s', 'L>L', [0xbf, 0xfc, 0x06], 'VF', lambda b: s64(trunc_sat(b, -2**63, 2**63 - 1)))
op('i64-conv', 'i64.trunc_sat_f64_u', 'L>L', [0xbf, 0xfc, 0x07], 'VF', lambda b: s64(trunc_sat(b, 0, 2**64 - 1)))
for nm, c, f in [
    ('add', 0x6a, lambda a, b: s32(a + b)), ('sub', 0x6b, lambda a, b: s32(a - b)), ('mul', 0x6c, lambda a, b: s32(a * b)),
    ('div_s', 0x6d, lambda a, b: (need(b != 0 and not (a == -2**31 and b == -1)), s32(tdiv(a, b)))[1]),
    ('div_u', 0x6e, lambda a, b: (need(b != 0), s32(u32(a) // u32(b)))[1]),
    ('rem_s', 0x6f, lambda a, b: (need(b != 0), s32(a - b * tdiv(a, b)))[1]),
    ('rem_u', 0x70, lambda a, b: (need(b != 0), s32(u32(a) % u32(b)))[1]),
    ('and', 0x71, lambda a, b: s32(u32(a) & u32(b))), ('or', 0x72, lambda a, b: s32(u32(a) | u32(b))), ('xor', 0x73, lambda a, b: s32(u32(a) ^ u32(b))),
    ('shl', 0x74, lambda a, b: s32(u32(a) << (u32(b) % 32))), ('shr_s', 0x75, lambda a, b: s32(a >> (u32(b) % 32))),
    ('shr_u', 0x76, lambda a, b: s32(u32(a) >> (u32(b) % 32))),
    ('rotl', 0x77, lambda a, b: s32(((u32(a) << (u32(b) % 32)) | (u32(a) >> (32 - u32(b) % 32))) & M32)),
    ('rotr', 0x78, lambda a, b: s32(((u32(a) >> (u32(b) % 32)) | (u32(a) << (32 - u32(b) % 32))) & M32)),
]: op('i32-arith', 'i32.' + nm, 'II>I', [c], 'V32', f)
for nm, c, f in [
    ('eq', 0x46, lambda a, b: int(a == b)), ('ne', 0x47, lambda a, b: int(a != b)),
    ('lt_s', 0x48, lambda a, b: int(a < b)), ('lt_u', 0x49, lambda a, b: int(u32(a) < u32(b))),
    ('gt_s', 0x4a, lambda a, b: int(a > b)), ('gt_u', 0x4b, lambda a, b: int(u32(a) > u32(b))),
    ('le_s', 0x4c, lambda a, b: int(a <= b)), ('le_u', 0x4d, lambda a, b: int(u32(a) <= u32(b))),
    ('ge_s', 0x4e, lambda a, b: int(a >= b)), ('ge_u', 0x4f, lambda a, b: int(u32(a) >= u32(b))),
]: op('i32-cmp-unary', 'i32.' + nm, 'II>I', [c], 'V32', f)
op('i32-cmp-unary', 'i32.clz', 'I>I', [0x67], 'V32', lambda a: 32 - u32(a).bit_length())
op('i32-cmp-unary', 'i32.ctz', 'I>I', [0x68], 'V32', lambda a: 32 if a == 0 else (u32(a) & -u32(a)).bit_length() - 1)
op('i32-cmp-unary', 'i32.popcnt', 'I>I', [0x69], 'V32', lambda a: bin(u32(a)).count('1'))
op('i32-cmp-unary', 'i32.eqz', 'I>I', [0x45], 'V32', lambda a: int(a == 0))
op('i32-cmp-unary', 'i32.extend8_s', 'I>I', [0xc0], 'V32', lambda a: sext(a, 8))
op('i32-cmp-unary', 'i32.extend16_s', 'I>I', [0xc1], 'V32', lambda a: sext(a, 16))

# ---- NEW (DEVICE-1 audit): i64 shifts / rotates across the 32-bit boundary, appended AFTER the old groups so the old digests do not move.
# (a) variable amount, inputs 'VSH' (values x amounts out of ONE mixed set: amounts 31/32/33, 16/47, 63/64/65, 127/128, a high word set, negative)
SHIFTS = [('shl', 0x86, lambda a, b: s64(u64(a) << (u64(b) % 64))), ('shr_s', 0x87, lambda a, b: s64(a >> (u64(b) % 64))),
          ('shr_u', 0x88, lambda a, b: s64(u64(a) >> (u64(b) % 64))),
          ('rotl', 0x89, lambda a, b: s64(((u64(a) << (u64(b) % 64)) | (u64(a) >> (64 - u64(b) % 64))) & M64)),
          ('rotr', 0x8a, lambda a, b: s64(((u64(a) >> (u64(b) % 64)) | (u64(a) << (64 - u64(b) % 64))) & M64))]
for nm, c, f in SHIFTS: op('i64-shift-var', 'i64.' + nm + ' (VSH)', 'LL>L', [c], 'VSH', f, export='i64_' + nm + '_vsh')
# (b) constant amount: Liftoff and TurboFan lower `x shift CONST` through different (special-cased) paths than a variable amount
CONST_AMOUNTS = [0, 1, 31, 32, 33, 63, 64, 65, 0x100000020]
for nm, c, f in SHIFTS:
    for k in CONST_AMOUNTS:
        op('i64-shift-const', 'i64.%s const %d' % (nm, k), 'L>L', [0x42] + sleb(k) + [c], 'VSH', (lambda f, k: lambda a: f(a, k))(f, k), export='i64_%s_c%d' % (nm, k))

# ---------------------------------------------------------------- input sets
V64 = [0, 1, -1, 2, -2, 3, 5, 7, 10, 255, 256, 65535, 65536, 0x7fffffff, 0x80000000, 0xffffffff, 0x100000000, -0x80000000, -0x80000001,
       0x7fffffffffffffff, -0x8000000000000000, 0x123456789abcdef0, -0x123456789abcdef1, 0x100000001, s64(0xfffffffeffffffff),
       (1 << 60) + (1 << 36) + 1,           # f32.convert_i64_s: direct rounding goes UP, through-double rounding goes DOWN
       s64((1 << 63) + (1 << 39) + 1)]      # the same for f32.convert_i64_u (an unsigned value above 2^63)
V32 = [0, 1, -1, 2, -2, 3, 7, 31, 32, 33, 255, 256, 0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff, -0x80000000, 0x12345678, -0x12345679,
       0x55555555, s32(0xaaaaaaaa), 1000000007, -999999937]
DBL = [0.0, -0.0, 0.5, -0.5, 1.0, -1.0, 1.5, -1.5, 0.99999999, -0.99999999, 123456789.987, -123456789.987, 4294967296.0, 4294967295.5,
       2.0**53, 2.0**53 + 2, 2.0**62, 2.0**63 - 1024, 2.0**63, -(2.0**63), -(2.0**63) - 2048, 2.0**64 - 2048, 2.0**64, 1e30, -1e30,
       math.inf, -math.inf]
VF = [dbits(d) for d in DBL] + [0x7ff8000000000000, s64(0xfff8000000000001)]      # two NaN bit patterns
assert sum(1 for a in V64 if f32_bits_from_int(a) != f32_bits_via_double(a)) >= 1, 'no double-rounding witness among V64 (signed)'
assert sum(1 for a in V64 if f32_bits_from_int(u64(a)) != f32_bits_via_double(u64(a))) >= 1, 'no double-rounding witness among V64 (unsigned)'
# shift set: every value is used as the shifted value AND as the amount (all pairs). Amounts mod 64 covered: 0,1,2,3,16,31,32,33,47,63 (+ 64..65, 127, 128, a
# high word set (0x100000020 -> 32), negatives (-1 -> 63, -32 -> 32, -33 -> 31)). Values: bits on both sides of the word boundary.
VSH = [0, 1, -1, 2, 3, 16, 31, 32, 33, 47, 63, 64, 65, 127, 128, 0x7fffffff, 0x80000000, 0xffffffff, 0x100000000, 0x100000001, 0x100000020,
       0x7fffffffffffffff, -0x8000000000000000, 0x123456789abcdef0, -0x123456789abcdef1, s64(0xffffffff00000000), s64(0x8000000000000020), -32, -33]
SETS = {'V64': V64, 'V32': V32, 'VF': VF, 'VSH': VSH}

def fnv(h, s):
    for c in s:
        h ^= ord(c); h = (h * 16777619) & 0xffffffff
    return h
def digest(o):
    inp = SETS[o['inputs']]; ar = len(o['sig'].split('>')[0])
    h = 2166136261; traps = 0; n = 0
    tuples = [(a, b) for a in inp for b in inp] if ar == 2 else [(a,) for a in inp]
    for t in tuples:
        n += 1
        try: tok = str(o['ref'](*t))
        except Trap: traps += 1; tok = 'T'
        h = fnv(h, tok + ',')
    return '%08x/%d/%d' % (h, n, traps)
ext_ans = {o['fn']: digest(o) for o in OPS}

# ---------------------------------------------------------------- the ext module
fn_list = []      # (export name, params, results, body bytes incl. locals decl but excluding size)
for o in OPS:
    ps, rs = o['sig'].split('>')
    body = [0]       # no locals
    for i in range(len(ps)): body += [0x20, i]
    body += o['code'] + [0x0b]
    fn_list.append((o['fn'], ps, rs, body))
fn_list += [
    ('ld32', 'I', 'I', [0, 0x20, 0, 0x28, 0x02, 0x00, 0x0b]),
    ('st32', 'II', '', [0, 0x20, 0, 0x20, 1, 0x36, 0x02, 0x00, 0x0b]),
    ('ld64', 'I', 'L', [0, 0x20, 0, 0x29, 0x03, 0x00, 0x0b]),
    ('st64', 'IL', '', [0, 0x20, 0, 0x20, 1, 0x37, 0x03, 0x00, 0x0b]),
    ('grow', 'I', 'I', [0, 0x20, 0, 0x40, 0x00, 0x0b]),
    ('size', '', 'I', [0, 0x3f, 0x00, 0x0b]),
    # spin(n): x = 0; i = 0; do { x = x*1664525 + 1013904223; i++ } while (i < n unsigned); return x      (runs max(n,1) times)
    ('spin', 'I', 'I', [1, 2, I32] + [0x03, 0x40, 0x20, 2, 0x41] + sleb(1664525) + [0x6c, 0x41] + sleb(1013904223) +
                      [0x6a, 0x21, 2, 0x20, 1, 0x41, 1, 0x6a, 0x22, 1, 0x20, 0, 0x49, 0x0d, 0, 0x0b, 0x20, 2, 0x0b]),
    # memloop(n): i = 0; s = 0; do { m[(i*4)&0xfffc] = m[((i+1)*4)&0xfffc] + i; s += m[(i*8)&0xfffc]; i++ } while (i < n unsigned); return s
    # (word accesses inside the first 64 KiB page, every one bounds-checked: a loop where Liftoff's code is about 3x slower than TurboFan's)
    ('memloop', 'I', 'I', [1, 2, I32] + [0x03, 0x40] +
                      [0x20, 1, 0x41, 4, 0x6c, 0x41] + sleb(0xfffc) + [0x71] +
                      [0x20, 1, 0x41, 1, 0x6a, 0x41, 4, 0x6c, 0x41] + sleb(0xfffc) + [0x71, 0x28, 2, 0, 0x20, 1, 0x6a, 0x36, 2, 0] +
                      [0x20, 2, 0x20, 1, 0x41, 8, 0x6c, 0x41] + sleb(0xfffc) + [0x71, 0x28, 2, 0, 0x6a, 0x21, 2] +
                      [0x20, 1, 0x41, 1, 0x6a, 0x22, 1, 0x20, 0, 0x49, 0x0d, 0, 0x0b, 0x20, 2, 0x0b]),
]

# ---- NEW (DEVICE-1 audit): narrow / extending / unaligned / float / offset-overflow / constant-index memory accesses.
# One descriptor table drives BOTH the wasm body and the python model below (a descriptor error shows up as a python-vs-Node disagreement in node_check).
#   kind 'ld': (idx | const idx) [+ memarg offset] -> load; 'st': idx, value -> store; 'cp': f32/f64 load then store (bits only, no float arithmetic, no NaN canonicalisation);
#   'ldf': float load + reinterpret (returns the bits); 'stf': reinterpret + float store (writes the given bits)
MEMFNS = {}
def mfn(name, kind, opc, align, size, res='I', signed=False, off=0, const=None):
    MEMFNS[name] = dict(kind=kind, opc=opc, align=align, size=size, res=res, signed=signed, off=off, const=const)
for nm, opc, al, size, res, sg in [('ld8_s', 0x2c, 0, 1, 'I', True), ('ld8_u', 0x2d, 0, 1, 'I', False), ('ld16_s', 0x2e, 1, 2, 'I', True), ('ld16_u', 0x2f, 1, 2, 'I', False),
                                   ('ld64_8s', 0x30, 0, 1, 'L', True), ('ld64_8u', 0x31, 0, 1, 'L', False), ('ld64_16s', 0x32, 1, 2, 'L', True), ('ld64_16u', 0x33, 1, 2, 'L', False),
                                   ('ld64_32s', 0x34, 2, 4, 'L', True), ('ld64_32u', 0x35, 2, 4, 'L', False),
                                   ('ld32', 0x28, 2, 4, 'I', True), ('ld64', 0x29, 3, 8, 'L', True)]:
    mfn(nm, 'ld', opc, al, size, res, sg)
for nm, opc, al, size, vt in [('st8', 0x3a, 0, 1, 'I'), ('st16', 0x3b, 1, 2, 'I'), ('st32', 0x36, 2, 4, 'I'),
                              ('st64_8', 0x3c, 0, 1, 'L'), ('st64_16', 0x3d, 1, 2, 'L'), ('st64_32', 0x3e, 2, 4, 'L'), ('st64', 0x37, 3, 8, 'L')]:
    mfn(nm, 'st', opc, al, size, vt)
mfn('f32_copy', 'cp', (0x2a, 0x38), 2, 4); mfn('f64_copy', 'cp', (0x2b, 0x39), 3, 8)          # f32.load/store, f64.load/store with the natural alignment HINT
mfn('f32_copy_a0', 'cp', (0x2a, 0x38), 0, 4); mfn('f64_copy_a0', 'cp', (0x2b, 0x39), 0, 8)    # ... and with hint 0 (never changes the semantics)
# static offsets (memarg offset immediate): 65532/65535 are inside the 1-page minimum, 196604 is inside the 3-page MAXIMUM only (dynamic check until grown),
# 0xfffffffc / 0xffffffff / 0xfffffff8 can never be in bounds (V8 emits an unconditional trap; a 32-bit target must not wrap the 32-bit sum)
mfn('ld32_o65532', 'ld', 0x28, 2, 4, off=65532); mfn('ld8_o65535', 'ld', 0x2d, 0, 1, off=65535); mfn('ld32_o196604', 'ld', 0x28, 2, 4, off=196604)
mfn('ld32_o4G4', 'ld', 0x28, 2, 4, off=0xfffffffc); mfn('ld8_o4G1', 'ld', 0x2d, 0, 1, off=0xffffffff); mfn('ld64_o4G8', 'ld', 0x29, 3, 8, 'L', off=0xfffffff8)
mfn('st32_o4G4', 'st', 0x36, 2, 4, 'I', off=0xfffffffc)
# constant address operand (Liftoff folds `i32.const A; load` with IndexStaticallyInBounds, TurboFan folds it too): in bounds, unaligned, at the edge, past it, + offset overflow
mfn('ld32_c1', 'ld', 0x28, 2, 4, const=1); mfn('ld64_c3', 'ld', 0x29, 3, 8, 'L', const=3); mfn('ld16_s_c65534', 'ld', 0x2e, 1, 2, signed=True, const=65534)
mfn('ld16_u_c65535', 'ld', 0x2f, 1, 2, const=65535); mfn('ld32_c65533', 'ld', 0x28, 2, 4, const=65533); mfn('ld8_u_c65536', 'ld', 0x2d, 0, 1, const=65536)
mfn('st32_c65533', 'st', 0x36, 2, 4, 'I', const=65533); mfn('st16_c65535', 'st', 0x3b, 1, 2, 'I', const=65535); mfn('st8_c65535', 'st', 0x3a, 0, 1, 'I', const=65535)
mfn('ld32_c8_o4G4', 'ld', 0x28, 2, 4, const=8, off=0xfffffffc); mfn('ld8_c1_o65535', 'ld', 0x2d, 0, 1, const=1, off=65535); mfn('ld8_c0_o65535', 'ld', 0x2d, 0, 1, const=0, off=65535)
# float loads/stores whose VALUE crosses between the integer and the float register file (ARM32: vmov r<->s/d): f32.load + i32.reinterpret_f32, f64.load +
# i64.reinterpret_f64 return the exact bits to JS; f32.reinterpret_i32 / f64.reinterpret_i64 + store write exact bits (the spec requires reinterpret, load and
# store to preserve every bit pattern, NaN payloads included: no float ARITHMETIC anywhere, so there is nothing to canonicalise). _a0: alignment hint 0.
mfn('f32_ld_bits', 'ldf', (0x2a, 0xbc), 2, 4, 'I'); mfn('f64_ld_bits', 'ldf', (0x2b, 0xbd), 3, 8, 'L')
mfn('f32_ld_bits_a0', 'ldf', (0x2a, 0xbc), 0, 4, 'I'); mfn('f64_ld_bits_a0', 'ldf', (0x2b, 0xbd), 0, 8, 'L')
mfn('f32_st_bits', 'stf', (0xbe, 0x38), 2, 4, 'I'); mfn('f64_st_bits', 'stf', (0xbf, 0x39), 3, 8, 'L')
mfn('f32_st_bits_a0', 'stf', (0xbe, 0x38), 0, 4, 'I'); mfn('f64_st_bits_a0', 'stf', (0xbf, 0x39), 0, 8, 'L')
MEMFN_SIGS = {}
for nm, d in MEMFNS.items():
    if nm in ('ld32', 'ld64', 'st32', 'st64'): continue          # already in fn_list (hand-written bodies above)
    memarg = [d['align']] + leb(d['off'])
    addr = [0x41] + sleb(s32(d['const'])) if d['const'] is not None else [0x20, 0]
    if d['kind'] == 'ld':
        ps = '' if d['const'] is not None else 'I'; rs = d['res']; body = [0] + addr + [d['opc']] + memarg + [0x0b]
    elif d['kind'] == 'st':
        ps = ('' if d['const'] is not None else 'I') + d['res']; rs = ''
        body = [0] + addr + [0x20, 0 if d['const'] is not None else 1, d['opc']] + memarg + [0x0b]
    elif d['kind'] == 'ldf':
        ps, rs = 'I', d['res']; body = [0, 0x20, 0, d['opc'][0]] + memarg + [d['opc'][1], 0x0b]
    elif d['kind'] == 'stf':
        ps, rs = 'I' + d['res'], ''; body = [0, 0x20, 0, 0x20, 1, d['opc'][0], d['opc'][1]] + memarg + [0x0b]
    else:   # cp(src, dst): dst, then src, then load, then store
        ps, rs = 'II', ''; body = [0, 0x20, 1, 0x20, 0, d['opc'][0]] + memarg + [d['opc'][1]] + memarg + [0x0b]
    MEMFN_SIGS[nm] = (ps, rs)
    fn_list.append((nm, ps, rs, body))
for nm in ('ld32', 'ld64', 'st32', 'st64'):
    MEMFN_SIGS[nm] = ('I', 'I') if nm == 'ld32' else ('II', '') if nm == 'st32' else ('I', 'L') if nm == 'ld64' else ('IL', '')
MEMFN_SIGS['grow'] = ('I', 'I'); MEMFN_SIGS['size'] = ('', 'I')
types = []
def type_index(ps, rs):
    t = (ps, rs)
    if t not in types: types.append(t)
    return types.index(t)
tidx = [type_index(ps, rs) for (_, ps, rs, _) in fn_list]
type_sec = leb(len(types))
for ps, rs in types:
    type_sec += [0x60] + leb(len(ps)) + [VT[c] for c in ps] + leb(len(rs)) + [VT[c] for c in rs]
func_sec = leb(len(fn_list)) + sum([leb(i) for i in tidx], [])
mem_sec = [1, 0x01, 1, 3]                       # one memory: min 1 page, max 3 pages
exp_sec = leb(len(fn_list) + 1)
for i, (nm, *_rest) in enumerate(fn_list): exp_sec += name(nm) + [0] + leb(i)      # FIX: the function index is a LEB128 (the old 84 functions all had i < 128)
exp_sec += name('mem') + [2, 0]
code_sec = leb(len(fn_list)) + sum([leb(len(b)) + b for (_, _, _, b) in fn_list], [])
ext_bytes = bytes([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0] + section(1, type_sec) + section(3, func_sec) + section(5, mem_sec) + section(7, exp_sec) + section(10, code_sec))

# spin answers
def spin_ref(n):
    x = 0
    for _ in range(max(n, 1)): x = (x * 1664525 + 1013904223) & M32
    return s32(x)
SPIN_N = [1, 1000, 100000, 2000000]
spin_ans = {str(n): spin_ref(n) for n in SPIN_N}
def memloop_ref(n):
    m = [0] * 16384; i = 0; sm = 0
    for _ in range(max(n, 1)):
        dst = ((i * 4) & M32) & 0xfffc
        src = ((((i + 1) & M32) * 4) & M32) & 0xfffc
        m[dst >> 2] = s32(m[src >> 2] + i)
        sm = s32(sm + m[(((i * 8) & M32) & 0xfffc) >> 2])
        i = (i + 1) & M32
    return sm
MEMLOOP_N = [1, 20000, 100000]
MEMLOOP_TIMING_N = 3000000
memloop_ans = {str(n): memloop_ref(n) for n in MEMLOOP_N + [MEMLOOP_TIMING_N]}

# ---------------------------------------------------------------- NEW: the memory script. Every WANT below is computed by this python model (exact integers,
# bytearray memory, little-endian), never by a browser. A step is [phase, fn, [args as decimal strings], want]; want = decimal result | 'ok' (no result) | 'T' (trap);
# the pseudo-function '@bytes' [start, n] compares memory read through the JS API (Uint8Array over mem.buffer) with the model's bytes (hex): an independent route.
class MemModel:
    MAXP = 3
    def __init__(s): s.m = bytearray(65536)
    def call(s, fn, args):
        if fn == 'grow':
            old = len(s.m) // 65536; n = u32(args[0])
            if old + n > s.MAXP: return '-1'
            s.m.extend(bytes(65536 * n)); return str(old)
        if fn == 'size': return str(len(s.m) // 65536)
        d = MEMFNS[fn]; a = list(args)
        if d['kind'] == 'cp':
            src, dst = u32(a[0]), u32(a[1])
            if src + d['size'] > len(s.m) or dst + d['size'] > len(s.m): return 'T'
            b = bytes(s.m[src:src + d['size']]); s.m[dst:dst + d['size']] = b; return 'ok'
        ea = u32(d['const'] if d['const'] is not None else a.pop(0)) + d['off']      # exact effective address: index (u32) + offset, no wrap-around
        if ea + d['size'] > len(s.m): return 'T'
        if d['kind'] in ('ld', 'ldf'):
            v = int.from_bytes(s.m[ea:ea + d['size']], 'little', signed=d['signed'])
            return str(s32(v) if d['res'] == 'I' else s64(v))
        v = a.pop(0); s.m[ea:ea + d['size']] = (v & ((1 << (8 * d['size'])) - 1)).to_bytes(d['size'], 'little'); return 'ok'
MM = MemModel(); SCRIPT = []
def S(phase, fn, *args): SCRIPT.append([phase, fn, [str(x) for x in args], MM.call(fn, args)])
def BYTES(phase, start, n): SCRIPT.append([phase, '@bytes', [str(start), str(n)], bytes(MM.m[start:start + n]).hex()])
def le64(bs): return int.from_bytes(bytes(bs), 'little', signed=True)
W0, W1, W2 = le64([1, 2, 3, 4, 5, 6, 7, 8]), le64([0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0]), le64([0x80, 0xff, 0x00, 0x7f, 0x00, 0x80, 0xff, 0x7f])
def val(k): return s64((0x9E3779B97F4A7C15 * (k + 1)) & M64)
def arg_for(fn, v): return s32(v & M32) if MEMFNS[fn]['res'] == 'I' else v
PH = 'A loads: narrow, extending, unaligned (all in bounds)'
S(PH, 'st64', 0, W0); S(PH, 'st64', 8, W1); S(PH, 'st64', 16, W2)
for a in range(0, 17):
    for fn in ('ld8_s', 'ld8_u', 'ld16_s', 'ld16_u', 'ld32', 'ld64', 'ld64_8s', 'ld64_8u', 'ld64_16s', 'ld64_16u', 'ld64_32s', 'ld64_32u'): S(PH, fn, a)
BYTES(PH, 0, 24)
PH = 'B stores: narrow, truncating, unaligned (read back as two doublewords + the JS view)'
k = 0
for fn, addrs in [('st8', [0, 1, 7, 8, 13]), ('st16', [0, 1, 2, 3, 7, 9, 15]), ('st32', [0, 1, 2, 3, 5, 6, 7, 9]), ('st64_8', [0, 1, 7]),
                  ('st64_16', [1, 3, 7]), ('st64_32', [1, 2, 3, 5, 7]), ('st64', [1, 2, 3, 4, 5, 6, 7, 9])]:
    for a in addrs:
        v = val(k); k += 1
        S(PH, fn, a, arg_for(fn, v)); S(PH, 'ld64', a & ~7); S(PH, 'ld64', (a & ~7) + 8)
BYTES(PH, 0, 32)
PH = 'C the page end: every width, in bounds vs trapping, a trapped store writes nothing'
S(PH, 'st64', 65528, W1)
for fn, a in [('ld8_u', 65535), ('ld8_s', 65535), ('ld8_u', 65536), ('ld8_u', -1), ('ld8_u', 2147483647), ('ld16_u', 65534), ('ld16_s', 65534), ('ld16_u', 65535), ('ld16_s', 65536),
              ('ld32', 65532), ('ld32', 65533), ('ld32', 65535), ('ld32', -2147483648), ('ld64', 65528), ('ld64', 65529), ('ld64', 65535),
              ('ld64_8u', 65535), ('ld64_8s', 65535), ('ld64_8s', 65536), ('ld64_16u', 65534), ('ld64_16s', 65534), ('ld64_16s', 65535), ('ld64_32u', 65532), ('ld64_32s', 65532), ('ld64_32s', 65533),
              ('f32_ld_bits', 65532), ('f32_ld_bits', 65533), ('f32_ld_bits_a0', 65535), ('f64_ld_bits', 65528), ('f64_ld_bits', 65529), ('f64_ld_bits_a0', 65535), ('f64_ld_bits', -1)]:
    S(PH, fn, a)
for fn, a in [('st8', 65535), ('st8', 65536), ('st8', -1), ('st16', 65534), ('st16', 65535), ('st32', 65532), ('st32', 65533), ('st32', 65535), ('st64', 65528), ('st64', 65529),
              ('st64_8', 65535), ('st64_8', 65536), ('st64_16', 65534), ('st64_16', 65535), ('st64_32', 65532), ('st64_32', 65533),
              ('f32_st_bits', 65532), ('f32_st_bits', 65533), ('f32_st_bits_a0', 65535), ('f64_st_bits', 65528), ('f64_st_bits', 65529), ('f64_st_bits_a0', 65535)]:
    v = val(100 + len(SCRIPT)); S(PH, fn, a, arg_for(fn, v)); S(PH, 'ld64', 65528)
for fn, args in [('f32_copy', (65532, 0)), ('f32_copy', (65533, 0)), ('f32_copy', (0, 65533)), ('f64_copy', (65528, 0)), ('f64_copy', (65529, 0)), ('f64_copy', (0, 65529)), ('f64_copy', (0, 65528))]:
    S(PH, fn, *args)
BYTES(PH, 0, 16); BYTES(PH, 65520, 16)
PH = 'D float loads and stores: bit patterns incl. NaN payloads, -0, denormals, unaligned, hint 0'
P32 = [0x00000000, 0x80000000, 0x3f800000, 0x7f800000, 0xff800000, 0x7fc00000, 0x7fa00001, 0xffc00123, 0x00000001, 0x807fffff, 0x7f7fffff, 0x12345678]
P64 = [0x0, 0x8000000000000000, 0x3ff0000000000000, 0x7ff0000000000000, 0xfff0000000000000, 0x7ff8000000000000, 0x7ff4000000000001, 0xfff8000000000123,
       0x1, 0x800fffffffffffff, 0x7fefffffffffffff, 0x123456789abcdef0]
for i, p in enumerate(P32):
    for s in (0, 1, 2, 3):
        slot = i * 4 + s; src = 1024 + 32 * slot + s; dst = 4096 + 32 * slot + (s + 1) % 4; cp = 'f32_copy' if (i + s) % 2 == 0 else 'f32_copy_a0'
        S(PH, 'st32', src, s32(p)); S(PH, cp, src, dst); S(PH, 'ld32', dst); BYTES(PH, dst - 4, 12)
for i, p in enumerate(P64):
    for s in (0, 1, 3, 4, 5, 7):
        slot = i * 6 + s; src = 8192 + 32 * slot + s; dst = 16384 + 32 * slot + (s + 3) % 8; cp = 'f64_copy' if (i + s) % 2 == 0 else 'f64_copy_a0'
        S(PH, 'st64', src, s64(p)); S(PH, cp, src, dst); S(PH, 'ld64', dst); BYTES(PH, dst - 8, 24)
S(PH, 'st64', 30000, s64(0x7ff4000000000001)); S(PH, 'f64_copy', 30000, 30001); BYTES(PH, 29992, 24)       # overlapping: the load completes before the store
S(PH, 'st32', 30100, s32(0x7fa00001)); S(PH, 'f32_copy', 30100, 30102); BYTES(PH, 30096, 16)
# value-returning float accesses (f32/f64 load + reinterpret, reinterpret + store): the exact bits cross the integer <-> float register boundary in both directions
for i, p in enumerate(P32):
    for s in (0, 1, 2, 3):
        a = 40000 + 64 * (i * 4 + s) + s
        st = 'f32_st_bits' if (i + s) % 2 == 0 else 'f32_st_bits_a0'; ld = 'f32_ld_bits' if (i + s) % 3 else 'f32_ld_bits_a0'
        S(PH, st, a, s32(p)); S(PH, ld, a); S(PH, 'ld32', a); BYTES(PH, a - 4, 12)
for i, p in enumerate(P64):
    for s in (0, 1, 3, 4, 5, 7):
        a = 44000 + 64 * (i * 6 + s) + s
        st = 'f64_st_bits' if (i + s) % 2 == 0 else 'f64_st_bits_a0'; ld = 'f64_ld_bits' if (i + s) % 3 else 'f64_ld_bits_a0'
        S(PH, st, a, s64(p)); S(PH, ld, a); S(PH, 'ld64', a); BYTES(PH, a - 8, 24)
PH = 'E offsets, constant addresses, growth: the 32-bit effective address must not wrap'
for fn, a in [('ld32_o65532', 0), ('ld32_o65532', 1), ('ld8_o65535', 0), ('ld8_o65535', 1), ('ld32_o196604', 0), ('ld32_o4G4', 0), ('ld32_o4G4', 8), ('ld32_o4G4', -8),
              ('ld8_o4G1', 0), ('ld8_o4G1', 1), ('ld64_o4G8', 0), ('ld64_o4G8', 16)]:
    S(PH, fn, a)
S(PH, 'st32_o4G4', 8, 0x5a5a5a5a); S(PH, 'ld32', 4); S(PH, 'ld32', 8); S(PH, 'st32_o4G4', -4, 0x1badf00d); S(PH, 'ld32', 0)
for fn in ('ld32_c1', 'ld64_c3', 'ld16_s_c65534', 'ld16_u_c65535', 'ld32_c65533', 'ld8_u_c65536', 'ld32_c8_o4G4', 'ld8_c1_o65535', 'ld8_c0_o65535'): S(PH, fn)
for fn, v in [('st32_c65533', 0x11223344), ('st16_c65535', 0x5566), ('st8_c65535', 0x77)]: S(PH, fn, v)
S(PH, 'ld64', 65528); BYTES(PH, 65520, 16)
S(PH, 'size'); S(PH, 'grow', 2); S(PH, 'size'); S(PH, 'ld32_o196604', 0); S(PH, 'st32', 196604, s32(0x7eadbeef)); S(PH, 'ld32_o196604', 0); S(PH, 'ld32_o196604', 1)
S(PH, 'ld32', 196604); S(PH, 'ld32', 196605); S(PH, 'ld8_u', 196607); S(PH, 'ld8_u', 196608); S(PH, 'grow', 1); S(PH, 'size')
BYTES(PH, 196592, 16)
MEM_FNS_SIG = {nm: MEMFN_SIGS[nm][0] + '>' + MEMFN_SIGS[nm][1] for nm in MEMFN_SIGS}
assert all(s[1] == '@bytes' or s[1] in MEM_FNS_SIG for s in SCRIPT)

# ---------------------------------------------------------------- the host's record of the SHAPE of what the page must execute (BATCH-DEVICE-1 follow-up)
# wasm.html emits one row per phase / per group by iterating the data, so a data file that lost a phase or a group would make those rows
# disappear instead of failing. These lists are written next to the script / the operator list; the page counts what it actually executed and
# fails unless the executed runs, in order, equal them (lib/wasm-cases.js memShape / opsShape; node_check.js runs the same functions).
def runs(labels):
    out = []
    for l in labels:
        if out and out[-1][0] == l: out[-1][1] += 1
        else: out.append([l, 1])
    return out
MEM_PHASES = runs([s[0] for s in SCRIPT])
OPS_GROUPS = runs([o['group'] for o in OPS])
assert len(MEM_PHASES) == len({p[0] for p in MEM_PHASES}), 'a phase of the memory script is not one contiguous run'
assert len(OPS_GROUPS) == len({g[0] for g in OPS_GROUPS}), 'an operator group is not one contiguous run'
assert sum(n for _, n in MEM_PHASES) == len(SCRIPT) and sum(n for _, n in OPS_GROUPS) == len(OPS)
assert {s[1] for s in SCRIPT if s[1] != '@bytes'} == set(MEM_FNS_SIG), 'every memory function must be called by the script (the page asserts it)'
assert len(ext_ans) == len(OPS), 'operator function names must be unique: one host digest per operator'

# ---------------------------------------------------------------- node's answers (second pass)
node_ext = None
p = HERE + '/node-answers.json'
if os.path.exists(p): node_ext = json.load(open(p))

def b64(b): return base64.b64encode(b).decode()
data = {
    'about': 'Data for wasm.html. s10 = the module and answers of s10/jit/wasm.js (V8SIM-2 3.6); ext = extra cases written for this kit. Generated by ' + TOOL + ' - do not edit by hand.',
    's10': {
        'b64': b64(s10_bytes), 'bytes': len(s10_bytes), 'sha256': hashlib.sha256(s10_bytes).hexdigest(),
        'exports': ['add32', 'mul32', 'div32', 'add64', 'mul64', 'div64'],
        'host_python': {k: S10.E[k] for k in ('wasm_i32', 'wasm_i64')},        # s10/jit/expected.py == expected.txt
        'host_node_x64': {k: node_ans[k] for k in ('wasm_i32', 'wasm_i64')},   # the original wasm.js run in Node (V8 on x64) == node-x64.txt
        'source': 's10/jit/wasm.js module bytes (run unchanged in Node); answers: s10/jit/expected.py (== expected.txt == node-x64.txt == a live Node run: ' + gt['node'] + ' V8 ' + gt['v8'] + ' ' + gt['arch'] + ')',
    },
    'ext': {
        'b64': b64(ext_bytes), 'bytes': len(ext_bytes), 'sha256': hashlib.sha256(ext_bytes).hexdigest(),
        'sets': {k: [str(v) for v in vs] for k, vs in SETS.items()},
        'ops': [{'name': o['name'], 'fn': o['fn'], 'group': o['group'], 'sig': o['sig'], 'inputs': o['inputs']} for o in OPS],
        'expect': {'groups': OPS_GROUPS, 'ops': len(OPS)},       # the host's record of the operator groups the page must digest (see the SHAPE comment above)
        'host_python': ext_ans,                      # digest 'fnv1a32-hex/inputs/traps' of every result, computed by gen_wasm_data.py from first principles
        'host_node_x64': node_ext['digests'] if node_ext else None,
        'spin': {'n': SPIN_N, 'host_python': spin_ans, 'host_node_x64': node_ext['spin'] if node_ext else None},
        'memloop': {'n': MEMLOOP_N, 'timing_n': MEMLOOP_TIMING_N, 'host_python': memloop_ans, 'host_node_x64': node_ext['memloop'] if node_ext else None},
        'mem': {'fns': MEM_FNS_SIG, 'expect': {'phases': MEM_PHASES, 'steps': len(SCRIPT), 'fns': len(MEM_FNS_SIG)}, 'script': '@@SCRIPT@@'},       # NEW: wants from the python MemModel; node_check.js replays the same script in Node (V8 x64)
        'source': TOOL + ': python reference semantics (exact integers; i64->f32 rounded directly), cross-checked with node_check.js (Node ' + gt['node'] + ' V8 ' + gt['v8'] + ' ' + gt['arch'] + ')',
    },
    'tool': {'nonce_section': 'kit-nonce'},
}
out = KIT + '/data/wasm-data.js'
with open(out, 'w', encoding='utf-8', newline='\n') as w:
    w.write('// Generated by ' + TOOL + ' - data for wasm.html (module bytes inline as base64, the host\'s answers). Do not edit by hand.\n')
    w.write('window.KIT_WASM = ' + json.dumps(data, indent=1).replace('"@@SCRIPT@@"', '[\n' + (',' + chr(10)).join(json.dumps(x, separators=(',', ':')) for x in SCRIPT) + '\n ]') + ';\n')   # one compact row per step
print('wrote', out, os.path.getsize(out), 'bytes; ext ops', len(OPS), 'ext module', len(ext_bytes), 'bytes; s10 module', len(s10_bytes), 'bytes; node answers merged:', bool(node_ext))

# ---------------------------------------------------------------- the binary test files (not loaded by the page; the same bytes are inline in the data file)
# manifest/wasm.tsv is written by manifest.py (it hashes every file the page owns, hand-written ones included, so it must run LAST).
os.makedirs(KIT + '/media', exist_ok=True)
open(KIT + '/media/wasm-s10.wasm', 'wb').write(s10_bytes)
open(KIT + '/media/wasm-ext.wasm', 'wb').write(ext_bytes)
print('wrote media/wasm-s10.wasm, media/wasm-ext.wasm (now run manifest.py)')
