#!/usr/bin/env bash
# ffmpeg-win-arm.sh -- BATCH-ARM-1 section 3G / decision 8 (Chrome branding only).
#
# Generates Chromium's FFmpeg config directory for Windows ARM32,
#     third_party/ffmpeg/chromium/config/Chrome/win/arm-neon/   (10 files)
# by running FFmpeg's OWN ./configure out of tree. Chromium's media/ffmpeg/scripts/build_ffmpeg.py cannot run
# on this tree as-is (no git HEAD for RoboConfiguration, an extension-less ./configure that native Windows
# Python cannot exec, host_os=='win' skips the cross-toolchain setup, no 'arm-neon' branch for win, and it
# would write build.arm-neon.win/ INSIDE the tree), so this script replays what build_ffmpeg.py does
# for a Windows config and applies build_ffmpeg.py's own post-configure rewrites to config.h, exactly:
#
#   Common flags ........ build_ffmpeg.py ConfigureAndBuild(): the 'Common' list incl. the Windows block
#                         (--toolchain=msvc, -I<ffmpeg>/chromium/include/win) and the cross-compile set that
#                         SetupWindowsCrossCompileToolchain() adds on a Linux host (--enable-cross-compile
#                         --cc=clang-cl --ld=lld-link --nm=llvm-nm --ar=llvm-ar --extra-cflags=-O2).
#   arm-neon flags ...... the 'arm-neon' branch of the linux block: --arch=arm --enable-armv6 --enable-armv6t2
#                         --enable-vfp --enable-thumb --enable-neon --extra-cflags=-mfpu=neon.
#   Chrome flags ........ --enable-decoder=aac,h264 --enable-demuxer=aac --enable-parser=aac,h264.
#   ffmpeg-rt knobs ..... --target-os=win32, --as = rt2.1 clang.exe (GAS-syntax .S files: armasm.exe cannot
#                         assemble them), --host-cc=clang for the x64 host tools; --enable-thumb --enable-neon.
#   NOT carried ......... -march=armv7-a, -mtune=cortex-a8, -mfloat-abi=hard (Linux sysroot cross flags; the
#                         Windows-ARM Chromium compile line carries --target=arm-windows -mfpu=neon only).
#                         Measured 2026-10-02: adding them changes none of the ten files (only the comment on config.h:4).
#                         Likewise --optflags=-O2 vs the literal --optflags="-O2" that build_ffmpeg.py's argv list passes
#                         (OPTFLAGS_MODE=literal) changes only that comment line.
# Output is shell-independent except for three build-host tool probes in config.h (HAVE_PERL / HAVE_POD2MAN /
# HAVE_XMLLINT, used by no source file): MSYS2 base has no perl (0), Git Bash has (1). The committed set was made under MSYS2.
# NOTE: MSYS2's bash drops environment variables handed over by a *Git Bash* `env -i`; set overrides inside the -lc string
# (bash -lc 'export WORK=...; bash script.sh').
#
# Pre-make rewrites applied (build_ffmpeg.py BuildFFmpeg, target_os 'win'): HAVE_VALGRIND_VALGRIND_H -> 0, HAVE_BCRYPT -> 0.
# Post-make rewrites applied (target_arch 'arm-neon'): FFMPEG_CONFIGURATION commented out; HAVE_VFP_ARGS commented
# out ("softfp/hardfp selection is done by the chrome build" -> third_party/ffmpeg/BUILD.gn asmflags);
# HAVE_VFP_INLINE / HAVE_VFP_EXTERNAL / HAVE_VFP forced to 1.
# `make` is not run (it is --config-only upstream too): the .o scan of generate_gn.py is replaced by the hand
# edit of ffmpeg_generated.gni (ffmpeg-gn.diff). libavutil/ffversion.h is made by make/version.sh, which needs
# the ffmpeg git history that this tree does not have, so it is copied from a sibling Chrome config of the same
# roll (all 34 current config dirs carry byte-identical copies; the script re-checks that).
#
# Runs under MSYS2 (default D:/MSYS2) or Git Bash:
#     D:/MSYS2/usr/bin/bash.exe -lc 'bash /f/cr/arm-1/drafts/ffmpeg/ffmpeg-win-arm.sh'
# It only READS $FFMPEG_SRC; the build dir is $WORK/build.arm-neon.win/Chrome; results go to $GEN_OUT.
# No tools beyond bash, sed, awk, grep, sha256sum, cmp, diff, cygpath and the rt2.1 LLVM tools are used
# (MSYS2 base has no perl and no python).

set -u
set -o pipefail

# ================================ CONFIG BLOCK ================================
# Everything machine-specific is here; every value can be overridden from the environment.
: "${CR_SRC:=/f/cr/src}"                                   # Chromium tree (read-only)
: "${FFMPEG_SRC:=$CR_SRC/third_party/ffmpeg}"              # FFmpeg configure source (read-only)
: "${RT21_BIN:=/f/cr/rt21-cr/bin}"                         # rt2.1 LLVM bin dir (clang-cl, clang, lld-link, ...)
: "${EXPECT_CLANG:=23.1.1-rt2.1}"                          # clang-cl --version must contain this
: "${MSVC_DIR:=D:/Program Files/vs22buildtools/VC/Tools/MSVC/14.44.35207}"   # 14.51+ dropped ARM32
: "${SDK_DIR:=D:/Windows Kits/10}"
: "${SDK_INC_VER:=10.0.26100.0}"                           # headers (decision D9 / Chromium's arm environment)
: "${SDK_LIB_VER:=10.0.19041.0}"                           # last SDK that ships ARM32 um/ucrt import libraries
: "${WORK:=/f/cr/arm-1/drafts/ffmpeg/work}"                # scratch: build dir + logs (out of tree)
: "${GEN_OUT:=/f/cr/arm-1/gen-inputs/ffmpeg}"              # output root; tree-relative path is appended
: "${BRANDING:=Chrome}"                                    # Chrome only (D13: codecs on); Chromium is not generated
: "${FFVERSION_FROM:=$FFMPEG_SRC/chromium/config/Chrome/win/arm64/libavutil/ffversion.h}"
: "${COMPARE_TO:=$FFMPEG_SRC/chromium/config/Chrome/linux/arm-neon}"
: "${OPTFLAGS_MODE:=plain}"                                # plain: --optflags=-O2 | literal: --optflags="-O2" as python passes it
# ==============================================================================

TARGET_REL=third_party/ffmpeg/chromium/config/$BRANDING/win/arm-neon
BUILD=$WORK/build.arm-neon.win/$BRANDING
LOG=$WORK/ffmpeg-win-arm.log
[ -n "${FFWA_DRYRUN:-}" ] && LOG=$WORK/ffmpeg-win-arm.dryrun.log

die() { echo "ffmpeg-win-arm: FAIL: $*" >&2; exit 1; }
say() { echo "ffmpeg-win-arm: $*"; }

# Re-run the body with output teed to $LOG (keeps the exit code).
if [ -z "${FFWA_INNER:-}" ]; then
  mkdir -p "$WORK" || { echo "cannot create $WORK" >&2; exit 1; }
  FFWA_INNER=1 bash "$0" "$@" 2>&1 | tee "$LOG"
  exit "${PIPESTATUS[0]}"
fi

[ "$BRANDING" = Chrome ] || die "BRANDING=$BRANDING: only Chrome is generated (D13, decision 8)"
say "start $(date '+%Y-%m-%d %H:%M:%S')  host: $(uname -s) $(uname -m)  bash $BASH_VERSION"

# ------------------------------- preflight ---------------------------------
CC_DIR=$(cygpath -u "$RT21_BIN") || die "cygpath missing"
for t in clang-cl clang lld-link llvm-nm llvm-ar llvm-readobj; do
  [ -f "$CC_DIR/$t.exe" ] || die "$CC_DIR/$t.exe not found (set RT21_BIN)"
done
ver=$("$CC_DIR/clang-cl.exe" --version 2>&1 | head -1)
case "$ver" in *"$EXPECT_CLANG"*) say "compiler: $ver" ;; *) die "wrong compiler: '$ver' (expected $EXPECT_CLANG)" ;; esac
[ -f "$FFMPEG_SRC/configure" ] || die "no $FFMPEG_SRC/configure"
[ -f "$FFVERSION_FROM" ] || die "no $FFVERSION_FROM"
[ -d "$MSVC_DIR/lib/arm" ] || die "no ARM32 MSVC libs at $MSVC_DIR/lib/arm"
[ -d "$SDK_DIR/Lib/$SDK_LIB_VER/um/arm" ] && [ -d "$SDK_DIR/Lib/$SDK_LIB_VER/ucrt/arm" ] \
  || die "no ARM32 import libraries in $SDK_DIR/Lib/$SDK_LIB_VER"
[ -d "$SDK_DIR/Include/$SDK_INC_VER/ucrt" ] || die "no $SDK_DIR/Include/$SDK_INC_VER/ucrt"

# INCLUDE/LIB (Windows form, ';'-separated) instead of -vctoolsdir/-winsdkdir arguments: configure cannot take
# arguments with spaces (build_ffmpeg.py:338-342), and these paths have them.
w() { cygpath -w "$1"; }
export INCLUDE="$(w "$MSVC_DIR/include");$(w "$SDK_DIR/Include/$SDK_INC_VER/ucrt");$(w "$SDK_DIR/Include/$SDK_INC_VER/um");$(w "$SDK_DIR/Include/$SDK_INC_VER/shared")"
export LIB="$(w "$MSVC_DIR/lib/arm");$(w "$SDK_DIR/Lib/$SDK_LIB_VER/ucrt/arm");$(w "$SDK_DIR/Lib/$SDK_LIB_VER/um/arm")"
export PATH="$CC_DIR:$PATH"
export MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1      # keep F:/... arguments untouched
export LC_ALL=C

# The toolset really is the pinned one? (clang-cl auto-detects MSVC; a newer one would hard-#error on ARM32.)
mkdir -p "$WORK/tc" && cd "$WORK/tc" || die "cd $WORK/tc"
printf '#include <stddef.h>\n#include <stdlib.h>\nint main(void){return 0;}\n' > tc.c
"$CC_DIR/clang-cl.exe" --target=arm-windows /c /showIncludes tc.c /Fotc.obj > tc.out 2>&1 || { cat tc.out; die "toolchain probe compile failed"; }
msvc_norm=$(printf '%s' "$MSVC_DIR" | tr 'A-Z\\' 'a-z/')
grep -i 'including file' tc.out | tr 'A-Z\\' 'a-z/' | grep -q "$msvc_norm/include" || die "clang-cl did not pick $MSVC_DIR (see $WORK/tc/tc.out)"
grep -i 'including file' tc.out | tr 'A-Z\\' 'a-z/' | grep -q "windows kits/10/include/$SDK_INC_VER/ucrt" || die "clang-cl did not pick SDK $SDK_INC_VER ucrt headers"
say "toolchain probe: MSVC $(basename "$MSVC_DIR") + SDK $SDK_INC_VER headers + SDK $SDK_LIB_VER ARM32 libs: OK"

# -------------------------------- flags ------------------------------------
FFW=$(cygpath -m "$FFMPEG_SRC")        # F:/cr/src/third_party/ffmpeg
CRW=$(cygpath -m "$CR_SRC")
case "$OPTFLAGS_MODE" in
  plain)   OPT=(--optflags=-O2) ;;
  literal) OPT=('--optflags="-O2"') ;;      # exactly what build_ffmpeg.py's argv list carries
  *) die "OPTFLAGS_MODE must be plain|literal" ;;
esac

COMMON=(                                    # build_ffmpeg.py: configure_flags['Common']
  --disable-everything --disable-all --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages
  --disable-static --enable-avcodec --enable-avformat --enable-avutil --enable-static --enable-libopus
  --disable-debug --disable-bzlib --disable-error-resilience --disable-iconv --disable-network --disable-schannel --disable-sdl2
  --disable-symver --disable-xlib --disable-zlib --disable-securetransport --disable-faan --disable-alsa --disable-iamf
  --disable-autodetect
  --enable-decoder=vorbis,libopus,flac
  --enable-decoder=pcm_u8,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,mp3
  --enable-decoder=pcm_s16be,pcm_s24be,pcm_mulaw,pcm_alaw
  --enable-demuxer=ogg,matroska,wav,flac,mp3,mov
  --enable-parser=opus,vorbis,flac,mpegaudio,vp9
  --extra-cflags=-I$CRW/third_party/opus/src/include
  --disable-linux-perf
  --x86asmexe=nasm
  "${OPT[@]}"
  # --- 'win' block ---
  --toolchain=msvc
  --extra-cflags=-I$FFW/chromium/include/win
  # --- SetupWindowsCrossCompileToolchain() set (a Windows host does not call it) ---
  --enable-cross-compile --cc=clang-cl --ld=lld-link --nm=llvm-nm --ar=llvm-ar --extra-cflags=-O2
  # --- arm-neon (+ ffmpeg-rt knobs) ---
  --arch=arm --target-os=win32
  --enable-armv6 --enable-armv6t2 --enable-vfp --enable-thumb --enable-neon
  --extra-cflags=--target=arm-windows --extra-cflags=-mfpu=neon
  --as="clang --target=armv7-pc-windows-msvc"
  --host-cc=clang --host-cflags=--target=x86_64-pc-windows-msvc
  --host-ld=clang --host-ldflags=--target=x86_64-pc-windows-msvc
  --extra-ldflags=-machine:arm
)
CHROME=( --enable-decoder=aac,h264 --enable-demuxer=aac --enable-parser=aac,h264 )

if [ -n "${FFWA_DRYRUN:-}" ]; then          # FFWA_DRYRUN=1: print the configure argv (one entry per line) and stop
  printf '%s\n' "${COMMON[@]}" "${CHROME[@]}"
  exit 0
fi

# ------------------------------- configure ---------------------------------
rm -rf "$BUILD" && mkdir -p "$BUILD" && cd "$BUILD" || die "cannot prepare $BUILD"
say "configure ($(date '+%H:%M:%S')) in $BUILD"
"$FFMPEG_SRC/configure" "${COMMON[@]}" "${CHROME[@]}" > configure.stdout 2> configure.stderr
rc=$?
cat configure.stdout; sed -n '1,40p' configure.stderr
[ $rc -eq 0 ] || die "configure rc=$rc (see $BUILD/ffbuild/config.log)"
say "configure rc=0 ($(date '+%H:%M:%S'))"
mkdir -p raw && cp config.h config_components.h raw/ || die "raw copy"

# ------------------- build_ffmpeg.py's rewrites (config.h) -----------------
rewrite() {   # rewrite <sed-ERE script> : same effect as RewriteFile()'s re.sub on config.h
  sed -E -i "$1" config.h || die "sed failed: $1"
}
# pre-make (target_os == 'win'):
rewrite 's|(#define HAVE_VALGRIND_VALGRIND_H [01])|#define HAVE_VALGRIND_VALGRIND_H 0 /* \1 -- forced to 0. See https://crbug.com/590440 */|'
rewrite 's|(#define HAVE_BCRYPT [01])|#define HAVE_BCRYPT 0|'
say "pre-make rewrites done (HAVE_VALGRIND_VALGRIND_H, HAVE_BCRYPT); make skipped (config-only)"
# post-make:
rewrite 's|(#define FFMPEG_CONFIGURATION .*)|/* \1 -- elide long configuration string from binary */|'
rewrite 's|(#define HAVE_VFP_ARGS [01])|/* \1 -- softfp/hardfp selection is done by the chrome build */|'
rewrite 's|(#define HAVE_VFP_INLINE [01])|#define HAVE_VFP_INLINE 1|'
rewrite 's|(#define HAVE_VFP_EXTERNAL [01])|#define HAVE_VFP_EXTERNAL 1|'
rewrite 's|(#define HAVE_VFP [01])|#define HAVE_VFP 1|'
say "post-make rewrites done (FFMPEG_CONFIGURATION, HAVE_VFP_ARGS, HAVE_VFP_INLINE, HAVE_VFP_EXTERNAL, HAVE_VFP)"

# ------------------------------- sanity gates -------------------------------
chk() {  # chk <regexp> <what>
  grep -Eq "$1" config.h || die "config.h gate failed: $2 (/$1/)"
}
chk '^#define ARCH_ARM 1$'              'ARCH_ARM 1'
chk '^#define ARCH_AARCH64 0$'          'ARCH_AARCH64 0'
chk '^#define CONFIG_THUMB 1$'          'CONFIG_THUMB 1'
chk '^#define HAVE_NEON 1$'             'HAVE_NEON 1'
chk '^#define HAVE_NEON_EXTERNAL 1$'    'HAVE_NEON_EXTERNAL 1'
chk '^#define HAVE_ARMV6T2 1$'          'HAVE_ARMV6T2 1'
chk '^#define HAVE_VFP 1$'              'HAVE_VFP 1'
chk '^#define HAVE_VFPV3 1$'            'HAVE_VFPV3 1'
chk '^#define HAVE_INLINE_ASM 1$'       'HAVE_INLINE_ASM 1'
chk '^#define HAVE_INTRINSICS_NEON 1$'  'HAVE_INTRINSICS_NEON 1'
chk '^#define EXTERN_ASM $'             'EXTERN_ASM empty (no symbol prefix on ARM Windows)'
chk '^#define HAVE_BCRYPT 0$'           'HAVE_BCRYPT 0'
chk '^#define CONFIG_PIC 0$'            'CONFIG_PIC 0 (no -fPIC on Windows)'
chk '^/\* #define HAVE_VFP_ARGS [01] -- softfp/hardfp selection is done by the chrome build \*/$' 'HAVE_VFP_ARGS commented out'
# configure's own float-ABI detection (raw, before the rewrite): the compiler said hard-float.
raw_vfp=$(grep -E '^#define HAVE_VFP_ARGS ' raw/config.h | head -1)
say "raw configure result before the rewrite: '$raw_vfp' (clang-cl defines __ARM_PCS_VFP / _M_ARM_FP=31 for thumbv7-windows)"
[ "$raw_vfp" = '#define HAVE_VFP_ARGS 1' ] || die "configure did not detect the hard-float ABI ($raw_vfp)"
for f in config.h config_components.h; do
  [ "$(tr -cd '\r' < $f | wc -c)" = 0 ] || die "$f has CR bytes (must be LF)"
done

# ------------------------- collect the 10 config files ----------------------
DEST=$GEN_OUT/$TARGET_REL
rm -rf "$DEST" && mkdir -p "$DEST/libavcodec" "$DEST/libavformat" "$DEST/libavutil" || die "mkdir $DEST"
for f in config.h config_components.h \
         libavcodec/bsf_list.c libavcodec/codec_list.c libavcodec/parser_list.c \
         libavformat/demuxer_list.c libavformat/muxer_list.c libavformat/protocol_list.c \
         libavutil/avconfig.h; do
  [ -f "$f" ] || die "configure did not produce $f"
  cp "$f" "$DEST/$f" || die "cp $f"
done
# libavutil/ffversion.h comes from make/version.sh upstream; all current config dirs hold the same bytes.
cp "$FFVERSION_FROM" "$DEST/libavutil/ffversion.h" || die "cp ffversion.h"
ref=$(sha256sum < "$FFVERSION_FROM")
n_same=0; n_diff=0
for d in "$FFMPEG_SRC"/chromium/config/*/*/*/; do
  f=${d}libavutil/ffversion.h
  [ -f "$f" ] || continue
  if [ "$(sha256sum < "$f")" = "$ref" ]; then n_same=$((n_same+1)); else n_diff=$((n_diff+1)); fi
done
say "ffversion.h: copied from $FFVERSION_FROM ($n_same config dirs hold identical bytes, $n_diff older/other-roll dirs differ)"
[ "$(grep -c . "$DEST/libavutil/ffversion.h")" -ge 3 ] || die "ffversion.h looks empty"

echo "-- manifest: <tree path> <sha256> <size>" | tee "$WORK/manifest.txt" >/dev/null
( cd "$GEN_OUT" && find "$TARGET_REL" -type f | LC_ALL=C sort | while read -r p; do
    printf '%s\t%s\t%s\n' "$p" "$(sha256sum < "$p" | cut -d' ' -f1)" "$(wc -c < "$p")"
  done ) | tee -a "$WORK/manifest.txt"
[ "$(find "$DEST" -type f | wc -l)" = 10 ] || die "expected 10 files in $DEST"

# ------------------ difference report against linux/arm-neon -----------------
if [ -d "$COMPARE_TO" ]; then
  diff -u "$COMPARE_TO/config.h" "$DEST/config.h" > "$WORK/config.h.vs-linux-arm-neon.diff"
  say "config.h vs $(basename "$(dirname "$COMPARE_TO")")/$(basename "$COMPARE_TO"): $(grep -c '^[-+][^-+]' "$WORK/config.h.vs-linux-arm-neon.diff") changed lines (full diff: $WORK/config.h.vs-linux-arm-neon.diff)"
  for f in config_components.h libavcodec/bsf_list.c libavcodec/codec_list.c libavcodec/parser_list.c \
           libavformat/demuxer_list.c libavformat/muxer_list.c libavformat/protocol_list.c libavutil/avconfig.h libavutil/ffversion.h; do
    if cmp -s "$COMPARE_TO/$f" "$DEST/$f"; then say "  $f: byte-identical to linux/arm-neon"; else say "  $f: DIFFERS from linux/arm-neon"; fi
  done
fi
say "done $(date '+%Y-%m-%d %H:%M:%S')  outputs: $DEST"
