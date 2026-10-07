# Venetium build environment (BATCH-V8SIM-2 §1) — the ONE recorded environment every build batch sources, on the laptop
# and on the server (the paths are the same on both). Git Bash; source it: `source venetium/env/build-env.sh`.
# From s9/env.sh (SUPERMIUM-3 → VENETIUM-1..4 → V8SIM-1) without its scratch paths, plus the G6 PATH entry.
# gn.exe / ninja.exe run directly; F:/depot_tools deliberately NOT on PATH (unbootstrapped zip).
export DEPOT_TOOLS_WIN_TOOLCHAIN=0
export vs2022_install='D:\Program Files\vs22buildtools'
export GYP_MSVS_VERSION=2022
export WINDOWSSDKDIR='D:\Windows Kits\10\'
export MSYS2_ARG_CONV_EXCL='*'
export PYTHONDONTWRITEBYTECODE=1
GN=/f/cr/src/buildtools/win/gn.exe
NINJA=/f/cr/src/third_party/ninja/ninja.exe
# G6 (TOOLCHAIN-ISSUES.md): FIXED in the fixed rt2.1 (BATCH-RT21-FIX-1 / VENETIUM-DROP §3). The fixed lld-link is built with
# static libxml2 (xml2s.lib), so it merges/embeds manifests itself and mt.exe is no longer needed on PATH; the line was removed.
