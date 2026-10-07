// Mutation checks for kit\canvas-webgl.html (server only; not part of the kit that goes to the device).
//   node mutants.js <scratch-dir> <mutation> [--run]        mutations: see M below (node = F:\cr\src\third_party\node\win\node.exe)
// Copies the whole kit and the runner to <scratch-dir>\<mutation>\kit and \runner (never touching the real ones), applies ONE mutation
// to the copy, and prints (or, with --run, executes) the runner command against the copy. Every replacement must match exactly once,
// or the tool stops, so a refactor of the page cannot silently turn a mutation into a no-op. --run writes the run to
// F:\cr\device-1\runs\mut2-canvas-webgl-<mutation> (mut- was the first round).
// Each mutation says what the page must then do (EXPECT); a run that does not do that is a defect of the page.
'use strict';
const fs = require('fs'), path = require('path'), { spawnSync } = require('child_process');
const KIT = path.resolve(__dirname, '..', '..', 'kit'), RUNNER = path.resolve(__dirname, '..', '..', 'runner');
const H = 'canvas-webgl.html';
const SCENE = 'const ops = CW.scene();';
const CTXLOSS = 'ext.loseContext(); await lost;';
const MODEL = '<script src="lib/canvas-webgl-model.js"></script>';
// a GPU that gives WebGL 1 but no WebGL 2 (Direct3D feature level 9_x): getContext('webgl2') returns null, everything else is untouched
const NO_GL2 = '<script>{ const g = HTMLCanvasElement.prototype.getContext; HTMLCanvasElement.prototype.getContext = function (t, ...a) { return t === \'webgl2\' ? null : g.call(this, t, ...a); }; }</script>\n' + MODEL;
// the opposite oddity: WebGL 2 works, WebGL 1 does not
const NO_GL1 = '<script>{ const g = HTMLCanvasElement.prototype.getContext; HTMLCanvasElement.prototype.getContext = function (t, ...a) { return (t === \'webgl\' || t === \'experimental-webgl\') ? null : g.call(this, t, ...a); }; }</script>\n' + MODEL;
// the linker reports the uniform 'c' under another name, on both prototypes
const RENAME_UNIFORM = '<script>for (const C of [WebGLRenderingContext, WebGL2RenderingContext]) { const f = C.prototype.getActiveUniform; C.prototype.getActiveUniform = function (p, i) { const u = f.call(this, p, i); return u && u.name === \'c\' ? { name: \'cc\', type: u.type, size: u.size } : u; }; }</script>\n' + MODEL;
const M = {
  // a throw outside every sec() / gt(): the scene generator
  'scene-throws': { files: { [H]: [[SCENE, "throw new Error('injected scene boom');\n    " + SCENE]] },
    expect: 'FAIL after well under 5 s, one bad check "page: uncaught exception in section start | Error: injected scene boom"' },
  // a throw in glSuite(1)'s setup, outside every gt(): WebGL 2 must still run
  'glsetup-throws': { files: { [H]: [["cv.addEventListener('webglcontextcreationerror'", "if (ver === 1) throw new Error('injected suite setup boom');\n    cv.addEventListener('webglcontextcreationerror'"]] },
    expect: 'FAIL, a bad check "webgl1: ran without an exception | Error: injected suite setup boom", and the WebGL 2 checks are all there and ok' },
  // the context never comes back: the wait is 15 s, then a failed check (not a hang)
  'no-restore': { files: { [H]: [['ext.restoreContext(); await restored;', 'await restored;']] },
    expect: 'FAIL after about 15 s per WebGL version (~32 s in all): bad checks "WebGL 1/2: context loss: ran without an exception | Error: timeout after 15000 ms: event webglcontextrestored"' },
  // WebGL required by the URL, and the browser has no WebGL (--disable-3d-apis)
  'webgl-required-url': { files: {}, env: { KIT_QS_EXTRA: '&webgl=required' }, extra: '--disable-3d-apis',
    expect: 'FAIL, exactly ONE bad check: "WebGL 1 context" ("required" = WebGL 1 only); "WebGL 2 context" is an info row; result.webglRequired {1:true,2:false}, webglBanner "red" (NO WEBGL AT ALL)' },
  // WebGL 1 and 2 both required by the URL, and the browser has no WebGL
  'webgl2-required-url': { files: {}, env: { KIT_QS_EXTRA: '&webgl=webgl2' }, extra: '--disable-3d-apis',
    expect: 'FAIL, exactly TWO bad checks: "WebGL 1 context" and "WebGL 2 context"; result.webglRequired {1:true,2:true}, webglBanner "red"' },
  // required by the URL, WebGL 1 and 2 present (this server): both versions tested, no banner
  'webgl-required-present': { files: {}, env: { KIT_QS_EXTRA: '&webgl=required' },
    expect: 'PASS, 0 bad, webglTested {1:true,2:true}, webglRequired {1:true,2:false}, webglBanner "none"' },
  'webgl2-required-present': { files: {}, env: { KIT_QS_EXTRA: '&webgl=webgl2' },
    expect: 'PASS, 0 bad, webglTested {1:true,2:true}, webglRequired {1:true,2:true}, webglBanner "none"' },
  // WebGL 2 missing, WebGL 1 works (the D3D feature level 9_x case): the requirement decides
  'gl2-missing-notrequired': { files: { [H]: [[MODEL, NO_GL2]] },
    expect: 'PASS, 0 bad; info rows "WebGL 2 context: NOT AVAILABLE while WebGL 1 works" and "WebGL coverage" (WebGL 1 tested, WebGL 2 NOT AVAILABLE while WebGL 1 works); webglBanner "grey" (NOT red); webglTested {1:true,2:false}' },
  'gl2-missing-required': { files: { [H]: [[MODEL, NO_GL2]] }, env: { KIT_QS_EXTRA: '&webgl=required' },
    expect: 'PASS, 0 bad ("required" = WebGL 1 only, so a missing WebGL 2 does not fail); webglBanner "grey"; webglRequired {1:true,2:false}' },
  'gl2-missing-webgl2-url': { files: { [H]: [[MODEL, NO_GL2]] }, env: { KIT_QS_EXTRA: '&webgl=webgl2' },
    expect: 'FAIL, exactly ONE bad check: "WebGL 2 context" (the run requires WebGL 1 and 2); all WebGL 1 checks ok; webglBanner "red" (WEBGL 2 REQUIRED BUT NOT AVAILABLE)' },
  'gl2-missing-webgl2-build': { files: { [H]: [[MODEL, NO_GL2]], 'lib/build.js': [['window.KIT_BUILD = null;', "window.KIT_BUILD = { v8_current_cpu: 'arm', webgl: 'webgl2' };"]] },
    expect: 'FAIL, exactly ONE bad check: "WebGL 2 context" (Kit.build.webgl is "webgl2"); webglBanner "red"' },
  // WebGL 1 missing, WebGL 2 works
  'gl1-missing-required': { files: { [H]: [[MODEL, NO_GL1]] }, env: { KIT_QS_EXTRA: '&webgl=required' },
    expect: 'FAIL, exactly ONE bad check: "WebGL 1 context"; WebGL 2 tested; webglBanner "red" (WEBGL 1 NOT TESTED)' },
  'gl1-missing-notrequired': { files: { [H]: [[MODEL, NO_GL1]] },
    expect: 'PASS, 0 bad; webglBanner "red" (WebGL 1 is missing, so it is NOT the grey case); webglTested {1:false,2:true}' },
  // an unrecognised setting is a failed check, not a silent "no requirement"
  'webgl-bad-setting': { files: {}, env: { KIT_QS_EXTRA: '&webgl=requred' },
    expect: 'FAIL, exactly ONE bad check: "the WebGL requirement setting is recognised"; nothing else bad (the typo requires nothing)' },
  // the shader row asks the linked programs about their active uniforms: a renamed uniform must fail it (once per WebGL version)
  'shader-uniform-renamed': { files: { [H]: [[MODEL, RENAME_UNIFORM]] },
    expect: 'FAIL, exactly TWO bad checks: "WebGL 1: GLSL ES 1.00 ... compile and link; ..." and "WebGL 2: GLSL ES 3.00 ..." (got shows uniforms cc: instead of c:)' },
  // WebGL required by the packaging step's lib/build.js (Kit.build), and the browser has no WebGL
  'webgl-required-build': { files: { 'lib/build.js': [['window.KIT_BUILD = null;', "window.KIT_BUILD = { v8_current_cpu: 'arm', webgl: 'required' };"]] }, extra: '--disable-3d-apis',
    expect: 'FAIL, exactly ONE bad check: "WebGL 1 context" (Kit.build.webgl is "required": proves build.js is loaded before kit.js and that "required" means WebGL 1 only)' },
  // not required, no WebGL: PASS, but the coverage row and the banner say so
  'webgl-missing-info': { files: {}, extra: '--disable-3d-apis',
    expect: 'PASS, info row "WebGL coverage: ... NO WEBGL AT ALL", result.webglTested {1:false,2:false}, webglBanner "red" at the top' },
  // the 2D canvas draws one pixel to the right: the CRC / model checks must fail
  '2d-shifted': { files: { [H]: [['<script src="lib/canvas-webgl-model.js"></script>', "<script>{ const f = CanvasRenderingContext2D.prototype.fillRect; CanvasRenderingContext2D.prototype.fillRect = function (x, y, w, h) { return f.call(this, x + 1, y, w, h); }; }</script>\n<script src=\"lib/canvas-webgl-model.js\"></script>"]] },
    expect: 'FAIL: the golden CRC32 and model-pixel checks of the exact scene are bad on every context kind' },
  // the golden file holds a wrong CRC: the golden checks must fail while the model (independent) still agrees with the canvas
  'golden-wrong': { files: { 'data/canvas-webgl-golden.js': [['"crc": "65376b2c"', '"crc": "65376b2d"']] },
    expect: 'FAIL: every "= golden" check is bad; the "equals the reference model" check stays ok' },
  // WebGL clear colour with red and blue swapped: the WebGL checks must fail
  'webgl-clear-swapped': { files: { [H]: [['<script src="lib/canvas-webgl-model.js"></script>', "<script>for (const C of [WebGLRenderingContext, WebGL2RenderingContext]) { const f = C.prototype.clearColor; C.prototype.clearColor = function (r, g, b, a) { return f.call(this, b, g, r, a); }; }</script>\n<script src=\"lib/canvas-webgl-model.js\"></script>"]] },
    expect: 'FAIL: the WebGL clear / scissor / draw checks are bad for WebGL 1 and 2' },
};
const args = process.argv.slice(2);
const [outDir, name] = args;
if (!outDir || !M[name]) { console.error('usage: node mutants.js <scratch-dir> <' + Object.keys(M).join('|') + '> [--run]'); process.exit(2); }
const root = path.resolve(outDir, name), mk = path.join(root, 'kit'), mr = path.join(root, 'runner');
fs.rmSync(root, { recursive: true, force: true });
fs.cpSync(KIT, mk, { recursive: true }); fs.cpSync(RUNNER, mr, { recursive: true });
for (const [rel, reps] of Object.entries(M[name].files)) {
  const f = path.join(mk, rel); let s = fs.readFileSync(f, 'utf8');
  for (const [from, to] of reps) {
    const n = s.split(from).length - 1;
    if (n !== 1) { console.error(rel + ': mutation anchor matches ' + n + ' times (need 1): ' + from); process.exit(1); }
    s = s.replace(from, () => to);
  }
  fs.writeFileSync(f, s);
}
// the scratch runner can add URL parameters (KIT_QS_EXTRA); the real runner cannot
const rs = path.join(mr, 'run.js'); let rj = fs.readFileSync(rs, 'utf8');
const QS = "const qs = `config=${encodeURIComponent(config)}${armed ? '&armed=1' : ''}`;";
if (rj.split(QS).length !== 2) { console.error('runner: the qs line was not found exactly once'); process.exit(1); }
fs.writeFileSync(rs, rj.replace(QS, QS.replace('`;', "${process.env.KIT_QS_EXTRA || ''}`;")));
const out = 'F:\\cr\\device-1\\runs\\mut2-canvas-webgl-' + name;
const runArgs = [rs, '--config', 'default', '--pages', 'canvas-webgl', '--out', out].concat(M[name].extra ? ['--extra', M[name].extra] : []);
console.log('mutation ' + name + ' written to ' + root + '\nEXPECT: ' + M[name].expect);
if (args.includes('--run')) {
  const r = spawnSync(process.execPath, runArgs, { stdio: 'inherit', env: Object.assign({}, process.env, M[name].env || {}) });
  process.exit(r.status || 0);
} else console.log('run: ' + (M[name].env ? Object.entries(M[name].env).map(([k, v]) => k + '=' + v + ' ').join('') : '') + process.execPath + ' ' + runArgs.join(' '));
