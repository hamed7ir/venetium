#!/usr/bin/env bash
# mediaprobe: build x64 (server sanity - exercises the code paths; the server has no camera/mic but WinRT factories and the
# API surface are present) and ARM32 (the device) with rt2.1 clang-cl + lld-link, static CRT (/MT), SDK 10.0.26100.0
# headers; ARM32 uses the 19041 ARM import libs and our clang_rt.builtins-arm.lib FIRST (the __rt_* shim). Links Media
# Foundation (mfplat/mf/mfreadwrite/mfuuid), ole32/oleaut32 (WASAPI via CoCreateInstance) and runtimeobject (WinRT factory
# probe). Output: F:/cr/media-cap/mediaprobe/bin/<arch>/mediaprobe.exe
set -u
export MSYS2_ARG_CONV_EXCL='*'
T=F:/cr/rt21-cr/bin
S=D:/repo/venetium/tests/kit-tools/mediaprobe
B=F:/cr/media-cap/mediaprobe/bin
VC="D:/Program Files/vs22buildtools/VC/Tools/MSVC/14.44.35207"
SDK="D:/Windows Kits/10"
PIN="-vctoolsdir \"$VC\" -winsdkdir \"$SDK\" -winsdkversion 10.0.26100.0"
CFLAGS="/nologo /O2 /MT /W3 /WX- /GS /guard:cf /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE"
LIBS="kernel32.lib user32.lib ole32.lib oleaut32.lib mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib runtimeobject.lib"
build() {   # arch target vclib sdkver extra_libs
  local arch=$1 target=$2 vclib=$3 sdkver=$4 extra=$5
  mkdir -p $B/$arch && cd $B/$arch || return 1
  eval $T/clang-cl.exe --target=$target $PIN $CFLAGS /c $S/mediaprobe.c /Fomediaprobe.obj > compile.log 2>&1 || { echo "COMPILE FAIL ($arch):"; cat compile.log; return 1; }
  $T/lld-link.exe /nologo /OUT:mediaprobe.exe /SUBSYSTEM:CONSOLE /DYNAMICBASE /NXCOMPAT /DEBUG /PDB:mediaprobe.pdb \
    $extra mediaprobe.obj \
    "/LIBPATH:$VC/lib/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/ucrt/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/um/$vclib" \
    libcmt.lib libucrt.lib libvcruntime.lib $LIBS > link.log 2>&1 || { echo "LINK FAIL ($arch):"; cat link.log; return 1; }
  echo "$arch: $(stat -c %s mediaprobe.exe) B $(sha256sum mediaprobe.exe | cut -c1-16) $(grep -c warning compile.log) warnings"
}
build x64 x86_64-pc-windows-msvc x64 10.0.26100.0 ""
build arm32 thumbv7-pc-windows-msvc arm 10.0.19041.0 "F:/cr/rt21-cr/lib/clang/23/lib/windows/clang_rt.builtins-arm.lib"
