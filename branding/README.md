# Venetium icon

His files (2026-09-26) — BATCH-VENETIUM-1 §2 uses exactly these names:

- `Venetium.png` — the master, 1254 x 1254, transparent. Every PNG asset is rendered from it.
- `Venetium.ico` — hand-made, 16 / 32 / 48 / 64 / 128 / 256. It becomes the exe icon; its sizes win.
  The batch adds any size Chromium's original icon has that this one lacks, plus 24 x 24
  (what Windows asks for at the Surface 2's usual 150% scaling).

After the batch runs, look at `s4\icon-contact-sheet.png`. If the generated 24 x 24 looks soft,
draw one by hand, save it here as `Venetium-24.png`, and re-run §2.

The batch also writes `ICON-SPEC.md` here: every image Chromium uses, its size, and anything that
cannot be made from a square icon (a text wordmark, a tile background colour) — those need you.
