// Computes the HOST VALUES block of kit/storage.html (server only; the kit and the device never load this). The page stores big
// deterministic values (localStorage, IndexedDB) and compares what it reads back with the CRC-32 below, which comes from here,
// from Node's zlib.crc32 over bytes made with BigInt arithmetic: nothing in it is measured by the browser under test.
//   node storage-expect.js            prints the block
//   node storage-expect.js --check    exit 0 if the block in kit/storage.html is exactly this one, else 1
//   node storage-expect.js --write    rewrites the block in kit/storage.html
// node = F:\cr\src\third_party\node\win\node.exe. After --write, recompute the sha256 of kit/storage.html in kit/manifest/storage.tsv.
// The byte rule (the page has its own copy, written with Math.imul): 256-byte blocks; in a block with an even index byte i is
// 33 + (i * 7) % 90 (a repeating, compressible pattern); in an odd block byte i is the top byte of the next state of the LCG
// x = (x * 1664525 + 1013904223) mod 2^32, started from the seed and advanced once per byte of the odd blocks only.
'use strict';
const fs = require('fs'), path = require('path'), zlib = require('zlib');
const PAGE = path.resolve(__dirname, '..', '..', 'kit', 'storage.html');
function payload(seed, len) {
  const b = Buffer.alloc(len); let x = BigInt(seed);
  for (let i = 0; i < len; i++) {
    if (Math.floor(i / 256) % 2 === 0) b[i] = 33 + ((i * 7) % 90);
    else { x = (x * 1664525n + 1013904223n) & 0xffffffffn; b[i] = Number(x >> 24n); }
  }
  return b;
}
const spec = { ls: { seed: 20261004, len: 98304 }, idb: [{ seed: 7, len: 40000 }, { seed: 11, len: 300000 }] };
const crc = s => zlib.crc32(payload(s.seed, s.len)).toString(16).padStart(8, '0');
const one = s => "{ seed: " + s.seed + ", len: " + s.len + ", crc: '" + crc(s) + "' }";
const block = "const HOST = {\n  ls:  " + one(spec.ls) + ",\n  idb: [" + spec.idb.map(one).join(', ') + "],\n};\n";
const RE = /const HOST = \{[\s\S]*?\n\};\n/;
const html = fs.readFileSync(PAGE, 'utf8');
const m = RE.exec(html);
if (!m) { console.error('no HOST block in ' + PAGE); process.exit(2); }
if (process.argv.includes('--check')) {
  const same = m[0] === block;
  console.log((same ? 'OK: ' : 'DIFFERENT: ') + PAGE + (same ? ' holds exactly the host values' : ' does not hold the host values') + '\n' + block);
  process.exit(same ? 0 : 1);
}
if (process.argv.includes('--write')) { fs.writeFileSync(PAGE, html.replace(RE, () => block)); console.log('written to ' + PAGE); }
console.log(block);
