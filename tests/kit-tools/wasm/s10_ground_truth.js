// Runs the ORIGINAL s10/jit/wasm.js unchanged in Node (V8 on x64) and prints its module bytes and its answers as JSON.
const fs = require('fs'), vm = require('vm'), path = require('path');
// the repository root is four levels up from this file (venetium/tests/kit-tools/wasm)
const src = fs.readFileSync(path.resolve(__dirname, '..', '..', '..', '..', 's10', 'jit', 'wasm.js'), 'utf8');
const printed = [];
const ctx = { print: s => printed.push(s), WebAssembly, BigInt, Uint8Array };
vm.runInNewContext(src, ctx);
console.log(JSON.stringify({ bytes: Array.from(ctx.bytes), printed, node: process.version, v8: process.versions.v8, arch: process.arch }));
