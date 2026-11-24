# Venetium 0074: builds set-icon.exe into the browser's out dir, where package/portable.py's extras take it from (for the
# portable zip and the installer payload). AnyCPU MSIL against .NET Framework 4.0, like Setup, so ONE exe runs on
# Windows RT 8.1 / Windows 10 ARM32 and x86. Sources: SetIcon.cs + IconTools.cs (shared with Setup.cs).
# usage: build-set-icon.ps1 -OutDir F:\cr\src\out\arm-pgo        (build-installer.ps1 runs it itself)
param([Parameter(Mandatory = $true)][string]$OutDir)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$icon = Join-Path $repo 'branding\Venetium.ico'
$srcs = @((Join-Path $PSScriptRoot 'SetIcon.cs'), (Join-Path $PSScriptRoot 'IconTools.cs'))
foreach ($p in @($OutDir, $icon) + $srcs) { if (-not (Test-Path $p)) { throw "missing: $p" } }
. (Join-Path $PSScriptRoot 'csc-common.ps1')   # $csc, $refs
$out = Join-Path $OutDir 'set-icon.exe'
# /deterministic: build-installer.ps1 rebuilds set-icon.exe after the zip was made, and the packaging checks that the
# installer payload equals the zip set - same sources must give the same bytes (v1.4 packaging, 2026-11-24).
$cscArgs = @('/noconfig', '/nostdlib+', '/nologo', '/target:winexe', '/platform:anycpu', '/optimize+', '/deterministic', '/langversion:7.3', '/codepage:65001',
             "/out:$out", "/win32icon:$icon",
             ('/r:' + (Join-Path $refs 'mscorlib.dll')), ('/r:' + (Join-Path $refs 'System.dll')), ('/r:' + (Join-Path $refs 'System.Core.dll')),
             ('/r:' + (Join-Path $refs 'System.Drawing.dll')), ('/r:' + (Join-Path $refs 'System.Windows.Forms.dll'))) + $srcs
& $csc @cscArgs
if ($LASTEXITCODE -ne 0) { throw "set-icon.exe build failed ($LASTEXITCODE)" }
"set-icon.exe: $out ($((Get-Item $out).Length) bytes, sha256 $((Get-FileHash $out -Algorithm SHA256).Hash.Substring(0, 16).ToLowerInvariant())...)"
