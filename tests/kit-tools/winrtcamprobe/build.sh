#!/usr/bin/env bash
# winrtcamprobe: build x64 (server - compiles the whole chain; the server has no camera so it stops at "no camera", but
# it shakes out the WRL/WinRT code) and ARM32 (the device, the real test) with rt2.1 clang-cl + lld-link, static CRT
# (/MT), SDK 10.0.26100.0 headers; ARM32 uses the 19041 ARM import libs + our clang_rt.builtins-arm.lib (the __rt_* shim).
# Compiled as C++ (/TP) with NO C++ exceptions (WRL is HRESULT-based; this ARM32 toolchain can't emit C++ exceptions).
# Links runtimeobject (WinRT Ro*/HSTRING) + ole32. Output: F:/cr/media-cap/winrtcamprobe/bin/<arch>/winrtcamprobe.exe
set -u
export MSYS2_ARG_CONV_EXCL='*'
T=F:/cr/rt21-cr/bin
S=D:/repo/venetium/tests/kit-tools/winrtcamprobe
B=F:/cr/media-cap/winrtcamprobe/bin
VC="D:/Program Files/vs22buildtools/VC/Tools/MSVC/14.44.35207"
SDK="D:/Windows Kits/10"
PIN="-vctoolsdir \"$VC\" -winsdkdir \"$SDK\" -winsdkversion 10.0.26100.0"
CFLAGS="/nologo /O2 /MT /W3 /WX- /TP /GS /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE"
LIBS="kernel32.lib user32.lib ole32.lib oleaut32.lib runtimeobject.lib"
build() {   # arch target vclib sdkver extra_libs
  local arch=$1 target=$2 vclib=$3 sdkver=$4 extra=$5
  mkdir -p $B/$arch && cd $B/$arch || return 1
  eval $T/clang-cl.exe --target=$target $PIN $CFLAGS /c $S/winrtcamprobe.c /Fowinrtcamprobe.obj > compile.log 2>&1 || { echo "COMPILE FAIL ($arch):"; tail -30 compile.log; return 1; }
  $T/lld-link.exe /nologo /OUT:winrtcamprobe.exe /SUBSYSTEM:CONSOLE /DYNAMICBASE /NXCOMPAT /DEBUG /PDB:winrtcamprobe.pdb \
    $extra winrtcamprobe.obj \
    "/LIBPATH:$VC/lib/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/ucrt/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/um/$vclib" \
    libcmt.lib libucrt.lib libvcruntime.lib $LIBS > link.log 2>&1 || { echo "LINK FAIL ($arch):"; tail -30 link.log; return 1; }
  echo "$arch: $(stat -c %s winrtcamprobe.exe) B $(sha256sum winrtcamprobe.exe | cut -c1-16) $(grep -c warning compile.log) warnings"
}
build x64 x86_64-pc-windows-msvc x64 10.0.26100.0 ""
build arm32 thumbv7-pc-windows-msvc arm 10.0.19041.0 "F:/cr/rt21-cr/lib/clang/23/lib/windows/clang_rt.builtins-arm.lib"
