# kit-tools/storage

Server-side tools for the kit page `storage`. Nothing here goes to the device and the page never loads it.

## storage-expect.js

`kit/storage.html` stores big deterministic values in localStorage and IndexedDB and compares what it reads back with a CRC-32 that is
NOT measured in the browser: it is computed here, by Node's `zlib.crc32`, over bytes this tool makes with BigInt arithmetic (the page
has its own generator written with `Math.imul`, and checks it against the host value first, so a read-back failure alone points at the
storage and a failure of both at the JS engine).

- `node storage-expect.js` prints the HOST VALUES block; `--check` exits 0 only if the block in `storage.html` is exactly that;
  `--write` rewrites the block. After `--write`, recompute the sha256 of `storage.html` in `kit/manifest/storage.tsv`.

## mutants.js

`node mutants.js <scratch-dir> <mutation> [--run]` copies the page, `kit/lib` and the runner to a scratch folder, applies ONE mutation
(a corrupting localStorage, a corrupting IndexedDB, databases that vanish on close, a broken blob: URL, a wrong host value, a wrong
`Math.imul`, a denied localStorage) and runs the runner against the copy. Each mutation prints what the page must then do.
Node is `F:\cr\src\third_party\node\win\node.exe`.

## What the page cannot see

The Blob download: the runner records it (`summary.pages.storage.downloads`: one entry, `venetium-kit-download.txt`, state `completed`;
the file in `downloads\` is 45 bytes and starts `Venetium kit download venetium-`). A gate that wants it asserted must check that
in the runner's output; the page only clicks the link. Durability across a restart is not proven either: the two "earlier load" rows show
what a previous load left behind (null in a fresh profile).
