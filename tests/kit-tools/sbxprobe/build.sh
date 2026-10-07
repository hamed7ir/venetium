#!/usr/bin/env bash
# sbxprobe: build ARM32 (the device), x86 and x64 (RUNTIME checks on this server) with rt2.1 clang-cl + lld-link, static CRT
# (/MT), MSVC 14.44.35207, SDK 10.0.26100.0 headers; ARM32 links the 19041 ARM import libs (D9) and our clang_rt.builtins-arm.lib
# (the __rt_* division shim) FIRST, as Chromium's links do. Output: F:/cr/sbxprobe/bin/<arch>/sbxprobe.exe
set -u
export MSYS2_ARG_CONV_EXCL='*'
T=F:/cr/rt21-cr/bin
S=F:/cr/sbxprobe/src
B=F:/cr/sbxprobe/bin
VC="D:/Program Files/vs22buildtools/VC/Tools/MSVC/14.44.35207"
SDK="D:/Windows Kits/10"
PIN="-vctoolsdir \"$VC\" -winsdkdir \"$SDK\" -winsdkversion 10.0.26100.0"
CFLAGS="/nologo /O2 /MT /W4 /WX- /GS /guard:cf /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE"
build() {   # arch target vclib sdkver extra_libs
  local arch=$1 target=$2 vclib=$3 sdkver=$4 extra=$5
  mkdir -p $B/$arch && cd $B/$arch || return 1
  eval $T/clang-cl.exe --target=$target $PIN $CFLAGS /c $S/sbxprobe.c /Fosbxprobe.obj > compile.log 2>&1 || { cat compile.log; return 1; }
  $T/lld-link.exe /nologo /OUT:sbxprobe.exe /SUBSYSTEM:CONSOLE /GUARD:CF /DYNAMICBASE /NXCOMPAT /DEBUG /PDB:sbxprobe.pdb \
    $extra sbxprobe.obj \
    "/LIBPATH:$VC/lib/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/ucrt/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/um/$vclib" \
    libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib advapi32.lib > link.log 2>&1 || { cat link.log; return 1; }
  echo "$arch: $(stat -c %s sbxprobe.exe) B $(sha256sum sbxprobe.exe | cut -c1-16) $(grep -c warning compile.log) warnings"
}
build x64 x86_64-pc-windows-msvc x64 10.0.26100.0 ""
build x86 i686-pc-windows-msvc x86 10.0.26100.0 ""
build arm32 thumbv7-pc-windows-msvc arm 10.0.19041.0 "F:/cr/rt21-cr/lib/clang/23/lib/windows/clang_rt.builtins-arm.lib"
