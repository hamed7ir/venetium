#!/usr/bin/env python3
r"""mdump.py - minimal Windows minidump reader for the Venetium device crash triage
(BATCH-ISSUES-1 Part B: issue #4 STATUS_DATATYPE_MISALIGNMENT, issue #2 Telegram WASM).

No external modules; pure struct parsing of the MINIDUMP format. Runs on the build
server over a .dmp the device produced - the device itself does not need Python.

What it prints:
  - the exception: code (named), flags, faulting address, parameters
  - the faulting thread's register context (ARM32: r0-r12, sp, lr, pc, cpsr; x86/x64 too)
  - the loaded module that the faulting address / PC land in, as module + RVA
    (that RVA is what you feed to the map / llvm-symbolizer to name the frame -
     for #4 it tells you whether the unaligned access is in chrome.dll compiled
     code, in V8's JIT region (no owning module => anonymous RWX), or a system dll)
  - the raw bytes at PC if the dump carried that memory (so the faulting ARM
    instruction can be disassembled: LDRD/STRD/VLDR/VSTR are the usual unaligned ones)

Usage:
  python -I mdump.py <crash.dmp>
  python -I mdump.py <crash.dmp> --modules      # also list every module
"""
import struct, sys

MDMP = 0x504d444d
STREAM = {3: "ThreadList", 4: "ModuleList", 5: "MemoryList", 6: "Exception",
          7: "SystemInfo", 9: "Memory64List", 15: "MemoryInfoList"}
EXC = {
    0xC0000005: "ACCESS_VIOLATION",
    0x80000002: "DATATYPE_MISALIGNMENT",   # issue #4 - unaligned load/store
    0xC000001D: "ILLEGAL_INSTRUCTION",
    0xC0000096: "PRIVILEGED_INSTRUCTION",
    0xC00000FD: "STACK_OVERFLOW",
    0xC0000025: "NONCONTINUABLE_EXCEPTION",
    0xC0000094: "INTEGER_DIVIDE_BY_ZERO",
    0x80000003: "BREAKPOINT",
    0xC0000409: "STACK_BUFFER_OVERRUN",      # __fastfail / CFG
    0xE0000001: "V8 OOM/abort (sometimes)",
}
# processor arch in MINIDUMP_SYSTEM_INFO
ARCH = {0: "x86", 5: "ARM", 9: "x64", 12: "ARM64"}


def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def u64(b, o): return struct.unpack_from("<Q", b, o)[0]


def mdstring(b, rva):
    n = u32(b, rva)
    return b[rva + 4: rva + 4 + n].decode("utf-16-le", "replace")


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    path = sys.argv[1]
    want_modules = "--modules" in sys.argv[2:]
    b = open(path, "rb").read()
    if u32(b, 0) != MDMP:
        print("not a minidump (bad signature):", path); sys.exit(2)
    nstreams = u32(b, 8)
    dir_rva = u32(b, 12)
    dirs = {}
    for i in range(nstreams):
        o = dir_rva + i * 12
        st, size, rva = u32(b, o), u32(b, o + 4), u32(b, o + 8)
        dirs.setdefault(st, (size, rva))
    print("== minidump:", path, "==")
    print("streams:", ", ".join(sorted(STREAM.get(s, str(s)) for s in dirs)))

    arch = None
    if 7 in dirs:  # SystemInfo
        _, rva = dirs[7]
        parch = struct.unpack_from("<H", b, rva)[0]
        arch = ARCH.get(parch, "arch%d" % parch)
        major = u32(b, rva + 8); minor = u32(b, rva + 12); build = u32(b, rva + 16)
        print("system: %s  Windows %d.%d build %d" % (arch, major, minor, build))

    # modules
    modules = []
    if 4 in dirs:
        _, rva = dirs[4]
        nmod = u32(b, rva)
        p = rva + 4
        for i in range(nmod):
            base = u64(b, p); size = u32(b, p + 8); name_rva = u32(b, p + 20)
            name = mdstring(b, name_rva)
            modules.append((base, size, name))
            p += 108  # sizeof(MINIDUMP_MODULE)
    modules.sort()

    def owner(addr):
        for base, size, name in modules:
            if base <= addr < base + size:
                return name.split("\\")[-1], addr - base
        return None, None

    if want_modules:
        print("\n== modules (%d) ==" % len(modules))
        for base, size, name in modules:
            print("  %016x +%08x  %s" % (base, size, name.split("\\")[-1]))

    # exception
    if 6 not in dirs:
        print("\n(no Exception stream - this dump was not crash-triggered)")
        return
    _, rva = dirs[6]
    thread_id = u32(b, rva)
    er = rva + 8  # MINIDUMP_EXCEPTION
    code = u32(b, er); flags = u32(b, er + 4)
    exc_addr = u64(b, er + 16)
    nparams = u32(b, er + 24)
    params = [u64(b, er + 32 + 8 * i) for i in range(min(nparams, 15))]
    ctx_size = u32(b, rva + 8 + 152)      # ThreadContext LocationDescriptor
    ctx_rva = u32(b, rva + 8 + 156)
    mod, off = owner(exc_addr)
    print("\n== exception ==")
    print("  code   : 0x%08X  %s" % (code, EXC.get(code, "?")))
    print("  flags  : 0x%08X %s" % (flags, "(noncontinuable)" if flags & 1 else ""))
    print("  thread : 0x%x" % thread_id)
    print("  address: 0x%016X  %s" % (exc_addr, ("%s+0x%x" % (mod, off)) if mod else "<no owning module>"))
    if nparams >= 2 and code in (0xC0000005, 0x80000002):
        rw = {0: "read", 1: "write", 8: "DEP/execute"}.get(params[0], "op%d" % params[0])
        print("  detail : %s at 0x%016X" % (rw, params[1]))

    # faulting thread context
    if ctx_size and ctx_rva:
        c = ctx_rva
        cflags = u32(b, c)
        print("\n== faulting context (%s) ==" % (arch or "?"))
        if arch == "ARM" or (cflags & 0x40000000):   # CONTEXT_ARM
            regs = struct.unpack_from("<17I", b, c + 4)  # R0..R12, Sp, Lr, Pc, Cpsr
            names = ["r%d" % i for i in range(13)] + ["sp", "lr", "pc", "cpsr"]
            for i in range(0, 17, 4):
                print("  " + "  ".join("%-4s=%08x" % (names[j], regs[j])
                                        for j in range(i, min(i + 4, 17))))
            pc = regs[15]; lr = regs[14]
            for label, a in (("pc", pc), ("lr", lr)):
                m, o = owner(a)
                print("  %s 0x%08x -> %s" % (label, a, ("%s+0x%x" % (m, o)) if m else "<no module (JIT/anon?)>"))
        elif arch == "x64":
            # CONTEXT_AMD64: Rip at offset 0xF8 from context start
            rip = u64(b, c + 0xF8)
            m, o = owner(rip)
            print("  rip 0x%016x -> %s" % (rip, ("%s+0x%x" % (m, o)) if m else "<no module>"))
        elif arch == "x86":
            eip = u32(b, c + 0xB8)  # CONTEXT_x86 Eip
            m, o = owner(eip)
            print("  eip 0x%08x -> %s" % (eip, ("%s+0x%x" % (m, o)) if m else "<no module>"))

    print("\nNext: feed the module+RVA to llvm-symbolizer against the matching .pdb,")
    print("or note '<no module>' for pc  => the fault is in a JIT/anonymous region")
    print("(V8 or regexp JIT) rather than AOT-compiled code.")


if __name__ == "__main__":
    main()
