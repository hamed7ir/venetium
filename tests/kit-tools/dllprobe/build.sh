#!/usr/bin/env bash
# BATCH-SBX-1 dllprobe: build ARM32 (the device), x64 and x86 (RUNTIME checks on this server) with rt2.1 clang-cl + lld-link,
# static CRT (/MT), MSVC 14.44.35207; ARM32 links the 19041 ARM import libs (D9) and our clang_rt.builtins-arm.lib (__rt_* shim)
# FIRST, as Chromium's links do - and with /GUARD:CF (dllprobe has no delay-load imports, so the ARM32 CFG defect that forced
# 0031's CFG-off on chrome.dll does not bite it, exactly as sbxprobe/dbgrun). Mirrors sbxprobe/build.sh.
# Also builds a tiny x64 stand-in chrome.dll (+ dllprobe_dep.dll) so the server control can validate the load/snaps mechanism.
# Output: F:/cr/sbx-1/bin/<arch>/dllprobe.exe ; F:/cr/sbx-1/bin/x64/control/{chrome.dll,dllprobe_dep.dll}
set -u
export MSYS2_ARG_CONV_EXCL='*'
T=F:/cr/rt21-cr/bin
S=F:/cr/sbx-1/src
B=F:/cr/sbx-1/bin
VC="D:/Program Files/vs22buildtools/VC/Tools/MSVC/14.44.35207"
SDK="D:/Windows Kits/10"
PIN="-vctoolsdir \"$VC\" -winsdkdir \"$SDK\" -winsdkversion 10.0.26100.0"
CFLAGS="/nologo /O2 /MT /W4 /WX- /GS /guard:cf /EHsc- /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE"
build() {   # arch target vclib sdkver extra_libs
  local arch=$1 target=$2 vclib=$3 sdkver=$4 extra=$5
  mkdir -p $B/$arch && cd $B/$arch || return 1
  eval $T/clang-cl.exe --target=$target $PIN $CFLAGS /c $S/dllprobe.c /Fodllprobe.obj > compile.log 2>&1 || { echo "[$arch] COMPILE FAILED"; cat compile.log; return 1; }
  $T/lld-link.exe /nologo /OUT:dllprobe.exe /SUBSYSTEM:CONSOLE /GUARD:CF /DYNAMICBASE /NXCOMPAT /DEBUG /PDB:dllprobe.pdb \
    $extra dllprobe.obj \
    "/LIBPATH:$VC/lib/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/ucrt/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/um/$vclib" \
    libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib advapi32.lib > link.log 2>&1 || { echo "[$arch] LINK FAILED"; cat link.log; return 1; }
  echo "$arch: $(stat -c %s dllprobe.exe) B $(sha256sum dllprobe.exe | cut -c1-16) $(grep -c warning compile.log) warnings"
}
build x64   x86_64-pc-windows-msvc  x64 10.0.26100.0 ""
build x86   i686-pc-windows-msvc    x86 10.0.26100.0 ""
build arm32 thumbv7-pc-windows-msvc arm 10.0.19041.0 "F:/cr/rt21-cr/lib/clang/23/lib/windows/clang_rt.builtins-arm.lib"

# --- x64 server-control stand-in chrome.dll with a dependency (dllprobe_dep.dll) ---
CTRL=$B/x64/control
mkdir -p $CTRL && cd $CTRL || exit 1
LP=( "/LIBPATH:$VC/lib/x64" "/LIBPATH:$SDK/Lib/10.0.26100.0/ucrt/x64" "/LIBPATH:$SDK/Lib/10.0.26100.0/um/x64" )
DLLFLAGS="/nologo /O2 /MT /W4 /D_CRT_SECURE_NO_WARNINGS"
eval $T/clang-cl.exe --target=x86_64-pc-windows-msvc $PIN $DLLFLAGS /c $S/control_dep.c /Focontrol_dep.obj > dep.compile.log 2>&1 || { echo "dep compile FAILED"; cat dep.compile.log; exit 1; }
$T/lld-link.exe /nologo /DLL /OUT:dllprobe_dep.dll /IMPLIB:dllprobe_dep.lib /GUARD:CF /DYNAMICBASE /NXCOMPAT control_dep.obj "${LP[@]}" libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib > dep.link.log 2>&1 || { echo "dep link FAILED"; cat dep.link.log; exit 1; }
eval $T/clang-cl.exe --target=x86_64-pc-windows-msvc $PIN $DLLFLAGS /c $S/control_chrome.c /Focontrol_chrome.obj > chrome.compile.log 2>&1 || { echo "chrome compile FAILED"; cat chrome.compile.log; exit 1; }
$T/lld-link.exe /nologo /DLL /OUT:chrome.dll /IMPLIB:chrome.lib /GUARD:CF /DYNAMICBASE /NXCOMPAT control_chrome.obj dllprobe_dep.lib "${LP[@]}" libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib > chrome.link.log 2>&1 || { echo "chrome link FAILED"; cat chrome.link.log; exit 1; }
echo "control: chrome.dll $(stat -c %s chrome.dll) B imports dllprobe_dep.dll $(stat -c %s dllprobe_dep.dll) B"
