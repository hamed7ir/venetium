// Venetium test kit - js-f1 page glue (BATCH-X86-2 step 7; tier decision reworked in BATCH-DEVICE-1 section 1). Hand-written; no V8 natives syntax here.
// Runs F1Probe (lib/js-f1-probe.js, derived from f1probe.js) with the kit as its reporter, and does what f1probe.py does with the
// d8 output: parse the CHECK / TIER / SUMMARY lines, require every check OK, require the configuration's tier to be proven
// (when natives syntax is on), and report whether F1's own pattern was seen. `Kit` is passed in, so the same glue also runs in
// Node with the real lib/kit.js in a stub DOM (the rehearsal: venetium/tests/kit-tools/js-f1/js-f1-nodetest.js).
//   F1Host.run(Kit [, {probe, natives}])  -> resolves when every kit row is written; F1Host.extra goes to Kit.done(extra)
//     (the options are seams for the Node rehearsal only: the page passes none)
// The tier proof, by configuration (`tier` in lib/configs.js):
//   tier null (default, liftoff-only, no-liftoff)  n/a: "this configuration selects no JS tier", natives syntax on or off; the 72 checks still run
//   interpreter, sparkplug, turbofan               proven through %GetOptimizationStatus (interpreter: also the lite-mode bit, set only by --jitless)
//   a named tier with natives syntax off           a FAILED check (a configs.js error), never an n/a
//   maglev                                         proven whenever %IsMaglevEnabled() is true (the CPU / build never matters then). With
//                                                  %IsMaglevEnabled() false the check is n/a ONLY where the build has no Maglev:
//                                                    - lib/build.js present (Kit.build, written by the packaging step from the build's own
//                                                      v8_build_config.json): only when it says has_maglev === false AND v8_current_cpu === "x86"
//                                                      (V8 builds no Maglev for x86 and builds it for every other CPU: a record that says
//                                                      has_maglev false for arm / arm64 / x64 / no CPU is a FAILED check, "the build record says
//                                                      <cpu> has no Maglev, but V8 builds Maglev for <cpu>");
//                                                    - no record (Kit.build null, as in the record's copy): only when Kit.arch().ia32.
//                                                  Everything else (has_maglev true, an unknown record, non-ia32, no / failed / hung answer to
//                                                  the architecture question) is a FAILED check: Maglev is built for every CPU but ia32.
// Expected values come from lib/configs.js (the flags) and lib/build.js, never from the browser under test.
var F1Host = (function () {
  'use strict';
  var F1_PATTERN = '[global],1,2';      // f1probe.py: the "sloppy plain 2of3" params a pre-0011 Windows ARM32 d8 shows (receiver + 1,2)
  var N_CHECKS = 72, N_CALLS = 24;      // 24 calls x 3 checks
  var LITE_MODE = 4096;                 // OptimizationStatus kLiteMode (1 << 12) in Chromium 150's V8 (src/runtime/runtime.h): --jitless sets it
  var extra = {};

  function withTimeout(p, ms, what) {
    var t;
    return Promise.race([p, new Promise(function (_, rej) { t = setTimeout(function () { rej(new Error('timeout after ' + ms + ' ms: ' + what)); }, ms); })])
      .then(function (v) { clearTimeout(t); return v; }, function (e) { clearTimeout(t); throw e; });
  }
  function errText(e) { return String((e && (e.stack || e.message)) || e).split('\n').slice(0, 3).join(' | '); }

  // The CPU the browser reports, from the shared Kit.arch() (never rejects; 3 s timeout inside kit.js). Chromium reports architecture 'x86' for ia32
  // AND x64; ia32 = 'x86' and (bitness '32' or wow64 true); Windows ARM32 reports architecture '' / bitness '64' / wow64 false (a Chromium
  // quirk, not a finding). Whatever goes wrong here leaves ia32 false, and `note` says why.
  async function readArch(Kit) {
    var a;
    try { a = (typeof Kit.arch === 'function') ? await Kit.arch() : { note: 'Kit.arch() is not available (lib/kit.js too old)' }; }
    catch (e) { a = { note: 'Kit.arch() threw: ' + errText(e) }; }
    a = a || {};
    return { architecture: a.architecture === undefined ? null : a.architecture, bitness: a.bitness === undefined ? null : a.bitness,
      wow64: a.wow64 === undefined ? null : a.wow64, ia32: a.ia32 === true, note: a.note || '' };
  }

  // May a missing Maglev (%IsMaglevEnabled() false) be reported n/a in this configuration? -> { ok, why }
  function noMaglevAllowed(Kit, cfg, archInfo) {
    var b = Kit.build, cpu = JSON.stringify({ architecture: archInfo.architecture, bitness: archInfo.bitness, wow64: archInfo.wow64, note: archInfo.note || undefined });
    if (cfg.ia32 !== 'n/a') return { ok: false, why: 'lib/configs.js does not declare "' + Kit.config + '" as ia32: n/a' };
    if (b) {
      if (b.has_maglev === false) {
        // V8 builds Maglev for every CPU but x86 (v8/BUILD.gn): has_maglev false is believable only for v8_current_cpu "x86". An arm / arm64 / x64
        // record (or one naming no CPU) that says "no Maglev" is a wrong record or a Maglev-less build of a CPU that must have it: a FAILED check.
        if (b.v8_current_cpu === 'x86') return { ok: true, why: 'the build record (lib/build.js) says has_maglev false for v8_current_cpu x86, the one CPU V8 builds no Maglev for' };
        if (typeof b.v8_current_cpu === 'string' && b.v8_current_cpu !== '') {
          return { ok: false, why: 'the build record says ' + b.v8_current_cpu + ' has no Maglev, but V8 builds Maglev for ' + b.v8_current_cpu + ' (lib/build.js: ' + JSON.stringify(b) + ')' };
        }
        return { ok: false, why: 'the build record says has_maglev false but names no v8_current_cpu, and only "x86" may lack Maglev (lib/build.js: ' + JSON.stringify(b) + ')' };
      }
      if (b.has_maglev === true) return { ok: false, why: 'the build record (lib/build.js) says has_maglev true (v8_current_cpu ' + b.v8_current_cpu + '): this build has Maglev' };
      return { ok: false, why: 'the build record (lib/build.js) has no boolean has_maglev: ' + JSON.stringify(b) };
    }
    if (archInfo.ia32) return { ok: true, why: 'no build record; the browser reports ia32 (' + cpu + '), the one CPU V8 builds no Maglev for' };
    return { ok: false, why: 'no build record, and the CPU the browser reports ' + cpu + ' is not ia32' };
  }

  async function run(Kit, opt) {
    opt = opt || {};
    var cfg = Kit.cfg || {};
    var flags = cfg.flags || [], hasFlag = function (f) { return flags.indexOf(f) >= 0; };
    var wantTier = cfg.tier || null, who = cfg.who || null;
    // f1probe.py names its F1_MODE after the configuration; configurations that select no JS tier (default, liftoff-only, no-liftoff)
    // run f1probe.js's plain mode (no warm-up), which is also what f1probe.js does when F1_MODE is unset.
    var mode = wantTier ? Kit.config : 'default';
    globalThis.F1_MODE = mode;                       // f1probe.js line 21 reads it
    extra.mode = mode; extra.natives = !!Kit.natives; extra.wantTier = wantTier; extra.tierOf = who;

    // ---- the natives-only script (only when the configuration enables --allow-natives-syntax) ----
    var N = null, nativesErr = null;
    if (Kit.natives) {
      if (opt.natives) N = opt.natives;
      else {
        try {
          await withTimeout(Kit.loadScript('lib/js-f1-natives.js'), 15000, 'loading lib/js-f1-natives.js');
          N = (typeof F1Natives !== 'undefined') ? F1Natives : null;   // a SyntaxError (no natives syntax) still fires the script's load event
          if (!N) nativesErr = 'lib/js-f1-natives.js loaded but F1Natives is undefined (a SyntaxError: --allow-natives-syntax did not reach V8?)';
        } catch (e) { nativesErr = errText(e); }
      }
      if (!N) Kit.check('natives-only script loaded (configuration has --allow-natives-syntax)', 'F1Natives defined', nativesErr, false);
    }
    var probe = opt.probe || (typeof F1Probe !== 'undefined' ? F1Probe : null);
    if (!probe) { Kit.check('lib/js-f1-probe.js loaded', 'F1Probe defined', 'F1Probe is undefined', false); return; }

    // ---- run f1probe.js's logic; print becomes a buffer, parsed below exactly like f1probe.py parses d8's stdout ----
    var lines = [];
    var host = { print: function (s) { lines.push(String(s)); } };
    if (N) { host.tier = N.tier; host.warmup = N.warmup; }
    var t0 = Date.now(), threw = null;
    try { probe(host); } catch (e) { threw = errText(e); }
    extra.probeMs = Date.now() - t0;
    if (threw) Kit.check('F1Probe ran to its SUMMARY line', 'no exception', threw, false);

    var chk = [], tiers = [], summ = [];
    lines.forEach(function (l) {
      if (l.indexOf('CHECK ') === 0) chk.push(l.split(' | '));
      else if (l.indexOf('TIER ') === 0) tiers.push(l);
      else if (l.indexOf('SUMMARY ') === 0) summ.push(l);
    });
    var distinct = {}; chk.forEach(function (c) { distinct[c[1]] = 1; });
    var nDistinct = Object.keys(distinct).length;
    var badInLog = chk.filter(function (c) { return c[3] === 'BAD'; }).length;
    var f1Here = chk.some(function (c) { return c[1] === 'sloppy plain 2of3' && c[2] === 'params' && c[4] === 'got ' + F1_PATTERN; });
    extra.probeChecks = chk.length; extra.probeBad = badInLog; extra.probeCalls = nDistinct; extra.f1PatternSeen = f1Here;
    extra.summary = summ[0] || null;
    extra.lines = lines;                              // every CHECK / TIER / SUMMARY line, as d8 would print them

    // ---- rows: the facts first, then the page-level checks, then the tier proof, then the 72 probe checks ----
    Kit.info('configuration', Kit.config + ': F1_MODE=' + mode + '; --allow-natives-syntax ' + (Kit.natives ? 'on' : 'off') + '; js-flags ' + (flags.join(' ') || '(none)'));
    // the build record (lib/build.js: written per package by the packaging step; null in the record's own copy) and the CPU the browser reports:
    // together they decide whether a missing Maglev may be "n/a". Read in every configuration, shown as information.
    var build = Kit.build || null;
    extra.build = build;
    Kit.info('kit build record (lib/build.js; null = unknown, the CPU decides)', build);
    var archInfo = await readArch(Kit);
    extra.arch = archInfo;
    Kit.info('CPU architecture as the browser reports it (Kit.arch(); Windows ARM32 reports architecture "" and bitness 64: a Chromium quirk, not a finding)',
      { architecture: archInfo.architecture, bitness: archInfo.bitness, wow64: archInfo.wow64, ia32: archInfo.ia32, note: archInfo.note || undefined });
    var maglevOn = null;
    if (N) {
      try {
        maglevOn = !!N.maglevEnabled();
        extra.tiersEnabled = { sparkplug: !!N.sparkplugEnabled(), maglev: maglevOn, turbofan: !!N.turbofanEnabled() };
        Kit.info('V8 tiers enabled in this build + configuration (%IsSparkplugEnabled / %IsMaglevEnabled / %IsTurbofanEnabled)', extra.tiersEnabled);
        // the flags reached V8: the expected values come from the configuration's own flag list (lib/configs.js) and, for Maglev where no flag
        // decides, from the build record - never from the browser
        var fe = { sparkplug: null, maglev: null, turbofan: null }, feFrom = { sparkplug: 'flags reached V8', maglev: 'flags reached V8', turbofan: 'flags reached V8' };
        if (hasFlag('--jitless')) fe.sparkplug = fe.maglev = fe.turbofan = false;
        if (hasFlag('--no-sparkplug')) fe.sparkplug = false;
        if (hasFlag('--no-maglev')) fe.maglev = false;
        if (hasFlag('--no-turbofan')) fe.turbofan = false;
        if (hasFlag('--always-sparkplug')) fe.sparkplug = true;
        if (fe.maglev === null && build && typeof build.has_maglev === 'boolean') { fe.maglev = build.has_maglev; feFrom.maglev = 'build record (has_maglev) vs V8'; }
        Object.keys(fe).forEach(function (k) {
          if (fe[k] !== null) Kit.check(feFrom[k] + ': ' + k + ' enabled', fe[k], extra.tiersEnabled[k], extra.tiersEnabled[k] === fe[k]);
        });
      } catch (e) { Kit.check('tier availability probes (%IsMaglevEnabled etc.)', 'answer', errText(e), false); }
    }

    Kit.check('probe ran: 24 calls x 3 checks', N_CALLS + ' calls, ' + N_CHECKS + ' CHECK lines', nDistinct + ' calls, ' + chk.length + ' CHECK lines',
      chk.length === N_CHECKS && nDistinct === N_CALLS);
    var wantSumm = 'SUMMARY ' + mode + ' checks=' + N_CHECKS + ' bad=0';
    Kit.check('SUMMARY line (f1probe.js: every check OK)', wantSumm, summ.length === 1 ? summ[0] : (summ.length + ' SUMMARY lines'), summ.length === 1 && summ[0] === wantSumm);
    Kit.check('F1 pattern not seen ("sloppy plain 2of3" params must not be ' + F1_PATTERN + ')', 'not seen', f1Here ? 'SEEN: params ' + F1_PATTERN : 'not seen', !f1Here);

    // ---- the tier proof (f1probe.py: tiers non-empty and every status startswith "<tier>(") ----
    var tierWord = 'not proven', naWhy = '';
    var mine = who ? tiers.filter(function (l) { return l.indexOf('TIER ' + who + ' ') === 0; })
                       .map(function (l) { var t = l.trim().split(/\s+/); return { name: t.slice(2, -1).join(' '), status: t[t.length - 1] }; }) : [];
    var expectN = who === 'caller' ? N_CALLS : 6;
    extra.tierLines = mine.map(function (t) { return t.name + ' ' + t.status; });
    if (!wantTier) {
      // default / liftoff-only / no-liftoff select no JS tier: n/a whether or not natives syntax is on (liftoff-only and no-liftoff carry it for wasm.html)
      tierWord = 'n/a'; naWhy = 'configuration selects no JS tier';
      Kit.na('tier proof (%GetOptimizationStatus)', 'not applicable: the "' + Kit.config + '" configuration selects no JS tier to prove' + (cfg.wasmTier ? ' (its flag picks the wasm ' + cfg.wasmTier + ' tier)' : '') +
        (Kit.natives ? '' : '; --allow-natives-syntax is off'));
    } else if (!Kit.natives) {
      // a configuration that names a tier but turns natives off cannot be proven: a configs.js error, never an n/a
      tierWord = 'NOT PROVEN';
      Kit.check('tier proof: ' + wantTier + ' (' + who + 's)', wantTier + '(...) for every function', 'not proven: --allow-natives-syntax is off in the "' + Kit.config + '" configuration (lib/configs.js)', false);
    } else if (!N) {
      tierWord = 'NOT PROVEN';
      Kit.check('tier proof: ' + wantTier + ' (' + who + 's)', wantTier + '(...) for every function', 'not proven: the natives-only script is not available', false);
    } else if (wantTier === 'maglev' && maglevOn === false) {
      var allowed = noMaglevAllowed(Kit, cfg, archInfo);
      extra.noMaglev = allowed;
      if (!allowed.ok) {
        // Maglev is built for every CPU but ia32 (ARM32 included): a missing Maglev anywhere else is a build / launch fault, not an n/a
        tierWord = 'NOT PROVEN';
        Kit.check('tier proof: maglev (a missing Maglev is n/a only where the build has none)', 'Maglev present: %IsMaglevEnabled() true',
          '%IsMaglevEnabled() false (no Maglev in this build, or --no-maglev reached V8); ' + allowed.why, false);
      } else {
        tierWord = 'n/a'; naWhy = 'no Maglev in this build: ' + allowed.why;
        Kit.na('tier proof: maglev', 'not applicable: this build has no Maglev and V8 agrees - %IsMaglevEnabled() is false (v8_flags.maglev is read-only false in a build without Maglev; ' +
          '%OptimizeMaglevOnNextCall only prints "Maglev is not enabled."); ' + allowed.why + '. The probe\'s 72 checks still ran in this configuration. Maglev applies on ARM32.');
        // consistency of two V8 answers (not Maglev evidence: a V8 without Maglev cannot produce Maglev code): %IsMaglevEnabled() false and the
        // status of every function the probe looked at agree, and the probe did produce its tier lines
        var seenMaglev = mine.filter(function (t) { return /^maglev/.test(t.status); });
        Kit.check('no function reports Maglev code while %IsMaglevEnabled() is false (' + expectN + ' ' + who + 's looked at)', 'none of ' + expectN,
          seenMaglev.length ? seenMaglev.map(function (t) { return t.name + ' ' + t.status; }).join('; ') : 'none of ' + mine.length,
          seenMaglev.length === 0 && mine.length === expectN);
      }
    } else {
      // the proof branch: the configuration's tier for every function (maglev: whenever %IsMaglevEnabled() is true)
      Kit.check('tier lines found (' + who + 's)', expectN, mine.length, mine.length === expectN);
      var allOk = mine.length === expectN;
      mine.forEach(function (t) {
        var ok = t.status.indexOf(wantTier + '(') === 0;    // "turbofan+maybe-deopted(...)" does not count, as in f1probe.py
        allOk = allOk && ok;
        Kit.check('tier proof ' + who + ' ' + t.name, wantTier + '(...)', t.status, ok);
        if (wantTier === 'interpreter') {
          // jitless vs ignition: kLiteMode is set by --jitless only; the expectation comes from the configuration's flags
          var wantLite = hasFlag('--jitless'), num = parseInt((t.status.match(/\((\d+)\)$/) || [])[1], 10);
          var lite = !isNaN(num) && (num & LITE_MODE) !== 0;
          allOk = allOk && (lite === wantLite);
          Kit.check('lite-mode bit ' + who + ' ' + t.name + ' (set only by --jitless)', wantLite ? 'set' : 'clear', lite ? 'set' : 'clear', lite === wantLite);
        }
      });
      tierWord = allOk ? 'proven' : 'NOT PROVEN';
    }
    extra.tierProof = tierWord;
    extra.maglevEnabled = maglevOn;
    extra.tier = tierWord === 'proven' ? ('proven ' + wantTier + ' (' + who + 's, ' + mine.length + ' functions)')
      : tierWord === 'n/a' ? 'n/a (' + naWhy + ')' : 'NOT PROVEN';

    // ---- the probe's own checks (f1probe.js CHECK lines) ----
    chk.forEach(function (c) {
      if (c.length !== 6) { Kit.check('CHECK line parses (6 fields)', '6 fields', c.length + ' fields: ' + c.join(' | '), false); return; }
      var name = c[1], what = c[2], gotS = c[4].replace(/^got /, ''), wantS = c[5].replace(/^want /, '');
      var ok = c[3] === 'OK', wantShown = wantS;
      if (name === 'sloppy plain 2of3' && what === 'params') {
        // F1's own check carries the kit's one planted expectation: armed (?armed=1), Kit.want hands back a wrong value here, once
        wantShown = Kit.want(wantS, wantS + ' (PLANTED WRONG)');
        ok = ok && gotS === wantShown;               // unarmed: wantShown === wantS, and f1probe.js's own verdict (got === exp) decides
        extra.plantedAt = name + ' | ' + what;
      }
      Kit.check(name + ' | ' + what, wantShown, gotS, ok);
    });
  }
  return { run: run, extra: extra };
})();
