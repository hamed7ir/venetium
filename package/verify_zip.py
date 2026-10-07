r"""BATCH-DEVICE-1 §2 step 12 (STRUCTURAL) — verify the ARM32 portable zip by reading the zip itself (independent of portable.py):
  1. every file of the resolved arm32 set (set-arm32.tsv) is in the zip with the same sha256; nothing else outside kit/
  2. every PE (.exe/.dll) in the zip is IMAGE_FILE_MACHINE_ARMNT (llvm-readobj on an extracted copy)
  3. no file whose name contains 'swiftshader' anywhere in the zip (the 0021/0025 SwiftShader-off gate)
  4. the non-kit file list equals the x86 zip's list one-for-one except the x86-only SwiftShader files
  5. kit/: every file except lib/build.js matches kit/MANIFEST.tsv (as zipped); lib/build.js names arm + has_maglev true and the
     sha256 of the zipped venetium.exe
  6. sizes
  python verify_zip.py <arm32.zip> <x86.zip> <set-arm32.tsv> <extract-dir>     exit 0 = all pass, 7 = any failure"""
import hashlib, json, os, re, subprocess, sys, zipfile

READOBJ = r"F:\cr\rt21-cr\bin\llvm-readobj.exe"


def main():
    zarm, zx86, setp, ext = sys.argv[1:5]
    fails = []
    za, zx = zipfile.ZipFile(zarm), zipfile.ZipFile(zx86)
    ra = os.path.splitext(os.path.basename(zarm))[0] + "/"
    rx = os.path.splitext(os.path.basename(zx86))[0] + "/"
    names_a = [n for n in za.namelist() if not n.endswith("/")]
    names_x = [n for n in zx.namelist() if not n.endswith("/")]
    build_a = {n[len(ra):]: n for n in names_a if n.startswith(ra) and not n[len(ra):].startswith("kit/")}
    kit_a = {n[len(ra) + 4:]: n for n in names_a if n.startswith(ra + "kit/")}
    other = [n for n in names_a if not n.startswith(ra)]
    if other:
        fails.append(f"entries outside the root folder: {other[:5]}")
    # 1. the set
    want = {}
    for line in open(setp, encoding="utf-8"):
        if line.startswith(("#", "path\t")) or not line.strip():
            continue
        p, h = line.split("\t")[:2]
        want[p] = h
    for p, h in want.items():
        if p not in build_a:
            fails.append(f"set file missing from zip: {p}")
        elif hashlib.sha256(za.read(build_a[p])).hexdigest() != h:
            fails.append(f"sha256 differs from the set: {p}")
    extra = sorted(set(build_a) - set(want))
    if extra:
        fails.append(f"files in the zip that are not in the set: {extra[:10]}")
    print(f"1. set: {len(want)} files expected, {len(build_a)} build files in the zip, {len(kit_a)} kit files")
    # 2. PE machine types
    os.makedirs(ext, exist_ok=True)
    pes = [p for p in build_a if p.lower().endswith((".exe", ".dll"))]
    machines = {}
    for p in pes:
        dst = os.path.join(ext, p.replace("/", os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, "wb").write(za.read(build_a[p]))
        t = subprocess.run([READOBJ, "--file-headers", dst], capture_output=True, text=True).stdout
        m = re.search(r"Machine: IMAGE_FILE_MACHINE_(\w+)", t)
        machines[p] = m.group(1) if m else "?"
    not_arm = {p: m for p, m in machines.items() if m != "ARMNT"}
    if not_arm:
        fails.append(f"PEs that are not ARMNT: {not_arm}")
    print(f"2. PEs: {len(pes)}, machine types {sorted(set(machines.values()))}")
    # 3. SwiftShader
    sw = [n for n in names_a if "swiftshader" in n.lower()]
    if sw:
        fails.append(f"SwiftShader in the ARM package: {sw}")
    print(f"3. swiftshader entries in the ARM zip: {len(sw)}")
    # 4. x86 list comparison
    build_x = {n[len(rx):] for n in names_x if n.startswith(rx)}
    only_x, only_a = sorted(build_x - set(build_a)), sorted(set(build_a) - build_x)
    if [p for p in only_x if "swiftshader" not in p.lower()] or only_a:
        fails.append(f"list differs beyond SwiftShader: x86-only {only_x}, arm-only {only_a}")
    print(f"4. vs x86 zip: x86-only {only_x}; arm-only {only_a}")
    # 5. the kit
    man = {}
    mtext = za.read(kit_a["MANIFEST.tsv"]).decode("utf-8").splitlines()
    for line in mtext[1:]:
        if line.strip():
            p, h = line.split("\t")[:2]
            man[p] = h
    kit_bad = [p for p in kit_a if p not in ("MANIFEST.tsv", "lib/build.js") and not p.startswith("manifest/")
               and man.get(p) != hashlib.sha256(za.read(kit_a[p])).hexdigest()]
    if kit_bad:
        fails.append(f"kit files not matching MANIFEST.tsv: {kit_bad[:10]}")
    unlisted = [p for p in man if p not in kit_a]
    if unlisted:
        fails.append(f"MANIFEST rows with no file in the zip: {unlisted[:10]}")
    bj = za.read(kit_a["lib/build.js"]).decode("utf-8")
    m = re.search(r"window\.KIT_BUILD = (\{.*\});", bj)
    facts = json.loads(m.group(1)) if m else {}
    exe_h = hashlib.sha256(za.read(build_a["venetium.exe"])).hexdigest()
    if not (facts.get("v8_current_cpu") == "arm" and facts.get("has_maglev") is True and facts.get("venetium_exe_sha256") == exe_h):
        fails.append(f"lib/build.js does not describe this ARM build: {facts}")
    for need in ("README.md", "index.html", "launchers/default.cmd", "launchers/maglev.cmd"):
        if need not in kit_a:
            fails.append(f"kit file missing: {need}")
    print(f"5. kit: {len(kit_a)} files, {len(man)} MANIFEST rows, mismatches {len(kit_bad)}; build.js {facts.get('v8_current_cpu')} has_maglev {facts.get('has_maglev')} exe {str(facts.get('venetium_exe_sha256'))[:12]}")
    # 6. sizes
    usz = sum(za.getinfo(n).file_size for n in names_a)
    print(f"6. sizes: zip {os.path.getsize(zarm):,} B, unpacked {usz:,} B (build {sum(za.getinfo(build_a[p]).file_size for p in build_a):,} B, kit {sum(za.getinfo(kit_a[p]).file_size for p in kit_a):,} B)")
    print("RESULT:", "PASS" if not fails else "FAIL")
    for f in fails:
        print("  FAIL:", f)
    sys.exit(0 if not fails else 7)


if __name__ == "__main__":
    main()
