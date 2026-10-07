// Mutation checks for kit\media.html (server only; not part of the kit that goes to the device).
//   node mutants.js <scratch-dir> <mutation> [--run]      node = F:\cr\src\third_party\node\win\node.exe
// Copies the page, kit\lib, kit\data and the runner to <scratch-dir>\<mutation>\kit and \runner (never touching the real ones), applies ONE
// mutation to the copy, and prints (or, with --run, executes) the runner command against the copy: the runner's own default autoplay policy
// applies (it passes no --autoplay-policy flag, like the device launchers). --run writes the run to F:\cr\device-1\runs\mut-media-<mutation>.
// Every replacement must match exactly once, or the tool stops, so a refactor of the page cannot silently turn a mutation into a no-op.
// What each mutation must do (EXPECT) is printed with it. Most mutations break the BROWSER's behaviour from an injected <script>, which is
// what the page's checks are for; the first two put back the old defect (a gesture-free page playing through an element the default
// autoplay policy refuses), which only a run under the default policy can catch.
'use strict';
const fs = require('fs'), path = require('path'), { spawnSync } = require('child_process');
const KIT = path.resolve(__dirname, '..', '..', 'kit'), RUNNER = path.resolve(__dirname, '..', '..', 'runner');
const HEAD = '<script src="lib/kit.js"></script>';
const inject = js => ({ 'media.html': [[HEAD, HEAD + '\n<script>' + js + '</script>']] });
const M = {
  // the old defect: audio-only clips in an <audio> element (muted or not, the default policy refuses it without a gesture)
  'audio-element': { files: { 'media.html': [["const el = document.createElement('video');", "const el = document.createElement(video ? 'video' : 'audio');"]] },
    expect: 'FAIL under the default policy: every audio-only clip has play() NotAllowedError ("loads: no error ..." and "currentTime passes 2 s" bad)' },
  // unmuted playback: refused by the default policy
  'unmuted': { files: { 'media.html': [['el.muted = true;', 'el.muted = false;']] },
    expect: 'FAIL under the default policy: every clip has play() NotAllowedError' },
  // the catalog says 1281 for the avc1 clip's width
  'size-wrong': { files: { 'data/media-index.js': [['"w": 1280,', '"w": 1281,']] },
    expect: 'FAIL: only "avc1 (H.264) / avc1: decoded frame size" is bad (want 1281x720 got 1280x720)' },
  // one byte of a clip changed: the embedded data no longer matches its recorded sha256
  'data-corrupt': { files: { 'data/media-vp8.js': [["['vp8'] = 'GkXf", "['vp8'] = 'GkXe"]] },
    expect: 'FAIL: "embedded clips match their recorded size and sha256" is bad (vp8) and the vp8 clip itself fails to load or decode' },
  // the video frame never reaches the canvas: a blank picture
  'blank-picture': { files: inject("{ const d = CanvasRenderingContext2D.prototype.drawImage; CanvasRenderingContext2D.prototype.drawImage = function (s) { return s instanceof HTMLVideoElement ? undefined : d.apply(this, arguments); }; }"),
    expect: 'FAIL: every video clip has "the frame has picture content (not blank or black)" bad' },
  // decodeAudioData hands back silence
  'silent-audio': { files: inject("{ const d = OfflineAudioContext.prototype.decodeAudioData; OfflineAudioContext.prototype.decodeAudioData = function (b) { return d.call(this, b).then(buf => { for (let c = 0; c < buf.numberOfChannels; c++) buf.getChannelData(c).fill(0); return buf; }); }; }"),
    expect: 'FAIL: every audio clip has "decodeAudioData gives non-silent PCM of the right length" bad' },
  // canPlayType says no to vp9: the kit's own assumption row fails and says so
  'codec-missing': { files: inject("{ const c = HTMLMediaElement.prototype.canPlayType; HTMLMediaElement.prototype.canPlayType = function (t) { return /vp9|vp09/.test(t) ? '' : c.call(this, t); }; }"),
    expect: 'FAIL: "vp9: canPlayType(...) is not empty" is bad and its message says KIT ASSUMPTION; the vp9 clip is still played and its failures (if any) say so too' },
};
const args = process.argv.slice(2);
const [outDir, name] = args;
if (!outDir || !M[name]) { console.error('usage: node mutants.js <scratch-dir> <' + Object.keys(M).join('|') + '> [--run]'); process.exit(2); }
const root = path.resolve(outDir, name), mk = path.join(root, 'kit'), mr = path.join(root, 'runner');
fs.rmSync(root, { recursive: true, force: true });
fs.mkdirSync(mk, { recursive: true });
fs.copyFileSync(path.join(KIT, 'media.html'), path.join(mk, 'media.html'));
fs.cpSync(path.join(KIT, 'lib'), path.join(mk, 'lib'), { recursive: true }); fs.cpSync(path.join(KIT, 'data'), path.join(mk, 'data'), { recursive: true });
fs.cpSync(RUNNER, mr, { recursive: true });
for (const [rel, reps] of Object.entries(M[name].files)) {
  if (!reps) continue;
  const f = path.join(mk, rel); let s = fs.readFileSync(f, 'utf8');
  for (const [from, to] of reps) {
    const n = s.split(from).length - 1;
    if (n !== 1) { console.error(rel + ': mutation anchor matches ' + n + ' times (need 1): ' + from); process.exit(1); }
    s = s.replace(from, () => to);
  }
  fs.writeFileSync(f, s);
}
const out = 'F:\\cr\\device-1\\runs\\mut-media-' + name;
const runArgs = [path.join(mr, 'run.js'), '--config', 'default', '--pages', 'media', '--out', out];
console.log('mutation ' + name + ' written to ' + root + '\nEXPECT: ' + M[name].expect);
if (args.includes('--run')) process.exit(spawnSync(process.execPath, runArgs, { stdio: 'inherit' }).status || 0);
console.log('run: ' + process.execPath + ' ' + runArgs.join(' '));
