# kit-tools/wasm - how `data/wasm-data.js` and the wasm test files are made

Server-side tools for `venetium/tests/kit/wasm.html`. Nothing here ships with the kit; the kit only carries their output.
All paths are relative to this folder, so the commands work from any current directory. No `__pycache__` is written
(`sys.dont_write_bytecode`; importing `s10/jit/expected.py` would otherwise leave one in the record).

## Regenerate (on the build server)

```
set PYTHONDONTWRITEBYTECODE=1
python gen_wasm_data.py && F:\cr\src\third_party\node\win\node.exe node_check.js && python gen_wasm_data.py && python manifest.py
```

`KIT_NODE` overrides the Node path (v24.x is asserted; the data records the version it ran on: v24.12.0, V8 13.6.233.17-node.37, x64).
Python 3.13, standard library only.

| file | what it does |
|---|---|
| `gen_wasm_data.py` | assembles the `ext` module (124 i32/i64 operator functions incl. 5 variable and 45 constant-amount i64 shifts, 52 memory functions, `grow`/`size`, `spin`, `memloop`) and computes every answer in python from exact integers: operator digests, `spin`/`memloop`, and the memory script (a python `MemModel`: a little-endian bytearray with the exact effective address `u32(index) + offset`, no wrap). Writes `kit/data/wasm-data.js`, `kit/media/wasm-s10.wasm`, `kit/media/wasm-ext.wasm`. The s10 module and its answers come from the ORIGINAL `s10/jit/wasm.js` run unchanged (via `s10_ground_truth.js`) and `s10/jit/expected.py`. |
| `s10_ground_truth.js` | runs `s10/jit/wasm.js` in Node and prints its module bytes and answers (JSON). |
| `node_check.js` | the cross-check: loads the kit's OWN `lib/wasm-cases.js` + `data/wasm-data.js` in Node (V8 on x64, an independent engine) and fails (exit 1) unless every digest, `spin`, `memloop`, the 27 + 5 memory probes and every step of the memory script equals the python answer, AND the replay has exactly the shape the data records (`ext.mem.expect`, `ext.expect`: the same `memShape` / `opsShape` the page runs). Writes `node-answers.json` (an intermediate: pass 2 of the generator merges it into the data as `host_node_x64`). |
| `manifest.py` | writes `kit/manifest/wasm.tsv` (header `path<TAB>sha256<TAB>source`; wasm.html, lib/wasm-*.js, data/wasm-data.js, media/wasm-*.wasm; hashes of the files as they are now). Run it LAST. `--check` exits 1 if the manifest is stale. |
| `mutate_shape.js` | mutation gate for the shape rows: `node mutate_shape.js <scratch-root>` makes copies of kit + runner (+ a copy of `node_check.js` that checks the copy's own data) in which the data lost phase D, phase E, both, one step, the last step, a phase name, the only call of one function (counts unchanged), an operator group, one operator, or in which the host record (`expect`) is changed or missing, plus a `control-unchanged` copy that must PASS. Every other copy must FAIL in the runner (default: exactly the shape row) and exit 1 in `node_check.js`. |
| `mutate.js` | mutation gate: `node mutate.js <scratch-root>` makes copies of kit + runner with one byte pattern of the ext module changed (i64.load16_s as load16_u, load8_s as load8_u, store16 as store8, shr_s const 32 as shr_u, rotl const 32 as 33, variable rotl as rotr, offset 0xfffffffc dropped, constant address + overflowing offset, f64/f32 bit accesses shifted by a byte, f64 copy reversed, ...). Every mutant must FAIL (the runner, config `default` and `turbofan`), naming the step. |

The generated rows come with a reproducibility guarantee: pass 3 is byte-identical to pass 2.

## The memory script

The script has 1,423 steps in 5 phases: A 208, B 118, C 86, D 966, E 45. The generator writes this list next to the script
(`ext.mem.expect = {phases: [[name, steps], ...], steps: 1423, fns: 52}`) and the operator groups next to the operator list
(`ext.expect = {groups: [[name, operators], ...], ops: 124}`), after asserting that every phase and group is one contiguous run, that the counts add
up and that the script calls every one of the 52 memory functions. `wasm.html` counts what it actually executed (the phase of every step
it ran, the function of every step, the group of every digest it produced) and FAILS one real row per pass unless the executed
phases, in order, with their step counts, the total, and the called functions equal that record, so a data file that lost the float phase
(D) or the offset / constant-address phase (E) cannot pass by omission. A consistently rewritten data file (script and record changed
together) is not a runtime matter: the manifest sha256 and the byte-identical regeneration guard the file.

`ext.mem.script` is `[phase, function, [args], want]` steps replayed on ONE fresh instance (want: a decimal result, `ok`, or `T` for a
`WebAssembly.RuntimeError`; `@bytes` steps compare `mem.buffer` read through the JS API with the model's bytes). Phases:
A loads (every load width at addresses 0..16 over a 3-word pattern), B stores (every store width at unaligned addresses, read back as
two doublewords), C the page end (every width at 65535/65534/65532/65528 and one byte past, negative and 2^31 addresses, a trapped
store writes nothing), D floats (f32/f64 load+store copies and value-returning `load+reinterpret` / `reinterpret+store`; 12 f32 and
12 f64 bit patterns including signalling NaN payloads, -0, denormals; four and six source alignments; alignment hint 0 and natural;
an overlapping copy), E static offsets, constant addresses and growth (offsets 65532 / 65535 / 196604 / 0xfffffffc / 0xffffffff /
0xfffffff8: the 32-bit sum must not wrap; constant addresses folded by the compilers; `grow(2)`, accesses after growth, `grow` past
the 3-page maximum). No float arithmetic anywhere: the wasm spec requires load, store and reinterpret to preserve every bit pattern,
so there is no NaN canonicalisation to depend on.

## Device note

An unaligned f32/f64 access that kills the tab on ARM32 (alignment fault), or any FAIL in phase D (float bit patterns), is a real
finding, not a harness bug.
