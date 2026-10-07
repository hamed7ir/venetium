r"""Venetium F1 probe runner (BATCH-REDO-1 §5 step 22; also BATCH-X86-1 §1.6 as the x86 control).

Usage:  python f1probe.py <path\to\d8.exe> [--json <out.json>] [--label <name>]

Runs venetium/tests/f1probe.js under d8 in 7 configurations and proves each configuration's tier with
%GetOptimizationStatus. d8 runs with its own folder as the working directory (snapshot_blob.bin, icudtl.dat).
Exit 0 only if every configuration ran, every check passed and every tier was proven; 1 otherwise; 2 on usage errors.
It also reports whether F1's own pattern was seen (`S3(1, 2)` from a sloppy plain call returning `[global],1,2`):
a pre-0011 Windows ARM32 d8 must show it (the armed run), a fixed one must not.
"""
import json, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
PROBE = os.path.join(HERE, "f1probe.js")
CONFIGS = [  # name, d8 flags, the tier every proven function must be in, which functions carry the proof
    ("jitless",         ["--jitless", "--allow-natives-syntax"],                                      "interpreter", "callee"),
    ("ignition",        ["--no-sparkplug", "--no-maglev", "--no-turbofan", "--allow-natives-syntax"], "interpreter", "callee"),
    ("sparkplug",       ["--always-sparkplug", "--no-maglev", "--no-turbofan", "--allow-natives-syntax"], "sparkplug", "callee"),
    ("maglev",          ["--allow-natives-syntax"],                                                   "maglev",      "callee"),
    ("turbofan",        ["--allow-natives-syntax"],                                                   "turbofan",    "callee"),
    ("maglev-caller",   ["--allow-natives-syntax", "--no-maglev-inlining"],                           "maglev",      "caller"),
    ("turbofan-caller", ["--allow-natives-syntax", "--no-turbo-inlining", "--no-maglev"],             "turbofan",    "caller"),
]
F1_PATTERN = "[global],1,2"


def main(argv):
    if len(argv) < 2 or not os.path.isfile(argv[1]):
        print(__doc__); return 2
    d8 = os.path.abspath(argv[1])
    out_json = argv[argv.index("--json") + 1] if "--json" in argv else None
    label = argv[argv.index("--label") + 1] if "--label" in argv else os.path.basename(os.path.dirname(d8))
    results, all_ok, f1_seen = [], True, False
    for name, flags, want_tier, who in CONFIGS:
        cmd = [d8] + flags + ["-e", "globalThis.F1_MODE='%s';" % name, PROBE]
        t0 = time.time()
        p = subprocess.run(cmd, cwd=os.path.dirname(d8), capture_output=True, text=True, timeout=900)
        wall = time.time() - t0
        lines = p.stdout.splitlines()
        chk = [l.split(" | ") for l in lines if l.startswith("CHECK ")]
        tiers = [l.split()[2:] for l in lines if l.startswith("TIER " + who + " ")]
        summ = [l for l in lines if l.startswith("SUMMARY ")]
        n = len(chk); nbad = sum(1 for c in chk if c[3] == "BAD")
        tier_ok = bool(tiers) and all(t[-1].startswith(want_tier + "(") for t in tiers)
        f1_here = any(c[1] == "sloppy plain 2of3" and c[2] == "params" and c[4] == "got " + F1_PATTERN for c in chk)
        f1_seen |= f1_here
        ok = p.returncode == 0 and n > 0 and nbad == 0 and tier_ok and bool(summ)
        all_ok &= ok
        bad_examples = [" | ".join(c[1:]) for c in chk if c[3] == "BAD"][:6]
        results.append(dict(config=name, flags=flags, rc=p.returncode, wall_s=round(wall, 2), checks=n, bad=nbad,
                            tier_wanted=want_tier, tier_of=who, tiers=[" ".join(t) for t in tiers], tier_proven=tier_ok,
                            f1_pattern_seen=f1_here, summary=summ[0] if summ else None, bad_examples=bad_examples,
                            stderr=p.stderr[-2000:], command=cmd))
        print(f"{name:16s} rc={p.returncode} checks={n:3d} bad={nbad:3d} tier({who}s)={'PROVEN ' + want_tier if tier_ok else 'NOT PROVEN ' + str(sorted(set(t[-1].split('(')[0] for t in tiers)))}"
              f"  F1 pattern {'SEEN' if f1_here else 'not seen'}  {wall:.1f}s")
        for b in bad_examples[:3]:
            print("      BAD " + b)
    total = sum(r["checks"] for r in results); total_bad = sum(r["bad"] for r in results)
    print(f"TOTAL [{label}] configurations {len(results)} | checks {total} | bad {total_bad} | all tiers proven "
          f"{all(r['tier_proven'] for r in results)} | F1 pattern seen {f1_seen} | {'PASS' if all_ok else 'FAIL'}")
    if out_json:
        json.dump(dict(d8=d8, label=label, probe=PROBE, results=results, total_checks=total, total_bad=total_bad,
                       f1_pattern_seen=f1_seen, passed=all_ok), open(out_json, "w"), indent=1)
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
