#!/usr/bin/env bash
# d3dprobe: build x64 (server control, runs on WARP/Basic Render) and ARM32 (the device) with rt2.1 clang-cl + lld-link, static
# CRT (/MT), SDK 10.0.26100.0 headers; ARM32 uses the 19041 ARM import libs and our clang_rt.builtins-arm.lib FIRST (the __rt_*
# shim). Links d3d11/dxgi/d3d9; d3dcompiler_47.dll is loaded at run time. Output: F:/cr/gpu-1/d3dprobe/bin/<arch>/d3dprobe.exe
set -u
export MSYS2_ARG_CONV_EXCL='*'
T=F:/cr/rt21-cr/bin
S=F:/cr/gpu-1/d3dprobe
B=F:/cr/gpu-1/d3dprobe/bin
VC="D:/Program Files/vs22buildtools/VC/Tools/MSVC/14.44.35207"
SDK="D:/Windows Kits/10"
PIN="-vctoolsdir \"$VC\" -winsdkdir \"$SDK\" -winsdkversion 10.0.26100.0"
CFLAGS="/nologo /O2 /MT /W3 /WX- /GS /guard:cf /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE"
build() {   # arch target vclib sdkver extra_libs
  local arch=$1 target=$2 vclib=$3 sdkver=$4 extra=$5
  mkdir -p $B/$arch && cd $B/$arch || return 1
  eval $T/clang-cl.exe --target=$target $PIN $CFLAGS /c $S/d3dprobe.c /Fod3dprobe.obj > compile.log 2>&1 || { cat compile.log; return 1; }
  $T/lld-link.exe /nologo /OUT:d3dprobe.exe /SUBSYSTEM:CONSOLE /DYNAMICBASE /NXCOMPAT /DEBUG /PDB:d3dprobe.pdb \
    $extra d3dprobe.obj \
    "/LIBPATH:$VC/lib/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/ucrt/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/um/$vclib" \
    libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib user32.lib d3d11.lib dxgi.lib d3d9.lib > link.log 2>&1 || { cat link.log; return 1; }
  echo "$arch: $(stat -c %s d3dprobe.exe) B $(sha256sum d3dprobe.exe | cut -c1-16) $(grep -c warning compile.log) warnings"
}
build x64 x86_64-pc-windows-msvc x64 10.0.26100.0 ""
build arm32 thumbv7-pc-windows-msvc arm 10.0.19041.0 "F:/cr/rt21-cr/lib/clang/23/lib/windows/clang_rt.builtins-arm.lib"
