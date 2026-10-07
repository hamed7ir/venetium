# kit-tools/canvas-webgl

Server-side tools for the kit page `canvas-webgl`. Nothing here goes to the device and the page never loads it.

## canvas-webgl-gen.js

Makes `kit/data/canvas-webgl-golden.js`: the golden CRC32 of the exact 2D scene's pixels and of its operation list.
It runs `kit/lib/canvas-webgl-model.js` (the page's own scene generator and independent software rasteriser, plain integer JS, no
canvas) under Node and writes what that model predicts.

Run from the repository root (`D:\repo\supermium-rt`):

- Regenerate: `F:\cr\src\third_party\node\win\node.exe venetium\tests\kit-tools\canvas-webgl\canvas-webgl-gen.js`
  (prints the values as JSON and rewrites the golden file)
- Check only: `F:\cr\src\third_party\node\win\node.exe venetium\tests\kit-tools\canvas-webgl\canvas-webgl-gen.js --check`
  (writes nothing; prints `OK: ... byte for byte ...` and exits 0 when the golden file on disk is exactly what the model generates,
  prints `DIFFERENT` and exits 1 otherwise)

After a regeneration, recompute the sha256 of `kit/data/canvas-webgl-golden.js` and update `kit/manifest/canvas-webgl.tsv`.

## mutants.js

`node mutants.js <scratch-dir> <mutation> [--run]` copies the whole kit and the runner to a scratch folder, applies ONE mutation to the
copy and runs the runner against it (`--run`), or prints the command; each mutation prints what the page must then do: a scene that
throws, a throwing WebGL suite setup (the other WebGL version still runs), a context that never restores (fails after 15 s, no hang),
WebGL required by `?webgl=required` or by `lib/build.js` (`KIT_BUILD.webgl = 'required'`) while the browser has no WebGL
(`--disable-3d-apis`), no WebGL and not required (PASS, but the coverage row and a red banner say WebGL was not tested), a shifted 2D
canvas, a wrong golden file, a WebGL clear colour with red and blue swapped. Node is `F:\cr\src\third_party\node\win\node.exe`.

Follow-up round (BATCH-DEVICE-1 follow-up, group webrtc-canvas): the WebGL requirement is per version. `?webgl=required` or
`KIT_BUILD.webgl = 'required'` (lib/build.js) requires WebGL 1 only (WebGL 2 is reported); `?webgl=webgl2` or `KIT_BUILD.webgl = 'webgl2'` requires
both; absent requires nothing; any other value is a failed check "the WebGL requirement setting is recognised" (the stricter of URL and build.js
wins). Result fields: `webglRequired` {1,2}, `webglSetting`, `webglTested` {1,2}, `webglBanner` ('none' | 'grey' | 'red'). Banner: red when WebGL
1 is missing (no WebGL at all) or a required version is missing; grey information when WebGL 2 is missing while WebGL 1 works and WebGL 2 is not
required (normal on a GPU that only reaches Direct3D feature level 9_x); none otherwise. The shader row now asks the linked programs for their
attached shaders, active attribute and active uniforms (name:type). `mutants.js` writes to `F:\cr\device-1\runs\mut2-canvas-webgl-<mutation>` and
has new entries for each of these: `webgl2-required-url`, `webgl-required-present`, `webgl2-required-present`, `gl2-missing-notrequired`,
`gl2-missing-required`, `gl2-missing-webgl2-url`, `gl2-missing-webgl2-build`, `gl1-missing-required`, `gl1-missing-notrequired`, `webgl-bad-setting`,
`shader-uniform-renamed` (the older `webgl-required-*` entries now expect exactly one bad check, "WebGL 1 context").
