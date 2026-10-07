// Mutation checks for kit\webrtc.html (server only; not part of the kit that goes to the device).
//   node mutants.js <scratch-dir> <mutation> [--run]      node = F:\cr\src\third_party\node\win\node.exe
// Copies the kit (lib + page only: this page loads nothing else) and the runner to <scratch-dir>\<mutation>\kit and \runner, applies ONE
// mutation to the copy of webrtc.html, and prints (or, with --run, executes) the runner command against the copy; --run writes the run to
// F:\cr\device-1\runs\mut2-webrtc-<mutation>. Every replacement must match exactly once, or the tool stops (so a refactor of webrtc.html
// cannot silently turn a mutation into a no-op). What each mutation must do (EXPECT) is printed with it.
'use strict';
const fs = require('fs'), path = require('path'), { spawnSync } = require('child_process');
const KIT = path.resolve(__dirname, '..', '..', 'kit'), RUNNER = path.resolve(__dirname, '..', '..', 'runner');
const GUM_LIMIT = "45000, 'getUserMedia (on the device: allow the permission prompt)'";
const STOP_LATE = 'for (const t of tracks) { try { t.stop(); } catch (e) {} }';
const COLOUR_ANCHOR = "    const COLOUR = 'remote video frame has the colours of the local camera frame';";
const GUARD = 'if (!(L.readyState >= 2 && R.readyState >= 2)) {';
const FALLBACK = "if (err.name === 'NotAllowedError' && remoteMode.startsWith('unmuted'))";
const HEAD = '<script src="lib/kit.js"></script>';
const REMOTE_ADD = "      remote.addTrack(e.track);";
// the receive side reports silence for the audio stream while the source is loud: only the stats the page reads are changed
const ZERO_INBOUND = HEAD + "\n<script>{ const g = RTCPeerConnection.prototype.getStats; RTCPeerConnection.prototype.getStats = async function (...a) { const r = await g.apply(this, a); " +
  "return { forEach: f => r.forEach(s => f(s.type === 'inbound-rtp' && (s.kind || s.mediaType) === 'audio' ? Object.assign({}, s, { totalAudioEnergy: 0, audioLevel: 0 }) : s)) }; }; }</script>";
// what the autoplay policy does to an unmuted element without a user gesture (document-user-activation-required): play() is refused
const POLICY = HEAD + "\n<script>{ const p = HTMLMediaElement.prototype.play; HTMLMediaElement.prototype.play = function () { " +
  "if (!this.muted) return Promise.reject(new DOMException(\"play() failed because the user didn't interact with the document first.\", 'NotAllowedError')); return p.call(this); }; }</script>";
const M = {
  'late-gum': { rep: [[GUM_LIMIT, GUM_LIMIT.replace('45000', '1')]],
    expect: 'FAIL (its first check "returns the tracks" saw the timeout) but the check "getUserMedia that answered after its time limit has its tracks stopped" is ok' },
  'late-gum-nostop': { rep: [[GUM_LIMIT, GUM_LIMIT.replace('45000', '1')], [STOP_LATE, '/* the late stream is not stopped */']],
    expect: 'the check "getUserMedia that answered after its time limit has its tracks stopped" is BAD ("still audio live, video live")' },
  'no-frame': { rep: [[COLOUR_ANCHOR, "    L.srcObject = null; R.srcObject = null;\n" + COLOUR_ANCHOR]],
    expect: 'the colour check is BAD with "no frame to compare"' },
  'no-frame-noguard': { rep: [[COLOUR_ANCHOR, "    L.srcObject = null; R.srcObject = null;\n" + COLOUR_ANCHOR], [GUARD, 'if (false) {']],
    expect: 'the colour check PASSES (the weakness the guard closes): proves the guard is what makes the check real' },
  // the device policy refuses an unmuted element: the page must mute the remote element and keep passing, with the one audible-only check n/a
  'autoplay-refused': { rep: [[HEAD, POLICY]],
    expect: 'PASS; "remote element playback" says muted; "inbound audio carries the signal" is n/a (the only n/a); totalSamplesReceived and the frame checks still ok' },
  // the same refusal without the muted fallback: the remote element never plays, and the checks that need it must fail
  'autoplay-refused-nofallback': { rep: [[HEAD, POLICY], [FALLBACK, 'if (false)']],
    expect: 'FAIL: "remote video frame has the colours of the local camera frame" (the refused element shows black) and "inbound audio carries the signal" are BAD: without the fallback the refusal would sink the page' },
};
// quiet microphone: a fake capture device fed by a WAV of Gaussian white noise at rms <n> LSB of int16 (48 kHz, mono, 12 s; made by this tool; 60 and above are loud enough for the real check,
// deterministic). Not a mutation of the page: the page is unchanged. At 1 and 3 LSB the receiver gets all-zero samples after the 0.1 playout gain.
const quiet = n => ({ rep: [], wavRms: n, expect: n <= 30 ? 'PASS with exactly 1 n/a "inbound audio carries the signal" (source level far below 0.003: too low to survive the 0.1 playout gain); the inbound energy is printed in exponent form'
  : 'PASS: the max source level is about >= 0.003, so the check is REAL (not n/a) and ok: the signal survives the 0.1 gain' });
for (const n of [1, 3, 5, 10, 30, 60, 100, 300]) M['quiet-rms' + n] = quiet(n);
M['inbound-energy-zero'] = { rep: [[HEAD, ZERO_INBOUND]],
  expect: 'FAIL, exactly 1 bad check: "inbound audio carries the signal (inbound energy > 0)" (the default fake microphone is loud: max source level 1.0 >= 0.003, so the gate does not excuse a receive path that reports zero energy)' };
M['no-audio-playout'] = { rep: [[REMOTE_ADD, "      if (e.track.kind === 'video') remote.addTrack(e.track);"]],
  expect: 'FAIL: the remote audio track never reaches a playing element, so WebRTC pulls no samples; "inbound audio carries the signal (inbound energy > 0)" is BAD (loud source, remote element unmuted: not n/a) next to the totalSamplesReceived rows' };
function wav(n, file) {
  const N = 48000 * 12, buf = Buffer.alloc(44 + N * 2); let s = 12345;
  const u = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return (s + 0.5) / 4294967296; };
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + N * 2, 4); buf.write('WAVEfmt ', 8); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(1, 22);
  buf.writeUInt32LE(48000, 24); buf.writeUInt32LE(96000, 28); buf.writeUInt16LE(2, 32); buf.writeUInt16LE(16, 34); buf.write('data', 36); buf.writeUInt32LE(N * 2, 40);
  for (let i = 0; i < N; i++) { const g = Math.sqrt(-2 * Math.log(u())) * Math.cos(2 * Math.PI * u()); buf.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(g * n))), 44 + i * 2); }
  fs.writeFileSync(file, buf);
}
const args = process.argv.slice(2);
const [outDir, name] = args;
if (!outDir || !M[name]) { console.error('usage: node mutants.js <scratch-dir> <' + Object.keys(M).join('|') + '> [--run]'); process.exit(2); }
let html = fs.readFileSync(path.join(KIT, 'webrtc.html'), 'utf8');
for (const [from, to] of M[name].rep) {
  const n = html.split(from).length - 1;
  if (n !== 1) { console.error('mutation anchor matches ' + n + ' times (need 1): ' + from); process.exit(1); }
  html = html.replace(from, () => to);
}
const root = path.resolve(outDir, name), mk = path.join(root, 'kit'), mr = path.join(root, 'runner');
fs.rmSync(root, { recursive: true, force: true });
fs.mkdirSync(path.join(mk, 'lib'), { recursive: true }); fs.mkdirSync(mr, { recursive: true });
fs.writeFileSync(path.join(mk, 'webrtc.html'), html);
for (const f of ['configs.js', 'kit.js', 'build.js']) fs.copyFileSync(path.join(KIT, 'lib', f), path.join(mk, 'lib', f));
for (const f of fs.readdirSync(RUNNER)) fs.copyFileSync(path.join(RUNNER, f), path.join(mr, f));
const out = 'F:\\cr\\device-1\\runs\\mut2-webrtc-' + name;
const runArgs = [path.join(mr, 'run.js'), '--config', 'default', '--pages', 'webrtc', '--out', out];
if (M[name].wavRms) { const f = path.join(root, 'noise-rms' + M[name].wavRms + '.wav'); wav(M[name].wavRms, f); runArgs.push('--extra', '--use-file-for-fake-audio-capture=' + f); }
console.log('mutation ' + name + ' written to ' + root + '\nEXPECT: ' + M[name].expect);
if (args.includes('--run')) process.exit(spawnSync(process.execPath, runArgs, { stdio: 'inherit' }).status || 0);
console.log('run: ' + process.execPath + ' ' + runArgs.join(' '));
