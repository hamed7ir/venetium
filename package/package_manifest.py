r"""BATCH-DEVICE-1 §2 step 13 — PACKAGE-MANIFEST.tsv (zip · path · sha256 · arch · size) for both portable zips, each zip's .sha256,
and the copies in C:\handoff\ (refuses to overwrite a different file; an identical copy already there is left as it is). arch: x86 /
arm32 for build files, 'kit' for the test kit inside the ARM zip.
  python package_manifest.py <x86.zip> <arm32.zip> [--out <tsv>] [--label <text>]
(DEVICE-1 follow-up rtdiv: --out/--label so a re-packaged ARM zip gets its own manifest; the defaults are the DEVICE-1 values.)"""
import hashlib, os, shutil, sys, zipfile

OUT = r"F:\cr\device-1\PACKAGE-MANIFEST.tsv"
HANDOFF = r"C:\handoff"


def sha_file(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            h.update(b)
    return h.hexdigest()


def main():
    rows = []
    sums = {}
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else OUT
    label = sys.argv[sys.argv.index("--label") + 1] if "--label" in sys.argv else "BATCH-DEVICE-1 portable packages"
    for zp, arch in ((sys.argv[1], "x86"), (sys.argv[2], "arm32")):
        z = zipfile.ZipFile(zp)
        root = os.path.splitext(os.path.basename(zp))[0] + "/"
        for n in sorted(z.namelist(), key=str.lower):
            if n.endswith("/"):
                continue
            rel = n[len(root):]
            a = "kit" if rel.startswith("kit/") else arch
            rows.append((os.path.basename(zp), rel, hashlib.sha256(z.read(n)).hexdigest(), a, z.getinfo(n).file_size))
        sums[zp] = sha_file(zp)
        line = f"{sums[zp]}  {os.path.basename(zp)}\n"
        if not os.path.exists(zp + ".sha256") or open(zp + ".sha256", encoding="ascii").read() != line:
            with open(zp + ".sha256", "w", encoding="ascii", newline="\n") as f:
                f.write(line)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# {label}; zip sha256: " + "; ".join(f"{os.path.basename(k)} {v}" for k, v in sums.items()) + "\n")
        f.write("zip\tpath\tsha256\tarch\tsize\n")
        for r in rows:
            f.write("\t".join(map(str, r)) + "\n")
    print(f"{out}: {len(rows)} rows")
    for zp, h in sums.items():
        for src in (zp, zp + ".sha256"):
            dst = os.path.join(HANDOFF, os.path.basename(src))
            if os.path.exists(dst):
                if sha_file(dst) != sha_file(src):
                    raise SystemExit(f"REFUSING to overwrite {dst}")
                print(f"{dst}: already there, identical")
                continue
            shutil.copy2(src, dst)
        print(f"{os.path.basename(zp)}: {os.path.getsize(zp):,} B sha256 {h} -> {HANDOFF}; copy sha256 {sha_file(os.path.join(HANDOFF, os.path.basename(zp)))}")


if __name__ == "__main__":
    main()
