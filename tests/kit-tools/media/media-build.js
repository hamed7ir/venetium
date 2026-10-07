// Venetium test kit — media.html's data builder and checker (BATCH-X86-2 §1). NODE ONLY, run on the server; the kit and the Surface 2
// never load it (it lives outside the kit folder on purpose).
//   node media-build.js --fetch              download the clips from Chromium's repo at the build's tag, verify, write the data
//   node media-build.js <dir>                the same, from a folder that already holds the upstream files (curl'd earlier)
//   node media-build.js --unpack <dir>       write the 13 original upstream files back out of the kit's data/media-*.js into <dir>
//                                            (checked against the pinned sha256), so that `node media-build.js <dir>` can be run offline
//   node media-build.js --check              offline check of the kit as it is: every data/media-*.js decodes to its pinned original,
//                                            the catalog agrees (data/media-index.js is compared byte for byte with what this tool generates), and
//                                            every row of manifest/media.tsv matches the file on disk
//   node media-build.js --manifest           rewrite manifest/media.tsv only, from the files as they are now (run it LAST, after the final edit of
//                                            media.html: the sha256 of the page and of every data file is read from disk, never remembered)
//   --kit <dir>                              work on another copy of the kit (default: ..\..\kit, i.e. tests\kit next to this folder)
// Writes (relative to the kit folder): data/media-<id>.js (one clip each, base64 in KIT_MEDIA), data/media-index.js (the catalog the
// page reads: sizes, sha256 of the ORIGINAL file, container and codec strings, expected frame size and duration) and
// manifest/media.tsv (header "path<TAB>sha256<TAB>source", one row per kit file of the page: media.html, lib/media-wav.js, the data files).
// Every upstream file's sha256 is pinned below: a file that does not match is refused.
'use strict';
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const argv = process.argv.slice(2);
const kitAt = argv.indexOf('--kit');
const KIT = path.resolve(kitAt >= 0 ? argv[kitAt + 1] : path.join(__dirname, '..', '..', 'kit'));
if (kitAt >= 0) argv.splice(kitAt, 2);
const TAG = '150.0.7871.226';
const BASE = 'https://chromium.googlesource.com/chromium/src/+/refs/tags/' + TAG + '/media/test/data/';
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const oneLine = s => String(s).replace(/[\t\r\n]+/g, ' ');
// id = the name the page uses. mime/codecs = how the page types the Blob (and the MediaSource, for avc1-aac). w/h/dur = what the file is
// (from its own boxes and from a decode on this build); the page checks the decoded size, and the played time against dur.
const CLIPS = [
  { id: 'avc1',        file: 'bear-1280x720.mp4',                sha256: 'bcb75d3db0a1a5056f4cd5c770ceccdb4cae920f21abb8139b29cd9ad39e3857', mime: 'video/mp4',  codecs: 'avc1.64001F,mp4a.40.2', video: true, audio: true, w: 1280, h: 720, dur: 2.763, note: 'H.264 High L3.1 (avcC 64 00 1F) + AAC-LC, ordinary progressive (not fragmented) MP4, interleaved: the common web file' },
  { id: 'aac',         file: 'bear-640x360-a_frag.mp4',          sha256: '68fdae35e7d5087367d1425eabef3c6101bbbc29aae8f403fec26883137c88f9', mime: 'audio/mp4',  codecs: 'mp4a.40.2',   video: false, audio: true,  dur: 2.804, note: 'AAC-LC (esds objectType 0x40, AudioSpecificConfig 12 10), audio only, fragmented MP4' },
  { id: 'avc1-aac',    file: 'bear-640x360-av_frag.mp4',         sha256: '883c32716072192f1e024cee812e6a8ae8d78300de90660ad0e4ce69ff96294c', mime: 'video/mp4',  codecs: 'avc1.64001E,mp4a.40.2', video: true, audio: true, w: 640, h: 360, dur: 2.87, mseOnly: true, note: 'H.264 High L3.0 + AAC-LC in one FRAGMENTED MP4: the MSE test only. Played straight from a blob: URL (FFmpegDemuxer) this file fails with PIPELINE_ERROR_DECODE (audio packet) in Venetium x86 AND in stock Chrome 154 x64, so it is not a direct-playback clip' },
  { id: 'aac-adts',    file: 'bear-audio-lc-aac.aac',            sha256: '4d820c54c41d850fd72d4b81780fc0e007a938643181e26dacbb5da0f6d569c7', mime: 'audio/aac',  codecs: '',            video: false, audio: true,  dur: 2.79,  note: 'AAC-LC in raw ADTS frames (the audio/aac container)' },
  { id: 'hvc1',        file: 'bear-320x240-v_frag-hevc.mp4',     sha256: '731890bd0da00cb5eba43602ca0572cda022992d4e4c79cd1d62e01a5daea0dc', mime: 'video/mp4',  codecs: 'hvc1.1.6.L93.B0', video: true, audio: false, w: 320, h: 240, dur: 2.736, note: 'HEVC Main (hvcC), video only, fragmented MP4 (sample entry hev1)' },
  { id: 'vp8',         file: 'bear-320x240-video-only.webm',     sha256: '744978d48d76ab0a251cfa42e67ebbfadbd34afdb0c61579022c5a5484ad8504', mime: 'video/webm', codecs: 'vp8',         video: true,  audio: false, w: 320, h: 240, dur: 2.703, note: 'VP8 video only in WebM' },
  { id: 'vp9',         file: 'bear-vp9.webm',                    sha256: '7cd68c55f4bb47a2df22a85fac9e8b942cbbba24286c037048131944eb0a09eb', mime: 'video/webm', codecs: 'vp9',         video: true,  audio: false, w: 320, h: 240, dur: 2.703, note: 'VP9 video only in WebM' },
  { id: 'av01',        file: 'bear-av1.webm',                    sha256: '66e87771e204c6d86ce8ed86ce62c44e70f98325cbbdf8d077150c483e6e8ebb', mime: 'video/webm', codecs: 'av01.0.00M.08', video: true, audio: false, w: 320, h: 240, dur: 2.703, note: 'AV1 Main L2.0 8-bit video only in WebM (the mp4 twin has av1C 81 00 0C 00)' },
  { id: 'opus',        file: 'bear-opus.ogg',                    sha256: 'c074407b7126060972b5e9eb4081e04e35c88857d7125151cbb9e5d9a1191dae', mime: 'audio/ogg',  codecs: 'opus',        video: false, audio: true,  dur: 2.767, note: 'Opus in Ogg' },
  { id: 'vorbis',      file: 'bear-320x240-audio-only.webm',     sha256: 'f80882524401e748856d1f36f9144b492da9049eee6fc950cba52ffd0e8915a4', mime: 'audio/webm', codecs: 'vorbis',      video: false, audio: true,  dur: 2.744, note: 'Vorbis audio only in WebM' },
  { id: 'mp3',         file: 'bear-audio-10s-VBR-has-TOC.mp3',   sha256: '11c132fb96f1ad28230d2927997eb600a2d8870c3265f66b4edc83bf7ac30fbd', mime: 'audio/mpeg', codecs: '',            video: false, audio: true,  dur: 10.018, note: 'MP3, VBR with a Xing TOC, 10 s' },
  { id: 'flac',        file: 'bear-flac.mp4',                    sha256: '358d7b65ec0157b59ee5f2f8f923da255c2d906d83cfb1cb7448ac804d4b5b7e', mime: 'audio/mp4',  codecs: 'flac',        video: false, audio: true,  dur: 2.74,  note: 'FLAC in MP4 (no native .flac in media/test/data is longer than 1.1 s: bear.flac 1.07 s, sfx.flac 0.29 s)' },
  { id: 'flac-native', file: 'bear.flac',                        sha256: '71433fc931981db5a831694d5a156dd38d798dacbf2f04593ce7ee2371fac864', mime: 'audio/flac', codecs: '',            video: false, audio: true,  dur: 1.067, short: true, note: 'native .flac container; 1.07 s, so the page plays it to its end instead of past 2 s' },
];
const dataRel = c => 'data/media-' + c.id + '.js';
const dataJs = (c, buf) => "(window.KIT_MEDIA = window.KIT_MEDIA || {})['" + c.id + "'] = '" + buf.toString('base64') + "';\n";
const kitFile = rel => path.join(KIT, rel);
// the original upstream bytes, decoded back out of the kit's own data file (and checked against the pinned sha256)
function unpackClip(c) {
  const js = fs.readFileSync(kitFile(dataRel(c)), 'utf8');
  const m = /= '([A-Za-z0-9+\/=]+)';\s*$/.exec(js);
  if (!m) throw new Error(dataRel(c) + ': no base64 payload found');
  const buf = Buffer.from(m[1], 'base64');
  if (sha(buf) !== c.sha256) throw new Error(dataRel(c) + ': decodes to sha256 ' + sha(buf) + ' != pinned ' + c.sha256);
  return buf;
}
// the catalog the page reads (data/media-index.js), byte for byte as this tool writes it; bytesOf(clip) = the length of the original file.
// --check builds it from the kit's own data files and compares it with the file on disk, so every field (w, h, dur, mime, codecs, note ...)
// is covered, not only the sizes and hashes.
const catalogJs = bytesOf => '// generated by kit-tools/media/media-build.js — the catalog of media.html\'s clips (sha256 = of the original upstream file)\nwindow.KIT_MEDIA_INDEX = ' +
  JSON.stringify(CLIPS.map(c => Object.assign({}, c, { bytes: bytesOf(c), source: 'media/test/data/' + c.file + ' @ ' + TAG })), null, 1) + ';\n';
// the manifest: "path<TAB>sha256<TAB>source", one row per file of the page inside the kit; sha256 = of the file as it is in the kit now
function manifestRows(idxJs) {
  const rows = [['path', 'sha256', 'source'].join('\t')];
  const add = (rel, buf, source) => rows.push([rel, sha(buf), oneLine(source)].join('\t'));
  add('media.html', fs.readFileSync(kitFile('media.html')), 'original code (this kit): the media test page, written for BATCH-X86-2 §1; loads lib/media-wav.js and data/media-*.js by <script src>');
  for (const c of CLIPS) {
    const buf = unpackClip(c);
    add(dataRel(c), fs.readFileSync(kitFile(dataRel(c))), BASE + c.file + '?format=TEXT (Chromium ' + TAG + ', media/test/data/' + c.file + '); the original is ' + buf.length + ' bytes, sha256 ' + c.sha256 + '; base64 in KIT_MEDIA by kit-tools/media/media-build.js; ' + c.note);
  }
  add('data/media-index.js', idxJs ? Buffer.from(idxJs) : fs.readFileSync(kitFile('data/media-index.js')), 'generated by kit-tools/media/media-build.js from its CLIPS table: the catalog the page reads (size, sha256 of the original, MIME and codec strings, expected frame size and duration)');
  add('lib/media-wav.js', fs.readFileSync(kitFile('lib/media-wav.js')), 'original code (this kit): builds the PCM WAV test file (3.0 s, 22050 Hz mono s16le, 440 Hz sine) at run time, since no WAV file in Chromium media/test/data is longer than 1.07 s; there is no WAV file in the kit');
  return rows;
}
(async () => {
  const arg = argv[0];
  if (arg === '--check') {
    let bad = 0;
    const say = (ok, msg) => { if (!ok) bad++; console.log((ok ? 'ok   ' : 'BAD  ') + msg); };
    const idx = (() => { const ctx = { window: {} }; require('vm').runInNewContext(fs.readFileSync(kitFile('data/media-index.js'), 'utf8'), ctx); return ctx.window.KIT_MEDIA_INDEX; })();
    for (const c of CLIPS) {
      let buf = null, why = '';
      try { buf = unpackClip(c); } catch (e) { why = e.message; }
      const e = idx.find(x => x.id === c.id);
      say(!!buf && !!e && e.bytes === buf.length && e.sha256 === c.sha256, c.id.padEnd(12) + (buf ? buf.length + ' bytes decode to the pinned sha256; catalog ' + (e ? (e.bytes === buf.length && e.sha256 === c.sha256 ? 'agrees' : 'DISAGREES') : 'entry missing') : why));
    }
    // the whole catalog file against what this tool generates from the pinned CLIPS table and the kit's decoded clips
    let wantIdx = null; try { wantIdx = catalogJs(c => unpackClip(c).length); } catch (e) {}
    const haveIdx = fs.readFileSync(kitFile('data/media-index.js'), 'utf8');
    say(wantIdx !== null && haveIdx === wantIdx, 'data/media-index.js is byte for byte what this tool generates' + (wantIdx === null ? ' (cannot generate: a data file does not decode)' : haveIdx === wantIdx ? '' : ' - it DIFFERS (first difference at character ' + [...haveIdx].findIndex((ch, i) => ch !== wantIdx[i]) + ' of the generated text); regenerate with: node media-build.js <dir of originals> (see --unpack)'));
    const lines = fs.readFileSync(kitFile('manifest/media.tsv'), 'utf8').split('\n').filter(Boolean);
    say(lines[0] === 'path\tsha256\tsource', 'manifest header is exactly "path<TAB>sha256<TAB>source"');
    const want = [kitFile('media.html')].concat(CLIPS.map(c => kitFile(dataRel(c))), [kitFile('data/media-index.js'), kitFile('lib/media-wav.js')]).map(p => path.relative(KIT, p).replace(/\\/g, '/'));
    const have = lines.slice(1).map(l => l.split('\t')[0]);
    say(want.length === have.length && want.every(p => have.includes(p)), 'manifest lists exactly the ' + want.length + ' kit files of the page (' + have.length + ' rows)');
    for (const l of lines.slice(1)) {
      const [rel, h, src] = l.split('\t');
      let got = null; try { got = sha(fs.readFileSync(kitFile(rel))); } catch (e) {}
      say(got === h && l.split('\t').length === 3 && !!src, rel.padEnd(26) + (got === h ? 'sha256 matches the file' : 'sha256 does NOT match (file ' + got + ', manifest ' + h + ')'));
    }
    console.log(bad ? bad + ' problem(s)' : 'all ok');
    process.exit(bad ? 1 : 0);
  }
  if (arg === '--manifest') {
    const rows = manifestRows(null);
    fs.writeFileSync(kitFile('manifest/media.tsv'), rows.join('\n') + '\n');
    console.log('manifest/media.tsv rewritten (' + (rows.length - 1) + ' rows plus the header)');
    return;
  }
  if (arg === '--unpack') {
    const dir = argv[1]; if (!dir) { console.error('usage: node media-build.js --unpack <dir>'); process.exit(2); }
    fs.mkdirSync(dir, { recursive: true });
    for (const c of CLIPS) { const buf = unpackClip(c); fs.writeFileSync(path.join(dir, c.file), buf); console.log(c.file.padEnd(34), String(buf.length).padStart(7), 'sha256 ok'); }
    return;
  }
  if (!arg) { console.error('usage: node media-build.js --fetch | <dir> | --unpack <dir> | --check | --manifest   [--kit <kit folder>]'); process.exit(2); }
  fs.mkdirSync(path.join(KIT, 'data'), { recursive: true }); fs.mkdirSync(path.join(KIT, 'manifest'), { recursive: true });
  const lens = {};
  let total = 0;
  for (const c of CLIPS) {
    let buf;
    if (arg === '--fetch') {
      const r = await fetch(BASE + c.file + '?format=TEXT');
      if (!r.ok) throw new Error(c.file + ': HTTP ' + r.status);
      buf = Buffer.from((await r.text()).replace(/\s+/g, ''), 'base64');
    } else buf = fs.readFileSync(path.join(arg, c.file));
    if (sha(buf) !== c.sha256) throw new Error(c.file + ': sha256 ' + sha(buf) + ' != pinned ' + c.sha256);
    fs.writeFileSync(kitFile(dataRel(c)), dataJs(c, buf));
    total += buf.length;
    lens[c.id] = buf.length;
    console.log(c.id.padEnd(12), String(buf.length).padStart(7), c.file);
  }
  const idxJs = catalogJs(c => lens[c.id]);
  fs.writeFileSync(kitFile('data/media-index.js'), idxJs);
  fs.writeFileSync(kitFile('manifest/media.tsv'), manifestRows(idxJs).join('\n') + '\n');
  console.log('total clip bytes', total, '; manifest/media.tsv written (' + (CLIPS.length + 4) + ' rows incl. header)');
})().catch(e => { console.error(e.stack || e); process.exit(1); });
