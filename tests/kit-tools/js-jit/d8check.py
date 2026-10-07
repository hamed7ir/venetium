"""js-jit page logic replayed under a d8 (no browser): same preamble order as js-jit.html, same flags as kit/lib/configs.js, same checks.
usage: python d8check.py <d8.exe> [--arch ARCH] [--build BUILD]
  e.g. python d8check.py F:\\cr\\src\\out\\x86-rt21\\d8.exe                       (ia32: the Maglev configurations print n/a-ia32, as the page does)
       python d8check.py F:\\cr\\src\\out\\x86-arm-sim\\d8.exe --arch arm32       (ARM32 simulator, Maglev built: they prove maglev)
       python d8check.py F:\\cr\\src\\out\\x86-arm-sim\\d8.exe --arch arm32 --build arm   (the same with a packaged build record)
ARCH = the CPU the browser would report through navigator.userAgentData (what Kit.arch() in lib/kit.js sees):
  x86 (architecture x86, bitness 64, wow64 true: the ia32 browser on the build server; the default), x86-32 (x86 / 32 / false),
  x64native (x86 / 64 / false), arm32 (architecture '' / 64 / false: what Windows ARM32 reports), arm64, nouad (no navigator.userAgentData).
BUILD = lib/build.js as the packaging step would write it: none (null, the record's copy; the default), arm (arm, has_maglev true), x86 (x86,
  has_maglev false), armfalse (arm, has_maglev false: a record that says an ARM build has no Maglev).
The Maglev decision is the page's (js-jit.html): %IsMaglevEnabled() true -> prove Maglev (mode force-maglev; with a build record that says
has_maglev false the page also FAILS, printed as KIT-BUILD-MISMATCH); false -> n/a-ia32 ONLY when the configuration has ia32 'n/a' and the build
record says has_maglev false AND v8_current_cpu x86 (has_maglev false for any other CPU is mode no-maglev = FAIL: V8 builds Maglev for it), or
(no record) the CPU is ia32 (architecture x86 and bitness 32 or wow64); anything else is mode no-maglev = FAIL.
This is a REPLAY of the logic, not the page: js-jit-d8-harness.js / js-jit-d8-matrix.js run the page's real inline script under d8 (with
the shipped Kit.arch) and are the stronger tool; this one is the quick look. It is NOT the browser and not real ARM32 hardware.
Per configuration it prints: rc, the answers that differ from the host's (bad_answers), typeof WebAssembly against the page's rule
(jitless: 'undefined', every other configuration: 'object'), the lite-mode bit rule for the interpreter configurations (bit 4096 of
%GetOptimizationStatus set in all 13 statuses iff --jitless), and the tier proof: none (natives off, or natives on but the configuration
selects no JS tier), n/a-ia32, or all 13 = wanted tier.
"""
import subprocess, sys, re, os, argparse
ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
ap.add_argument('d8')
ap.add_argument('--arch', default='x86', choices=['x86', 'x86-32', 'x64native', 'arm32', 'arm64', 'nouad'])
ap.add_argument('--build', default='none', choices=['none', 'arm', 'x86', 'armfalse'])
a = ap.parse_args()
d8 = os.path.abspath(a.d8); cwd = os.path.dirname(d8)
K = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'kit') + os.sep      # kit-tools/js-jit/../../kit/
txt = open(K + 'lib/configs.js', encoding='utf-8').read()
cfgs = {}
for m in re.finditer(r"^\s*'([\w-]+)':\s*\{(.*)\},?\s*$", txt, re.M):
    body = m.group(2)
    flags = re.findall(r"'([^']+)'", re.search(r"flags: \[(.*?)\]", body).group(1))
    natives = re.search(r"natives:\s*(true|false)", body).group(1) == 'true'
    tier = re.search(r"tier:\s*(null|'\w+')", body).group(1).strip("'")
    cfgs[m.group(1)] = dict(flags=flags, natives=natives, tier=None if tier == 'null' else tier,
                            nowasm='nowasm: true' in body, ia32_na="ia32: 'n/a'" in body)
data = open(K + 'data/js-jit-expected.js', encoding='utf-8').read()
E = dict(re.findall(r'"(\w+)": "([^"]*)"', data.split('window.JIT_NODE_X64')[0]))
N = dict(re.findall(r'"(\w+)": "([^"]*)"', data.split('window.JIT_NODE_X64')[1]))
SIN = 'sin_crosstier'
keys = list(dict.fromkeys(list(E) + list(N) + [SIN]))          # the page's KEYS: union of both objects
def ref(k): return N.get(k) if k == SIN else E.get(k)            # python answers for the 12, Node only for sin_crosstier
# Kit.arch(): ia32 = architecture 'x86' and (bitness '32' or wow64 true)
IA32 = {'x86': True, 'x86-32': True, 'x64native': False, 'arm32': False, 'arm64': False, 'nouad': False}[a.arch]
BUILD_HAS_MAGLEV = {'none': None, 'arm': True, 'x86': False, 'armfalse': False}[a.build]
BUILD_CPU = {'none': None, 'arm': 'arm', 'x86': 'x86', 'armfalse': 'arm'}[a.build]       # the record's v8_current_cpu
def maglev_built(flags):
    p = subprocess.run([d8] + flags + ['-e', 'print(%IsMaglevEnabled())'], cwd=cwd, capture_output=True, text=True, timeout=120)
    return p.stdout.strip() == 'true'
for name, c in cfgs.items():
    flags, natives, tier = c['flags'], c['natives'], c['tier']
    pre, mode, notes = [], 'plain', ''
    if natives and tier:                      # natives on + a tier to prove (the page's third branch); natives + tier null selects no JS tier
        pre = ['lib/js-jit-status.js']; mode = 'flags'
        if tier == 'turbofan': pre.append('lib/js-jit-force.js'); mode = 'force'
        if tier == 'maglev':
            if maglev_built(flags):
                pre.append('lib/js-jit-force-maglev.js'); mode = 'force-maglev'     # proven whatever the CPU report says
                if BUILD_HAS_MAGLEV is False: notes = ' KIT-BUILD-MISMATCH(build says has_maglev false, V8 has Maglev: the page FAILS)'
            elif c['ia32_na'] and ((BUILD_HAS_MAGLEV is False and BUILD_CPU == 'x86') or (BUILD_HAS_MAGLEV is None and IA32)): mode = 'na-ia32'
            else: mode = 'no-maglev'          # Maglev missing where it must exist: the page FAILS
    elif natives: mode = 'no-tier'
    cmd = [d8] + flags + ['-e', "print('page.typeof_wasm=' + typeof WebAssembly)"] + [K + x for x in pre] + [K + 'lib/js-jit-core.js']
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=900)
    kv = [l.strip().split('=', 1) for l in p.stdout.splitlines() if '=' in l]
    vals = {k: v for k, v in kv if not k.startswith('tier.') and k != 'page.typeof_wasm'}
    wasm = dict(kv).get('page.typeof_wasm')
    status = {k[5:]: v for k, v in kv if k.startswith('tier.')}
    reached = {k: v.split('(')[0] for k, v in status.items()}
    bad_vals = [k for k in keys if ref(k) is None or vals.get(k) != ref(k)]
    want_wasm = 'undefined' if c['nowasm'] else 'object'
    if mode in ('flags', 'force', 'force-maglev', 'no-maglev'): proof = 'proven' if all(reached.get(k) == (tier if mode != 'no-maglev' else 'maglev') for k in keys) else 'NOT PROVEN'
    else: proof = 'n/a (' + mode + ')'
    lite = ''
    if tier == 'interpreter' and mode == 'flags':     # jitless vs ignition: kLiteMode (1 << 12) set in every status iff --jitless
        want_lite = '--jitless' in flags
        got = [bool(re.search(r'\((\d+)\)$', status.get(k, '')) and int(re.search(r'\((\d+)\)$', status[k]).group(1)) & 4096) for k in keys]
        lite = f" lite_mode={'set' if all(got) else 'clear' if not any(got) else 'mixed'}(want {'set' if want_lite else 'clear'}){'' if all(g == want_lite for g in got) else ' MISMATCH'}"
    print(f"{name:16} rc={p.returncode} mode={mode:12} bad_answers={bad_vals} wasm={wasm}(want {want_wasm}){'' if wasm == want_wasm else ' MISMATCH'}{lite} "
          f"tier_wanted={tier} tiers_reached={sorted(set(reached.values()))} proof={proof} done={vals.get('done')}{notes}")
    if p.stderr.strip(): print('   stderr:', p.stderr.strip()[:200])
