// Venetium test kit — js-jit.html's embedded host answers (BATCH-X86-2 §1). GENERATED from s10\jit\expected.txt and s10\jit\node-x64.txt
// (the two files are not read at run time: every file:// document is its own opaque origin, so data lives in a .js file).
// JIT_EXPECTED: the host's independent answers for core.js, as printed by `python s10\jit\expected.py` (x64 CPython, exact integers;
//   JS semantics emulated explicitly). expected.txt also holds wasm_i32 / wasm_i64: those belong to wasm.html, not here.
// JIT_NODE_X64: the answers of host Node.js (V8 on x64) for the same cases, node-x64.txt. run_jit.py uses it only for sin_crosstier (libm-
//   dependent, not recomputable on the host); this page embeds all of them, and the build step verified they agree with JIT_EXPECTED.
window.JIT_EXPECTED = {
  "overflow": "-708020816",
  "divmod": "-2268820",
  "bits": "2093175226",
  "bitconst": "-2147483648,2147483648,4294967295,-2147483648,-1073741824,1073741824,-2147483648,1",
  "imul": "-2080158760",
  "strhash": "1177007544",
  "arrobj": "59955000,41541750,-665667000",
  "typed": "-440089804,4291470796,94860",
  "bigint": "9837489108690961646,-8609254965018589970,1200865369713252,172710",
  "regex": "134,764798328",
  "trycatch": "663,3152103",
  "doubles": "108.075028029,0.333333333,1.414213562,0.300000000,3,142857142857142860000",
};
window.JIT_NODE_X64 = {
  "overflow": "-708020816",
  "divmod": "-2268820",
  "bits": "2093175226",
  "bitconst": "-2147483648,2147483648,4294967295,-2147483648,-1073741824,1073741824,-2147483648,1",
  "imul": "-2080158760",
  "strhash": "1177007544",
  "arrobj": "59955000,41541750,-665667000",
  "typed": "-440089804,4291470796,94860",
  "bigint": "9837489108690961646,-8609254965018589970,1200865369713252,172710",
  "regex": "134,764798328",
  "trycatch": "663,3152103",
  "doubles": "108.075028029,0.333333333,1.414213562,0.300000000,3,142857142857142860000",
  "sin_crosstier": "0.393269602538",
};
