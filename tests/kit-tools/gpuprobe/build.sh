#!/usr/bin/env bash
# gpuprobe v2: ARM32 (Windows RT 8.x / 10 ARM32 tablets) and x64 (a smoke test on this server) with rt2.3 clang-cl +
# lld-link, static CRT (/MT), SDK 10.0.26100.0 headers; ARM32 links the 19041 ARM32 import libraries, subsystem 6.02
# (Windows 8+, like Venetium's RT builds). Every graphics/media DLL is loaded at run time: the exe imports only
# kernel32/user32/gdi32/advapi32/ole32. Output: F:/cr/gpuprobe2/bin/<arch>/gpuprobe.exe
set -u
export MSYS2_ARG_CONV_EXCL='*'
T=F:/cr/rt23-cr/bin
S=F:/cr/gpuprobe2/src
B=F:/cr/gpuprobe2/bin
VC="D:/Program Files/vs22buildtools/VC/Tools/MSVC/14.44.35207"
SDK="D:/Windows Kits/10"
PIN="-vctoolsdir \"$VC\" -winsdkdir \"$SDK\" -winsdkversion 10.0.26100.0"
CFLAGS="/nologo /O2 /MT /W3 /WX- /GS /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE -Wno-unused-function"
build() {   # arch target vclib sdkver subsystem
  local arch=$1 target=$2 vclib=$3 sdkver=$4 subsys=$5
  mkdir -p $B/$arch && cd $B/$arch || return 1
  eval $T/clang-cl.exe --target=$target $PIN $CFLAGS /c $S/gpuprobe.c /Fogpuprobe.obj > compile.log 2>&1 || { cat compile.log; return 1; }
  $T/lld-link.exe /nologo /OUT:gpuprobe.exe /SUBSYSTEM:CONSOLE,$subsys /DYNAMICBASE /NXCOMPAT /DEBUG /PDB:gpuprobe.pdb \
    gpuprobe.obj \
    "/LIBPATH:$VC/lib/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/ucrt/$vclib" "/LIBPATH:$SDK/Lib/$sdkver/um/$vclib" \
    libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib user32.lib gdi32.lib advapi32.lib ole32.lib > link.log 2>&1 || { cat link.log; return 1; }
  echo "$arch: $(stat -c %s gpuprobe.exe) B $(sha256sum gpuprobe.exe | cut -c1-16) $(grep -c 'warning:' compile.log) warnings"
}
build x64 x86_64-pc-windows-msvc x64 10.0.26100.0 6.02
build arm32 thumbv7-pc-windows-msvc arm 10.0.19041.0 6.02
