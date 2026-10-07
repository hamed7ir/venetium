// Mutation gate for the SHAPE rows of wasm.html (BATCH-DEVICE-1 follow-up): the page must FAIL when the data it replays lost a phase, a step,
// a function or an operator group, or when the host's record of the shape (data ext.mem.expect, ext.expect) is changed or missing.
// Complements mutate.js (byte mutants of the wasm module). The real kit is never touched.
// 'control-unchanged' is the same pipeline with no mutation: it must PASS (runner) and exit 0 (node_check).
//   node mutate_shape.js <scratch-root>     creates <scratch-root>/<mutant>/{kit,runner,kit-tools/wasm/node_check.js} for every mutant + list.txt
//   then, per mutant:   node <scratch-root>/<mutant>/runner/run.js --config default --pages wasm --out <dir>        (must FAIL)
//                       node <scratch-root>/<mutant>/kit-tools/wasm/node_check.js                                  (host side: must exit 1)
// The mutant copies carry their own copy of node_check.js, whose '../../kit' is the mutant's kit, so the host gate is exercised on the same data.
const fs = require('fs'), path = require('path'), vm = require('vm');
const TESTS = path.resolve(__dirname, '..', '..');                          // venetium/tests
const root = path.resolve(process.argv[2] || '');
if (!process.argv[2]) { console.error('usage: node mutate_shape.js <scratch-root>'); process.exit(2); }
const dataFile = path.join(TESTS, 'kit', 'data', 'wasm-data.js');
const dataTxt = fs.readFileSync(dataFile, 'utf8');
const load = () => { const ctx = { window: {} }; vm.runInNewContext(dataTxt, ctx); return ctx.window.KIT_WASM; };   // a fresh copy per mutant
const starts = letter => st => st[0].startsWith(letter + ' ');               // phase names begin with 'A loads', 'B stores', 'C the page end', 'D float ...', 'E offsets ...'
const dropPhases = (...letters) => D => { const n = D.ext.mem.script.length; D.ext.mem.script = D.ext.mem.script.filter(st => !letters.some(l => starts(l)(st))); return n - D.ext.mem.script.length + ' steps removed'; };
// name: [function(D) that mutates the data object in place and returns a note, what it models]
const MUTANTS = {
  'control-unchanged':       [D => 'nothing changed (the data is only re-serialised)', 'CONTROL: must PASS in the runner and node_check must exit 0, or the mutants below prove nothing'],
  'drop-D-float':            [dropPhases('D'), 'phase D (float bit patterns, NaN payloads, unaligned, hint 0) lost from the script'],
  'drop-E-offsets':          [dropPhases('E'), 'phase E (static offsets, constant addresses, growth) lost from the script'],
  'drop-D-and-E':            [dropPhases('D', 'E'), 'the script cut to phases A-C (the reviewer\'s m4: it gave PASS with 52 checks before the shape rows)'],
  'drop-one-bytes-step-D':   [D => { const i = D.ext.mem.script.findIndex(st => starts('D')(st) && st[1] === '@bytes'); D.ext.mem.script.splice(i, 1); return 'step ' + i + ' removed'; }, 'ONE read-only @bytes step of phase D lost: 965 of 966 steps, every remaining step still right'],
  'drop-last-step-E':        [D => { D.ext.mem.script.pop(); return 'last step removed'; }, 'the last step of phase E lost: 44 of 45'],
  'rename-phase-B':          [D => { let n = 0; for (const st of D.ext.mem.script) if (starts('B')(st)) { st[0] = 'B2 ' + st[0].slice(2); n++; } return n + ' steps renamed'; }, 'phase B carries another name in the script than in the host record'],
  'swap-function-same-counts': [D => {
      const i = D.ext.mem.script.findIndex(st => st[1] === 'ld16_u_c65535'), st = D.ext.mem.script[i];
      if (st[3] !== 'T' || D.ext.mem.script.find(x => x[1] === 'ld32_c65533')[3] !== 'T' || st[2].length) throw new Error('premise: both steps trap and take no argument');
      st[1] = 'ld32_c65533'; return 'step ' + i + ' now calls ld32_c65533 (same want T): every count equal, ld16_u_c65535 never called';
    }, 'a memory function no longer called by the script, all counts unchanged (needs no natives: tier rows would also notice in natives configurations)'],
  'expect-E-count':          [D => { D.ext.mem.expect.phases[4][1] += 1; return 'host record says E has 46 steps'; }, 'the host record of phase E says 46 steps, the script has 45'],
  'expect-steps':            [D => { D.ext.mem.expect.steps -= 1; return 'host record steps 1422'; }, 'the host record of the total says 1422, the script has 1423'],
  'expect-fns':              [D => { D.ext.mem.expect.fns += 1; return 'host record fns 53'; }, 'the host record says 53 memory functions, the table has 52'],
  'expect-removed':          [D => { delete D.ext.mem.expect; return 'ext.mem.expect deleted'; }, 'the host record of the phases is absent from the data'],
  'ops-drop-group-shift-const': [D => { const n = D.ext.ops.length; D.ext.ops = D.ext.ops.filter(o => o.group !== 'i64-shift-const'); return n - D.ext.ops.length + ' operators removed'; }, 'the i64-shift-const group lost from the operator list (the host digests stay in the data)'],
  'ops-drop-one-op':         [D => { const i = D.ext.ops.findIndex(o => o.group === 'i64-arith') + 4; const o = D.ext.ops.splice(i, 1)[0]; return 'operator ' + o.name + ' removed'; }, 'ONE operator lost from the middle of i64-arith: 14 of 15, every remaining digest still right'],
  'ops-expect-count':        [D => { D.ext.expect.groups[1][1] += 1; return 'host record says i64-cmp has 11'; }, 'the host record of i64-cmp says 11 operators, the list has 10'],
  'ops-expect-removed':      [D => { delete D.ext.expect; return 'ext.expect deleted'; }, 'the host record of the operator groups is absent from the data'],
  'ops-extra-host-answer':   [D => { D.ext.host_python.bogus_op = '00000000/1/0'; return 'a 125th host digest added'; }, 'the host holds an answer that no operator produces'],
};
fs.mkdirSync(root, { recursive: true });
const list = [];
for (const [name, [mutate, what]] of Object.entries(MUTANTS)) {
  const D = load(), note = mutate(D);
  const out = path.join(root, name);
  fs.rmSync(out, { recursive: true, force: true });
  fs.mkdirSync(path.join(out, 'kit-tools', 'wasm'), { recursive: true });
  fs.cpSync(path.join(TESTS, 'kit'), path.join(out, 'kit'), { recursive: true });
  fs.cpSync(path.join(TESTS, 'runner'), path.join(out, 'runner'), { recursive: true });
  fs.copyFileSync(path.join(__dirname, 'node_check.js'), path.join(out, 'kit-tools', 'wasm', 'node_check.js'));
  fs.writeFileSync(path.join(out, 'kit', 'data', 'wasm-data.js'), '// MUTANT ' + name + ': ' + what + '\nwindow.KIT_WASM = ' + JSON.stringify(D) + ';\n');
  list.push(name);
  console.log(name.padEnd(28), note, ' -> ', out);
}
fs.writeFileSync(path.join(root, 'list.txt'), list.join('\n') + '\n');
