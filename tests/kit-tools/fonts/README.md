# kit-tools/fonts

Server-side tools for the kit page `fonts` (`kit/fonts.html`, data `kit/data/fonts-strings.js`). Nothing here goes to the device and the
page never loads it. Needs Python 3 (and Node for `make-neg.js`); the inputs are build outputs, so they stay outside the repository and their
sha256 are the record.

## What the data is

`kit/data/fonts-strings.js` holds the four product-name messages (`IDS_ABOUT`, `IDS_APPMENU_TOOLTIP`, `IDS_BROWSER_HUNGBROWSER_MESSAGE`,
`IDS_DEFAULT_BROWSER_BUBBLE_DIALOG_TITLE`) of every translation the build contains (84 paks, 82 real locales), read from the build's own GRIT
output `out\x86-rt21\gen\chrome\branded_strings_<locale>.pak`. The expected strings therefore come from the build, never from the browser
under test. For the 55 locales that also ship `out\x86-rt21\locales\<locale>.pak` the generator asserts the four strings are inside that file
byte for byte.

## Regenerate (two steps; the second rewrites `kit/data/fonts-strings.js` and `fonts-sources.tsv`)

    python fonts-dump.py --out <paks-dir>                  (about 25 s; extracts every pak with F:\cr\src\tools\grit\pak_util.py extract -t)
    python fonts-gen.py  --paks <paks-dir>

Options: `--build <dir>` (default `F:/cr/src/out/x86-rt21`; use another build folder, e.g. the ARM one, to make that build's data),
`--src <dir>` (dump only: the tree that holds `tools/grit/pak_util.py`, sha256 of the script used
`4323d43825fe59fc950e7f19922b08e487643c662b0bf7c921b36be877e0d3ed`), `--kit <dir>` and `--sources <file>` (generator only: where the two outputs
go; defaults `..\..\kit` and the file next to the script, so point both at a scratch folder to compare without touching the kit).
The generator exits 1 and lists the problem if a string lacks the product name or a shipped locale pak does not contain the strings.

Reproduction check (BATCH-DEVICE-1): a fresh dump of the 84 paks of `x86-rt21` and a run of `fonts-gen.py` reproduce `fonts-sources.tsv`
byte for byte and `kit/data/fonts-strings.js` byte for byte (40 cases, 43 Latin locales, 55 of 55 shipped locales byte for byte, 0 problems).
After a regeneration recompute the sha256 of the data file and update `kit/manifest/fonts.tsv`. The source text of the data row names these
tools and holds the sha256 of `fonts-gen.py`, `fonts-dump.py`, `fonts-sources.tsv` and `pak_util.py`; it names no build path (the build
input is described in words, its paks are listed with their sha256 in `fonts-sources.tsv`), so refresh those hashes in the row too if a tool changes.

## Files

- `fonts-dump.py` - step 1, extracts the pak messages.
- `fonts-gen.py` - step 2, writes the data file and the source table.
- `fonts-sources.tsv` - locale, pak path, sha256 of every source pak, whether the locale also ships in `locales\`, and whether the strings are byte
  for byte in that shipped pak. Output of `fonts-gen.py` for the `x86-rt21` build.
- `fonts-groundtruth.txt` - the platform font that drew each script on the x86 server (DevTools `CSS.getPlatformFontsForNode`, glyph count per
  font); the page's "font that drew each script" row is an inference by identical pixels, this is the check of that inference.
- `make-neg.js` - negative test data: `node make-neg.js <out-file>` writes a copy of `fonts-strings.js` in which the Khmer letters are replaced by
  Tangut letters (no installed font has them) and a third of the Myanmar letters by one Tangut letter. Run the page on it from a scratch copy
  of the kit with that file as the scratch `data/fonts-strings.js`: the `km` row and the `my` row must FAIL (everything else PASS). Never write it
  into the real kit.

## What the page can and cannot tell

A failed language row on the device is a font finding (the row lists the code points and the fonts that were not found); the page measures glyph
presence, not shaping. The static `fonts-groundtruth.txt` and the x86 PASS say nothing about the Surface 2 fonts.
