r"""BATCH-DEVICE-1 §1 step 7 — the kit matrix on x86 (RUNTIME): every configuration x {normal, armed}, each as one index.html run
through the runner (all 9 pages in iframes, the browser's default autoplay policy, fake camera/microphone for the webrtc page).
Works in slices so no background shell outlives the ~30-min harness limit: it starts a new run only while the slice has time left.
  python matrix.py <tests-dir> <label> [--slice-seconds 1320]     (tests-dir holds kit\ and runner\; runs -> F:\cr\device-1\runs\<label>-<config>[-armed])
State: F:\cr\device-1\matrix\<label>.json (runs already there are skipped). The table: matrix_table.py <label>."""
import json, os, subprocess, sys, time

NODE = r"F:\cr\src\third_party\node\win\node.exe"
RUNS = r"F:\cr\device-1\runs"
CONFIGS = ["default", "jitless", "ignition", "sparkplug", "maglev", "turbofan", "maglev-caller", "turbofan-caller", "liftoff-only", "no-liftoff"]
RUN_MAX = 900  # one index run must finish within 15 min (index.html's own limits sum to ~19 min worst case; typical 45 s here)


def main():
    tests, label = sys.argv[1], sys.argv[2]
    slice_s = int(sys.argv[sys.argv.index("--slice-seconds") + 1]) if "--slice-seconds" in sys.argv else 1320
    state_p = os.path.join(r"F:\cr\device-1\matrix", label + ".json")
    os.makedirs(os.path.dirname(state_p), exist_ok=True)
    state = json.load(open(state_p, encoding="utf-8")) if os.path.exists(state_p) else {}
    t0 = time.time()
    for c in CONFIGS:
        for armed in (False, True):
            key = c + ("-armed" if armed else "")
            if key in state:
                continue
            if time.time() - t0 > slice_s - 360:
                json.dump(state, open(state_p, "w", encoding="utf-8"), indent=1)
                print(f"slice ends: {len(state)}/20 done")
                return
            out = os.path.join(RUNS, f"{label}-{key}")
            cmd = [NODE, os.path.join(tests, "runner", "run.js"), "--config", c, "--index", "--out", out] + (["--armed"] if armed else [])
            t1 = time.time()
            try:
                p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=RUN_MAX)
                rc = p.returncode
            except subprocess.TimeoutExpired:
                rc = "timeout"
            state[key] = {"rc": rc, "secs": round(time.time() - t1), "out": out}
            json.dump(state, open(state_p, "w", encoding="utf-8"), indent=1)
            print(f"{key:24s} rc={rc} {state[key]['secs']}s")
    print(f"ALL DONE: {len(state)}/20")


if __name__ == "__main__":
    main()
