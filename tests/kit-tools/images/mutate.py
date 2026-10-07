"""Scratch mutations of the DATA of a COPY of the kit (the first mutation set used while developing the page; mutations.js is the
newer, page-level set). Usage: python mutate.py <kit folder>   - the folder must be a scratch copy: it is refused if it is the
record's own kit (the sibling ../../kit of this script). Changes: a wrong exact checksum (gif-static), a wrong x86 reference
(jpeg-gray-islow), a wrong size (bmp-24), the damage of jpeg-badhuff-kraft 'repaired', png-trunc-idat replaced by the whole PNG.
Each of these must turn the page's verdict into FAIL; run the copy with its own runner (copy tests/runner beside it).
"""
import re, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
kit = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else None
real = os.path.normpath(os.path.join(HERE, '..', '..', 'kit'))
if not kit or os.path.normcase(os.path.realpath(kit)) == os.path.normcase(os.path.realpath(real)): sys.exit('refusing: give the path of a scratch COPY of the kit')
pv, pb = os.path.join(kit, 'data/images-valid.js'), os.path.join(kit, 'data/images-broken.js')
s = open(pv, encoding='utf8').read()
def rep(s, a, b):
    assert a in s, a[:60]
    return s.replace(a, b)
s = rep(s, '"id":"gif-static","fmt":"gif","type":"image/gif","w":64,"h":48,"exact":"67d7dea1"', '"id":"gif-static","fmt":"gif","type":"image/gif","w":64,"h":48,"exact":"67d7dea2"')
s = rep(s, '"x86":"050c1b05"', '"x86":"050c1b06"')
s = rep(s, '"id":"bmp-24","fmt":"bmp","type":"image/bmp","w":64,', '"id":"bmp-24","fmt":"bmp","type":"image/bmp","w":65,')
open(pv, 'w', encoding='utf8', newline='\n').write(s)
v = open(pv, encoding='utf8').read()
base = re.search(r'"id":"jpeg-baseline-420".*?"b64":"([^"]+)"', v).group(1)
b = open(pb, encoding='utf8').read()
m = re.search(r'("id":"jpeg-badhuff-kraft".*?"b64":")([^"]+)(")', b)
b = b[:m.start(2)] + base + b[m.end(2):]
png = re.search(r'"id":"png-rgb8","fmt":"png".*?"b64":"([^"]+)"', v).group(1)
m = re.search(r'("id":"png-trunc-idat".*?"b64":")([^"]+)(")', b)
b = b[:m.start(2)] + png + b[m.end(2):]
open(pb, 'w', encoding='utf8', newline='\n').write(b)
print('mutated', kit)
