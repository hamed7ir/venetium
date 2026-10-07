r"""BATCH-DEVICE-1 §1 step 6 — merge the per-page manifests (kit\manifest\<page>.tsv) into kit\MANIFEST.tsv.
Rows: every per-page row (must match the file on disk) + the shared files no page owns (sha256 now, source 'shared, hand-written
(BATCH-X86-2 / DEVICE-1)'). Every file under the kit except MANIFEST.tsv itself must appear exactly once; a page manifest without the
header row, a row outside the kit, a stale sha256, an unowned file or a duplicate fails the merge (exit 4) and nothing is written.
  python merge_manifest.py [--check]       (--check: verify an existing MANIFEST.tsv instead of writing it)"""
import hashlib, os, sys

KIT = r"D:\repo\supermium-rt\venetium\tests\kit"
HEADER = "path\tsha256\tsource"


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def main():
    problems, rows, owner = [], {}, {}
    mdir = os.path.join(KIT, "manifest")
    for fn in sorted(os.listdir(mdir)):
        if not fn.endswith(".tsv"):
            continue
        lines = open(os.path.join(mdir, fn), encoding="utf-8").read().splitlines()
        if not lines or lines[0] != HEADER:
            problems.append(f"{fn}: first line is not the header row")
            continue
        for ln in lines[1:]:
            if not ln.strip():
                continue
            parts = ln.split("\t")
            if len(parts) < 3:
                problems.append(f"{fn}: row with fewer than 3 fields: {ln[:80]}")
                continue
            path, h, src = parts[0].replace("\\", "/"), parts[1], "\t".join(parts[2:])
            full = os.path.normpath(os.path.join(KIT, path))
            if path.startswith(("/", "..")) or ":" in path or not full.startswith(KIT):
                problems.append(f"{fn}: row outside the kit: {path}")
                continue
            if not os.path.isfile(full):
                problems.append(f"{fn}: no such file: {path}")
                continue
            if sha(full) != h:
                problems.append(f"{fn}: stale sha256 for {path}")
            if path in rows:
                problems.append(f"{fn}: {path} already listed by {owner[path]}")
            rows[path], owner[path] = (h, src), fn
    # the shared files no page owns
    for d, _, fs in os.walk(KIT):
        for f in fs:
            rel = os.path.relpath(os.path.join(d, f), KIT).replace(os.sep, "/")
            if rel == "MANIFEST.tsv" or rel in rows:
                continue
            shared = rel in ("index.html", "README.md") or rel.startswith(("lib/kit.js", "lib/configs.js", "lib/build.js", "launchers/", "manifest/"))
            if shared:
                rows[rel] = (sha(os.path.join(d, f)), "shared, hand-written (BATCH-X86-2 / BATCH-DEVICE-1); lib/build.js is rewritten per package"
                             if rel == "lib/build.js" else "shared, hand-written (BATCH-X86-2 / BATCH-DEVICE-1)")
                owner[rel] = "(shared)"
            else:
                problems.append(f"unowned file (no page manifest lists it, not a shared file): {rel}")
    if problems:
        print("MERGE FAILED:"); [print("  " + p) for p in problems]; sys.exit(4)
    text = HEADER + "\n" + "".join(f"{p}\t{h}\t{s}\n" for p, (h, s) in sorted(rows.items()))
    target = os.path.join(KIT, "MANIFEST.tsv")
    if "--check" in sys.argv:
        ok = os.path.exists(target) and open(target, encoding="utf-8").read() == text
        print("MANIFEST.tsv", "matches" if ok else "DIFFERS from", "the merge of the page manifests + shared files")
        sys.exit(0 if ok else 5)
    open(target, "w", encoding="utf-8", newline="\n").write(text)
    n_shared = sum(1 for p in owner.values() if p == "(shared)")
    print(f"MANIFEST.tsv: {len(rows)} files ({len(rows) - n_shared} from {len(set(owner.values()) - {'(shared)'})} page manifests, {n_shared} shared); sha256 {sha(target)}")


if __name__ == "__main__":
    main()
