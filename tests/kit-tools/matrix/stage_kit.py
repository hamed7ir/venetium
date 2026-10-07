r"""BATCH-DEVICE-1 §1 — stage a copy of the kit + runner (venetium\tests\{kit,runner}) for a given build, with lib\build.js written
the way the packaging step writes it (from that build's own v8_build_config.json), so the x86 matrix runs the same build-record
path the device package will use.
  python stage_kit.py <dest-tests-dir> <out-dir> <v8_build_config.json>        (dest must not exist)"""
import hashlib, json, os, shutil, sys

SRC_TESTS = r"D:\repo\supermium-rt\venetium\tests"
CRSRC = r"F:\cr\src"


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def main():
    dest, out_dir, v8c = sys.argv[1:4]
    if os.path.exists(dest):
        raise SystemExit(f"REFUSING: {dest} exists")
    for sub in ("kit", "runner"):
        shutil.copytree(os.path.join(SRC_TESTS, sub), os.path.join(dest, sub))
    vc = json.load(open(v8c, encoding="utf-8"))
    facts = {k: vc.get(k) for k in ("v8_current_cpu", "v8_target_cpu", "has_maglev", "has_turbofan", "has_webassembly", "simulator_run")}
    facts["source"] = os.path.relpath(v8c, CRSRC).replace(os.sep, "\\")
    facts["venetium_exe_sha256"] = sha(os.path.join(out_dir, "venetium.exe"))
    text = ("// Venetium test kit - the build this kit copy is for. Written by the packaging step (BATCH-DEVICE-1 section 2,\n"
            "// F:\\cr\\device-1\\pkg\\portable.py) from that build's own v8_build_config.json; the record's copy says null.\n"
            "window.KIT_BUILD = " + json.dumps(facts, sort_keys=True) + ";\n")
    open(os.path.join(dest, "kit", "lib", "build.js"), "w", encoding="utf-8", newline="\n").write(text)
    print("staged", dest, "build.js =", facts)


if __name__ == "__main__":
    main()
