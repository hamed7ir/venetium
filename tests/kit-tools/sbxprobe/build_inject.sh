#!/usr/bin/env bash
# sbxprobe TEST builds (never shipped): x64 with SBX_INJECT=1 (one fake cause: HEAP_TERMINATE value 3 in every replay word)
# and SBX_INJECT=2 (+ a non-inheritable handle in every replay HANDLE_LIST: two independent causes), to exercise the
# leave-one-out, the cumulative removal and the DIAGNOSIS block on this server. Output: F:/cr/sbxprobe/inject/<n>/sbxprobe.exe
set -u
export MSYS2_ARG_CONV_EXCL='*'
T=F:/cr/rt21-cr/bin
S=F:/cr/sbxprobe/src
VC="D:/Program Files/vs22buildtools/VC/Tools/MSVC/14.44.35207"
SDK="D:/Windows Kits/10"
PIN="-vctoolsdir \"$VC\" -winsdkdir \"$SDK\" -winsdkversion 10.0.26100.0"
CFLAGS="/nologo /O2 /MT /W4 /WX- /GS /guard:cf /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE"
for n in 1 2; do
  O=F:/cr/sbxprobe/inject/$n
  mkdir -p $O && cd $O || exit 1
  eval $T/clang-cl.exe --target=x86_64-pc-windows-msvc $PIN $CFLAGS /DSBX_INJECT=$n /c $S/sbxprobe.c /Fosbxprobe.obj > compile.log 2>&1 || { cat compile.log; exit 1; }
  $T/lld-link.exe /nologo /OUT:sbxprobe.exe /SUBSYSTEM:CONSOLE /GUARD:CF /DYNAMICBASE /NXCOMPAT sbxprobe.obj \
    "/LIBPATH:$VC/lib/x64" "/LIBPATH:$SDK/Lib/10.0.26100.0/ucrt/x64" "/LIBPATH:$SDK/Lib/10.0.26100.0/um/x64" \
    libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib advapi32.lib > link.log 2>&1 || { cat link.log; exit 1; }
  echo "inject $n: $(stat -c %s sbxprobe.exe) B, $(grep -c warning compile.log) warnings"
done
