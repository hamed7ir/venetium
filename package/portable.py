r"""BATCH-DEVICE-1 §2 — the portable (run-from-folder, D16) file set from Chromium's own ship list, and the zips.

Ship list: chrome/installer/mini_installer/chrome.release, evaluated the way create_installer_archive.py does for this build's GN args
(GENERAL always; the distribution section only for a branded build; HIDPI if enable_hidpi; SNAPSHOTBLOB if !use_v8_context_snapshot
or include_both_v8_snapshots; DXC if dawn_use_built_dxc; FFMPEG if is_component_ffmpeg and not a component build). Cross-check:
chrome/tools/build/win/FILES.cfg's "default" filegroup. Layout: FLAT, the paths the files have in the build folder (the layout every
Venetium run so far used); the installer's %(VersionDir)s split is not used (chrome.exe loads chrome.dll from its own folder when no
<version>\ folder exists). The product exe is venetium.exe (Venetium's name for chrome.exe).

  python portable.py set   <out-dir> <arch-label> <report.tsv>          -> the resolved set + findings (no zip)
  python portable.py zip   <out-dir> <arch-label> <zip> [--kit <dir>]   -> the zip (root folder = zip name without .zip)
Exit 3 if a SwiftShader file would be packaged for arch 'arm32' (the 0021/0025 SwiftShader-off gate)."""
import configparser, fnmatch, glob, hashlib, json, os, re, subprocess, sys, zipfile

SRC = r"F:\cr\src"
RELEASE = os.path.join(SRC, r"chrome\installer\mini_installer\chrome.release")
FILESCFG = os.path.join(SRC, r"chrome\tools\build\win\FILES.cfg")
GN = os.path.join(SRC, r"buildtools\win\gn.exe")
READOBJ = r"F:\cr\rt21-cr\bin\llvm-readobj.exe"
EXE_RENAME = {"chrome.exe": "venetium.exe"}
ARGS = ["is_chrome_branded", "enable_hidpi", "use_v8_context_snapshot", "include_both_v8_snapshots", "dawn_use_built_dxc",
        "is_component_ffmpeg", "is_component_build", "enable_swiftshader"]


def gn_args(out_dir):
    env = dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="0", vs2022_install=r"D:\Program Files\vs22buildtools",
               GYP_MSVS_VERSION="2022", WINDOWSSDKDIR="D:\\Windows Kits\\10\\")
    r = subprocess.run([GN, "args", out_dir, "--list", "--short", "--json"], cwd=SRC, capture_output=True, text=True, env=env)
    vals = {}
    for e in json.loads(r.stdout):
        if e["name"] in ARGS:
            v = (e.get("current") or e.get("default"))["value"]
            vals[e["name"]] = (v == "true")
    return vals


def sections_for(a):
    s = ["GENERAL"]
    if a["is_chrome_branded"]:
        s.append("GOOGLE_CHROME")
    if a["enable_hidpi"]:
        s.append("HIDPI")
    if not a["use_v8_context_snapshot"] or a["include_both_v8_snapshots"]:
        s.append("SNAPSHOTBLOB")
    if a["dawn_use_built_dxc"]:
        s.append("DXC")
    if not a["is_component_build"] and a["is_component_ffmpeg"]:
        s.append("FFMPEG")
    return s


def read_release():
    cp = configparser.RawConfigParser(allow_no_value=True, delimiters=(":",))
    cp.optionxform = str  # keep case (the installer lower-cases and globs case-insensitively)
    cp.read(RELEASE, encoding="utf-8")
    return cp


def resolve(out_dir, arch):
    a = gn_args(out_dir)
    use = sections_for(a)
    cp = read_release()
    files, findings = {}, []
    for sec in cp.sections():
        for opt in cp.options(sec):
            if opt.endswith("dir"):
                continue
            pat = opt.replace("\\", os.sep)
            name = EXE_RENAME.get(pat, pat)
            hits = sorted(glob.glob(os.path.join(out_dir, name)))
            if sec not in use:
                if hits:
                    findings.append(("not-shipped", sec, opt, f"{len(hits)} file(s) built but section {sec} is off for this build"))
                continue
            if name != pat:
                findings.append(("renamed", sec, opt, f"ships as {name} (Venetium's product exe)"))
            if not hits:
                findings.append(("absent", sec, opt, "listed for this build, no file in the build folder"))
            for h in hits:
                rel = os.path.relpath(h, out_dir).replace(os.sep, "/")
                files.setdefault(rel, (sec, opt))
    # cross-check FILES.cfg's default filegroup
    ns = {}
    exec(open(FILESCFG, encoding="utf-8").read(), ns)
    for e in ns["FILES"]:
        if "default" in e.get("filegroup", []):
            pat = EXE_RENAME.get(e["filename"], e["filename"])
            if not any(fnmatch.fnmatch(r, pat) for r in files) and glob.glob(os.path.join(out_dir, pat)):
                findings.append(("FILES.cfg-only", "FILES.cfg", e["filename"], "in FILES.cfg default group and built, not in chrome.release's sections for this build"))
    return a, use, files, findings


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            h.update(b)
    return h.hexdigest()


def machine(p):
    if not p.lower().endswith((".exe", ".dll")):
        return "-"
    t = subprocess.run([READOBJ, "--file-headers", p], capture_output=True, text=True).stdout
    m = re.search(r"Machine: IMAGE_FILE_MACHINE_(\w+)", t)
    return m.group(1) if m else "?"


def main():
    mode, out_dir, arch = sys.argv[1], sys.argv[2], sys.argv[3]
    a, use, files, findings = resolve(out_dir, arch)
    swift = [r for r in files if "swiftshader" in r.lower()]
    if arch == "arm32" and swift:
        print("GATE FAILED: SwiftShader would be packaged for arm32:", swift)
        sys.exit(3)
    rows = []
    for rel in sorted(files, key=str.lower):
        p = os.path.join(out_dir, rel)
        rows.append((rel, sha(p), os.path.getsize(p), machine(p), files[rel][0], files[rel][1]))
    if mode == "set":
        rep = sys.argv[4]
        with open(rep, "w", encoding="utf-8", newline="\n") as f:
            f.write(f"# out={out_dir} arch={arch} gn={json.dumps(a, sort_keys=True)} sections={','.join(use)}\n")
            f.write("path\tsha256\tsize\tmachine\tsection\tship-list entry\n")
            for r in rows:
                f.write("\t".join(map(str, r)) + "\n")
            f.write("# findings\n")
            for fd in findings:
                f.write("# " + "\t".join(fd) + "\n")
        print(f"{arch}: {len(rows)} files, {sum(r[2] for r in rows) / 1e6:.1f} MB; sections {use}; findings {len(findings)}")
        for fd in findings:
            print("  finding:", " | ".join(fd))
        print("  swiftshader in set:", swift or "none")
        return
    if mode == "zip":
        zpath = sys.argv[4]
        kit = sys.argv[sys.argv.index("--kit") + 1] if "--kit" in sys.argv else None
        root = os.path.splitext(os.path.basename(zpath))[0]
        if os.path.exists(zpath):
            raise SystemExit(f"REFUSING: {zpath} exists")
        kit_files, build_js = [], None
        if kit:
            # the kit goes in under <root>\kit\ (launchers find ..\venetium.exe); every kit file must match kit\MANIFEST.tsv,
            # except lib\build.js, which is written here from the build's own v8_build_config.json (--v8-config)
            v8c = sys.argv[sys.argv.index("--v8-config") + 1]
            vc = json.load(open(v8c, encoding="utf-8"))
            facts = {k: vc.get(k) for k in ("v8_current_cpu", "v8_target_cpu", "has_maglev", "has_turbofan", "has_webassembly", "simulator_run")}
            facts["source"] = os.path.relpath(v8c, SRC).replace(os.sep, "\\")
            facts["venetium_exe_sha256"] = sha(os.path.join(out_dir, "venetium.exe"))
            build_js = ("// Venetium test kit - the build this kit copy is for. Written by the packaging step (BATCH-DEVICE-1 section 2,\n"
                        "// F:\\cr\\device-1\\pkg\\portable.py) from that build's own v8_build_config.json; the record's copy says null.\n"
                        "window.KIT_BUILD = " + json.dumps(facts, sort_keys=True) + ";\n").encode("utf-8")
            man = {}
            for line in open(os.path.join(kit, "MANIFEST.tsv"), encoding="utf-8").read().splitlines()[1:]:
                if line.strip():
                    p, h = line.split("\t")[:2]
                    man[p.replace("\\", "/")] = h
            bad = []
            for d, _, fs in os.walk(kit):
                for fn in sorted(fs):
                    p = os.path.join(d, fn)
                    rel = os.path.relpath(p, kit).replace(os.sep, "/")
                    kit_files.append((p, rel))
                    if rel in ("lib/build.js", "MANIFEST.tsv") or rel.startswith("manifest/"):
                        continue
                    if man.get(rel) != sha(p):
                        bad.append(rel)
            if bad:
                raise SystemExit(f"REFUSING: {len(bad)} kit files do not match kit\\MANIFEST.tsv: {bad[:10]}")
            print(f"kit: {len(kit_files)} files, all match MANIFEST.tsv; build.js = {facts}")
        n = 0
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for r in rows:
                z.write(os.path.join(out_dir, r[0]), f"{root}/{r[0]}")
                n += 1
            for p, rel in kit_files:
                if rel == "lib/build.js":
                    z.writestr(f"{root}/kit/{rel}", build_js)
                else:
                    z.write(p, f"{root}/kit/{rel}")
                n += 1
        print(f"{zpath}: {n} entries, {os.path.getsize(zpath):,} B, sha256 {sha(zpath)}")


if __name__ == "__main__":
    main()
