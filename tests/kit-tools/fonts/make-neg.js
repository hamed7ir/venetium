// Negative test data for fonts.html (server side, not shipped): takes kit/data/fonts-strings.js and replaces
//   * every Khmer letter of the 'km' case by a Tangut letter (script of the case changed to Tangut): no installed font has Tangut, so every
//     km letter must read as the missing-glyph box and the km row must FAIL;
//   * every 3rd Myanmar letter of the 'my' case by one Tangut letter (a partial miss, 'my' allowed scripts Myanmar + Tangut): the my row must FAIL.
// Usage:  node make-neg.js <out-file> [<kit dir>]        (kit dir defaults to ..\..\kit relative to this file)
// To run the page on it, copy the kit + runner to a scratch folder and overwrite the SCRATCH copy of data/fonts-strings.js with <out-file>
// (same window.FONTS_DATA global); never write the output into the real kit.
'use strict';
const fs = require('fs'), path = require('path'), vm = require('vm');
const OUT = process.argv[2];
if (!OUT) { console.error('usage: node make-neg.js <out-file> [<kit dir>]'); process.exit(2); }
const KIT = path.resolve(process.argv[3] || path.join(__dirname, '..', '..', 'kit'));
const ctx = { window: {} };
vm.runInNewContext(fs.readFileSync(path.join(KIT, 'data', 'fonts-strings.js'), 'utf8'), ctx);
const D = ctx.window.FONTS_DATA;
let n = 0;
const tang = () => String.fromCodePoint(0x17000 + (n++ % 40));
for (const c of D.cases) {
  if (c.loc === 'km') { c.script = 'Tangut'; c.texts = c.texts.map(t => [...t].map(ch => /[\u1780-\u17ff]/.test(ch) ? tang() : ch).join('')); }
  if (c.loc === 'my') { c.allowed = ['Myanmar', 'Tangut']; let k = 0; c.texts = c.texts.map(t => [...t].map(ch => (/[\u1000-\u109f]/.test(ch) && (k++ % 3 === 0)) ? String.fromCodePoint(0x17010) : ch).join('')); }
}
const esc = s => s.replace(/[\u0080-\uffff]/g, m => '\\u' + m.charCodeAt(0).toString(16).padStart(4, '0'));
fs.writeFileSync(path.resolve(OUT), 'window.FONTS_DATA = ' + esc(JSON.stringify(D)) + ';\n');
console.log('wrote', path.resolve(OUT));
