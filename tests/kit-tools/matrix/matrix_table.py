r"""BATCH-DEVICE-1 §1 step 7 — the matrix table from the run folders F:\cr\device-1\runs\<label>-<config>[-armed]\index.json
(the runner's record: {result: window.__kitIndexResult, crashes, ...}). Gate: every normal run PASS (every page PASS); every armed
run's index verdict PASS (= every page FAIL with planted=true) and, stricter, every armed page has exactly ONE failed check.
  python matrix_table.py <label> [<out.md>]        exit 0 if the gate holds, 6 if not"""
import json, os, sys

RUNS = r"F:\cr\device-1\runs"
CONFIGS = ["default", "jitless", "ignition", "sparkplug", "maglev", "turbofan", "maglev-caller", "turbofan-caller", "liftoff-only", "no-liftoff"]
PAGES = ["js-f1", "js-jit", "wasm", "images", "media", "canvas-webgl", "webrtc", "storage", "fonts"]


def load(label, key):
    p = os.path.join(RUNS, f"{label}-{key}", "index.json")
    if not os.path.exists(p):
        return None
    return json.load(open(p, encoding="utf-8"))


def main():
    label = sys.argv[1]
    out_md = sys.argv[2] if len(sys.argv) > 2 else None
    lines, bad = [], []
    head = "| configuration | run | index | " + " | ".join(PAGES) + " | tier proof (js-f1 / js-jit / wasm) | time |"
    lines += [head, "|" + "---|" * (len(PAGES) + 5)]
    for c in CONFIGS:
        for armed in (False, True):
            key = c + ("-armed" if armed else "")
            ix = load(label, key)
            if ix is None:
                bad.append(f"{key}: no run"); lines.append(f"| {c} | {'armed' if armed else 'normal'} | (not run) |" + " |" * (len(PAGES) + 2)); continue
            r = ix.get("result") or {}
            pages = {p["page"]: p for p in r.get("pages", [])}
            full = {x["page"]: (x.get("result") or {}) for x in (r.get("results") or [])}
            cells = []
            for pg in PAGES:
                p = pages.get(pg)
                if not p:
                    cells.append("—"); bad.append(f"{key}: {pg} missing"); continue
                nbad = len(p.get("failed") or [])
                cells.append(f"{p['verdict']} {p['checks']}/{nbad}" + (" P" if armed and p.get("planted") else ""))
                if not armed and p["verdict"] != "PASS":
                    bad.append(f"{key}: {pg} {p['verdict']} failed={p.get('failed')}")
                if armed and not (p["verdict"] == "FAIL" and p.get("planted") and nbad == 1):
                    bad.append(f"{key}: {pg} armed verdict {p['verdict']} planted {p.get('planted')} bad {nbad}: {p.get('failed')}")
            if not armed and r.get("verdict") != "PASS":
                bad.append(f"{key}: index verdict {r.get('verdict')}")
            if armed and r.get("verdict") != "PASS":
                bad.append(f"{key}: armed index verdict {r.get('verdict')} (must be PASS = every page failed as planted)")
            crashes = len(ix.get("crashes") or [])
            if crashes:
                bad.append(f"{key}: {crashes} crash(es)")
            tiers = " / ".join(str(full.get(pg, {}).get("tier", "-")) for pg in ("js-f1", "js-jit", "wasm"))
            lines.append(f"| {c} | {'armed' if armed else 'normal'} | {r.get('verdict')} | " + " | ".join(cells) + f" | {tiers} | {round((r.get('ms') or 0) / 1000)} s |")
    lines.append("")
    lines.append("Cells: verdict checks/failed (P = planted). Gate: normal = every page PASS; armed = every page FAIL, planted, exactly 1 failed check.")
    lines.append("GATE: " + ("PASS" if not bad else "FAIL — " + "; ".join(bad)))
    text = "\n".join(lines) + "\n"
    print(text)
    if out_md:
        open(out_md, "w", encoding="utf-8", newline="\n").write(text)
    sys.exit(0 if not bad else 6)


if __name__ == "__main__":
    main()
