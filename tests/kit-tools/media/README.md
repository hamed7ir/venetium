# kit-tools/media

Server-side tools for the kit page `media`. Nothing here goes to the device and the page never loads it.
Node is `F:\cr\src\third_party\node\win\node.exe`.

## media-build.js

Builds and checks `kit/data/media-*.js` (the clips, base64, from Chromium's own `media/test/data` at the build's tag; every upstream
file's sha256 is pinned in the script), `kit/data/media-index.js` (the catalog) and `kit/manifest/media.tsv`.
`--check` is offline: every data file decodes to its pinned original, `data/media-index.js` is byte for byte what this tool generates (so
an edit of any catalog value, w, h, dur, mime, codecs or note, or a stale header line, is a BAD row), every manifest row matches its file.
`--manifest` rewrites only `manifest/media.tsv` from the files as they are on disk (run it last, after the final edit of `media.html`).
`--fetch`, `<dir>` and `--unpack <dir>` regenerate / unpack the data (see the header of the script).

To regenerate `data/media-index.js` without network access: `node media-build.js --unpack <scratch>`, then `node media-build.js <scratch>`
(rewrites the 13 clip files, the catalog and the manifest; the clip files come out byte for byte the same), then `node media-build.js --check`.

## mutants.js

`node mutants.js <scratch-dir> <mutation> [--run]` copies the page, `kit/lib`, `kit/data` and the runner to a scratch folder, applies ONE
mutation and runs the runner against the copy (the runner's own default autoplay policy applies: like the device launchers it passes no
`--autoplay-policy` flag). Each mutation prints what the page must then do:

- `audio-element`, `unmuted`: the old defect, put back (audio-only clips in an `<audio>` element / unmuted playback): the default
  autoplay policy refuses it without a gesture, so the page must FAIL (only a run under the default policy can see this)
- `size-wrong`, `data-corrupt`, `blank-picture`, `silent-audio`, `codec-missing`: a wrong catalog entry, a changed clip byte, a video
  frame that never reaches the canvas, a decodeAudioData that returns silence, a canPlayType that says no (the kit-assumption row fails and says so)

Known limit (not a check): a decode that is wrong but structured (a picture with the wrong colours, noise instead of audio) passes
"non-blank picture" and "non-silent audio". Closing that needs reference values (a coarse luma grid per clip, an RMS envelope per
0.25 s) made by an independent decoder and a tolerance that a real ARM32 run has confirmed; neither exists yet.
