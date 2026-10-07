# Step 2 of 2: generator for venetium\tests\kit\data\fonts-strings.js (page fonts.html). Not part of the kit; it only documents how the data was made.
#   python fonts-gen.py --paks <paks-dir> [--build F:/cr/src/out/x86-rt21] [--kit <kit dir>] [--sources <fonts-sources.tsv>]   (python 3)
# <paks-dir> is the output of fonts-dump.py. Defaults: --kit = ..\..\kit (relative to this file), --sources = fonts-sources.tsv next to this file.
# Source: the built GRIT paks gen\chrome\branded_strings_<locale>.pak (every translation the build contains, 84 locales), dumped with the tree's
# own tools/grit/pak_util.py (extract -t) by fonts-dump.py into <paks-dir>/<locale>/<IDS_NAME>.  Four messages that carry the product name:
#   IDS_ABOUT (app menu "About Venetium"), IDS_APPMENU_TOOLTIP, IDS_BROWSER_HUNGBROWSER_MESSAGE, IDS_DEFAULT_BROWSER_BUBBLE_DIALOG_TITLE.
# For every locale whose pak is also shipped in <build>\locales\<locale>.pak the script asserts that the 4 strings are in that file byte for byte.
# The expected strings therefore come from the build's own GRIT output, never from the browser under test. Reads the build read-only.
# PYTHONDONTWRITEBYTECODE=1. Re-running with the defaults reproduces data/fonts-strings.js and fonts-sources.tsv byte for byte.
import os, sys, json, hashlib, re, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser(description='generate kit/data/fonts-strings.js from the dumped branded_strings paks')
ap.add_argument('--paks', required=True, help='folder made by fonts-dump.py (one sub-folder per locale)')
ap.add_argument('--build', default='F:/cr/src/out/x86-rt21', help='build output folder the paks were dumped from (default: the x86-rt21 build)')
ap.add_argument('--kit', default=os.path.normpath(os.path.join(HERE, '..', '..', 'kit')), help='kit folder (default: ..\\..\\kit)')
ap.add_argument('--sources', default=os.path.join(HERE, 'fonts-sources.tsv'), help='where the pak sha256 table is written')
args = ap.parse_args()
B = args.build.replace('\\', '/').rstrip('/')
BN = os.path.basename(B)                       # e.g. x86-rt21 (named in the generated header)
W = args.paks.replace('\\', '/').rstrip('/')
KIT = args.kit.replace('\\', '/').rstrip('/')
IDS = ['IDS_ABOUT', 'IDS_APPMENU_TOOLTIP', 'IDS_BROWSER_HUNGBROWSER_MESSAGE', 'IDS_DEFAULT_BROWSER_BUBBLE_DIALOG_TITLE']
# script (Unicode Script_Extensions name the page tests with) per locale; every other locale is Latin script. en-XA / ar-XB are pseudo-locales (skipped).
SCRIPT = {
 'hi': 'Devanagari', 'ne': 'Devanagari', 'mr': 'Devanagari', 'bn': 'Bengali', 'as': 'Bengali', 'pa': 'Gurmukhi', 'gu': 'Gujarati',
 'or': 'Oriya', 'ta': 'Tamil', 'te': 'Telugu', 'kn': 'Kannada', 'ml': 'Malayalam', 'si': 'Sinhala', 'th': 'Thai', 'lo': 'Lao',
 'km': 'Khmer', 'my': 'Myanmar', 'ka': 'Georgian', 'hy': 'Armenian', 'am': 'Ethiopic', 'ar': 'Arabic', 'fa': 'Arabic', 'ur': 'Arabic',
 'he': 'Hebrew', 'ru': 'Cyrillic', 'uk': 'Cyrillic', 'bg': 'Cyrillic', 'be': 'Cyrillic', 'sr': 'Cyrillic', 'mk': 'Cyrillic',
 'kk': 'Cyrillic', 'ky': 'Cyrillic', 'mn': 'Cyrillic', 'el': 'Greek', 'zh-CN': 'Han', 'zh-TW': 'Han', 'zh-HK': 'Han',
 'ja': 'Japanese', 'ko': 'Korean',
}
ALLOWED = {'Japanese': ['Hiragana', 'Katakana', 'Han'], 'Korean': ['Hangul', 'Han']}
SKIP = {'en-XA', 'ar-XB'}
NAME_HI = '\u0935\u0947\u0928\u0947\u091f\u093f\u092f\u092e'     # the product name in Devanagari (Hindi, Nepali)

def norm(s):
    s = s.replace('&', '').replace('\r', ' ').replace('\n', ' ')
    return s.strip()

def esc(s):
    out = []
    for ch in s:
        c = ord(ch)
        if 0x20 <= c < 0x7f and ch not in '"\\':
            out.append(ch)
        elif c > 0xffff:
            c -= 0x10000
            out.append('\\u%04x\\u%04x' % (0xd800 + (c >> 10), 0xdc00 + (c & 0x3ff)))
        else:
            out.append('\\u%04x' % c)
    return '"' + ''.join(out) + '"'

def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()

locs = sorted(d for d in os.listdir(W) if d not in SKIP)
cases, latin_locs, latin_texts, srcs, problems = [], [], [], [], []
for loc in locs:
    raw = {}
    for i in IDS:
        raw[i] = open(f'{W}/{loc}/{i}', 'rb').read().decode('utf-8')
    texts = [norm(raw[i]) for i in IDS]
    for i, t in zip(IDS, texts):
        if 'Venetium' not in t and NAME_HI not in t:
            problems.append(f'{loc} {i}: no Venetium form in {ascii(t)}')
    gen = f'{B}/gen/chrome/branded_strings_{loc}.pak'
    shipped = os.path.exists(f'{B}/locales/{loc}.pak')
    inpak = None
    if shipped:
        data = open(f'{B}/locales/{loc}.pak', 'rb').read()
        inpak = all(raw[i].encode('utf-8') in data for i in IDS)
        if not inpak:
            problems.append(f'{loc}: strings are not byte-for-byte in locales/{loc}.pak')
    srcs.append((loc, gen, sha(gen), shipped, inpak))
    if loc in SCRIPT:
        c = {'loc': loc, 'script': SCRIPT[loc], 'shipped': shipped, 'texts': texts}
        if SCRIPT[loc] in ALLOWED:
            c['allowed'] = ALLOWED[SCRIPT[loc]]
        cases.append(c)
    else:
        latin_locs.append(loc)
        latin_texts.extend(texts)
cases.append({'loc': 'latin', 'script': 'Latin', 'locales': latin_locs, 'shipped': True, 'texts': latin_texts})

js = ['// Venetium test kit - page fonts.html data. GENERATED (venetium\\tests\\kit-tools\\fonts\\fonts-gen.py): the Venetium product-name strings of every translation',
      f'// the build contains, read from out\\{BN}\\gen\\chrome\\branded_strings_<locale>.pak (tools/grit/pak_util.py extract -t). Texts have the menu',
      '// accelerator "&" removed; non-ASCII is written as \\uXXXX so the file is plain ASCII and loads the same from file:// whatever its encoding.',
      '// messages per case, in order: ' + ', '.join(IDS) + '.',
      f'// "shipped": the locale has its own pak in out\\{BN}\\locales (the UI language list of this build); the others exist only as translations.',
      'window.FONTS_DATA = {', ' ids: [' + ', '.join(esc(i) for i in IDS) + '],', ' cases: [']
for c in cases:
    parts = ['loc: ' + esc(c['loc']), 'script: ' + esc(c['script']), 'shipped: ' + ('true' if c['shipped'] else 'false')]
    if 'allowed' in c:
        parts.append('allowed: [' + ', '.join(esc(a) for a in c['allowed']) + ']')
    if 'locales' in c:
        parts.append('locales: [' + ', '.join(esc(a) for a in c['locales']) + ']')
    parts.append('texts: [\n    ' + ',\n    '.join(esc(t) for t in c['texts']) + ']')
    js.append('  {' + ', '.join(parts) + '},')
js += [' ],', '};', '']
os.makedirs(f'{KIT}/data', exist_ok=True)
out = f'{KIT}/data/fonts-strings.js'
open(out, 'w', encoding='ascii', newline='\n').write('\n'.join(js))
with open(args.sources, 'w', encoding='utf-8', newline='\n') as f:
    f.write('locale\tpak\tsha256\tshipped_in_locales_dir\tstrings_byte_for_byte_in_shipped_pak\n')
    for loc, gen, h, sh, ip in srcs:
        f.write(f'{loc}\t{gen}\t{h}\t{sh}\t{ip}\n')
print('cases', len(cases), 'latin locales', len(latin_locs), 'bytes', os.path.getsize(out))
print('wrote', out, 'and', args.sources)
print('shipped locales checked byte for byte:', sum(1 for s in srcs if s[4]), 'of', sum(1 for s in srcs if s[3]))
print('problems:', len(problems))
for p in problems:
    print('  ', p)
sys.exit(1 if problems else 0)
