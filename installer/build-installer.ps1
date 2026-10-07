# Builds the Venetium installer: Setup.cs (AnyCPU MSIL, .NET Framework 4.0) with the browser payload embedded as
# gzip resources, compiled with Roslyn csc. MSIL so ONE exe runs on Windows RT 8.1 / Windows 10 ARM32 as well as
# x86/x64 (a native x86 installer loader cannot start on ARM32 Windows). Adapted from hamed7ir/DroidFinestra.
#
#   .\installer\build-installer.ps1 -OutDir <chromium out dir> [-Version 150.0.7871.226]
#
# Produces <Work>\dist\Venetium-Setup-<ver>-arm32.exe  (+ SHA256SUMS.txt). The browser payload is the shipped set
# portable.py resolves (GENERAL+HIDPI) - the kit is NOT included. The installer source lives in the repo; the exe
# ships as a GitHub Release asset (it is git-ignored).
param(
    [ValidateSet('arm32', 'x86')][string]$Flavor = 'arm32',
    [string]$OutDir,
    [string]$Version = '150.0.7871.226',
    [string]$Work
)
$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = $(if ($Flavor -eq 'x86') { 'F:\cr\src\out\x86-rt21' } else { 'F:\cr\src\out\arm-rt21' }) }
if (-not $Work)   { $Work   = "F:\cr\installer-build-$Flavor" }
$repo   = Split-Path -Parent $PSScriptRoot
$py     = 'C:\Python313\python3.exe'
$portable = Join-Path $repo 'package\portable.py'
$setupCs  = Join-Path $repo 'installer\Setup.cs'
$icon     = Join-Path $repo 'branding\Venetium.ico'
foreach ($p in @($OutDir, $py, $portable, $setupCs, $icon)) { if (-not (Test-Path $p)) { throw "missing: $p" } }
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Sha256([string]$p) { (Get-FileHash $p -Algorithm SHA256).Hash.ToLowerInvariant() }

# ── 1. resolve the shipped browser set (no kit) with portable.py ─────────────────────────────────────────────
if (Test-Path $Work) { Remove-Item $Work -Recurse -Force }
New-Item -ItemType Directory -Force $Work | Out-Null
$tsv = Join-Path $Work "set-$Flavor.tsv"
& $py $portable set $OutDir $Flavor $tsv | Out-Null
if ($LASTEXITCODE -ne 0) { throw "portable.py set failed ($LASTEXITCODE)" }
$files = Get-Content $tsv | Where-Object { $_ -and ($_ -notmatch '^#') -and ($_ -notmatch '^path\t') } |
         ForEach-Object { ($_ -split "`t")[0] }
if (-not $files -or $files.Count -lt 1) { throw "no payload files resolved from $tsv" }
"payload: $($files.Count) files"

# ── 2. gzip each payload file (subdirs preserved); build payload.list + csc /resource args ───────────────────
$setupDir = Join-Path $Work 'setup'
New-Item -ItemType Directory -Force (Join-Path $setupDir 'payload') | Out-Null
$list = New-Object System.Collections.Generic.List[string]
$res  = New-Object System.Collections.Generic.List[string]
foreach ($rel in $files) {
    $srcf = Join-Path $OutDir $rel
    if (-not (Test-Path $srcf)) { throw "payload file missing: $srcf" }
    $relf = ($rel -replace '\\', '/')                         # forward slashes in list + resource name
    $gz   = Join-Path $setupDir ('payload\' + ($relf -replace '/', '\') + '.gz')
    New-Item -ItemType Directory -Force (Split-Path $gz) | Out-Null
    $in = [System.IO.File]::OpenRead($srcf); $out = [System.IO.File]::Create($gz)
    $z  = New-Object System.IO.Compression.GZipStream($out, [System.IO.Compression.CompressionMode]::Compress)
    $in.CopyTo($z); $z.Dispose(); $out.Dispose(); $in.Dispose()
    $list.Add((Sha256 $srcf) + "`t" + (Get-Item $srcf).Length + "`t" + $relf)
    $res.Add('/resource:"' + $gz + '",payload/' + $relf + '.gz')
}
[System.IO.File]::WriteAllLines((Join-Path $setupDir 'payload.list'), $list, (New-Object System.Text.UTF8Encoding($false)))
"staged + gzipped $($list.Count) payload files"

# ── 3. SetupInfo.cs (namespace must match Setup.cs) ──────────────────────────────────────────────────────────
$info = @"
using System.Reflection;
[assembly: AssemblyTitle("Venetium Setup")]
[assembly: AssemblyProduct("Venetium")]
[assembly: AssemblyCompany("Hamed Ghorbani")]
[assembly: AssemblyCopyright("(c) 2026 Hamed Ghorbani")]
[assembly: AssemblyVersion("$Version")]
[assembly: AssemblyFileVersion("$Version")]
[assembly: AssemblyInformationalVersion("$Version $Flavor")]
namespace VenetiumSetup
{
    internal static class Info
    {
        internal const string AppVersion = "$Version";
        internal const string Flavor = "$Flavor";
    }
}
"@
[System.IO.File]::WriteAllText((Join-Path $setupDir 'SetupInfo.cs'), $info, (New-Object System.Text.UTF8Encoding($false)))

# ── 4. compile with Roslyn csc against .NET 4.0 reference assemblies ─────────────────────────────────────────
# Roslyn csc (needs C# 7.3). $env:VENETIUM_CSC overrides; else a VS 2022 build-tools install; else vswhere.
$csc = $env:VENETIUM_CSC
if (-not $csc -or -not (Test-Path $csc)) {
    $csc = $null
    foreach ($c in @(
        'D:\Program Files\vs22buildtools\MSBuild\Current\Bin\Roslyn\csc.exe',
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe'),
        (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe'))) {
        if (Test-Path $c) { $csc = $c; break }
    }
}
if (-not $csc) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) { $csc = & $vswhere -version '[17.0,18.0)' -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\Roslyn\csc.exe' | Select-Object -First 1 }
}
if (-not $csc -or -not (Test-Path $csc)) { throw 'Roslyn csc.exe not found. Set VENETIUM_CSC to a VS 2022 csc.exe.' }
"csc: $csc"

# .NET Framework 4.x reference set: prefer a targeting pack (4.0 Client is best - it keeps the build to APIs that
# run on a bare .NET 4.0 / Windows RT); fall back to the installed 4.x runtime assemblies when no pack is present.
$refs = $null
$raRoot = Join-Path ${env:ProgramFiles(x86)} 'Reference Assemblies\Microsoft\Framework\.NETFramework'
foreach ($cand in @('v4.0\Profile\Client', 'v4.0', 'v4.5', 'v4.5.1', 'v4.5.2', 'v4.6', 'v4.6.1', 'v4.6.2', 'v4.7.2', 'v4.8')) {
    $d = Join-Path $raRoot $cand
    if (Test-Path (Join-Path $d 'mscorlib.dll')) { $refs = $d; "reference assemblies: $refs (targeting pack)"; break }
}
if (-not $refs) {
    foreach ($d in @("$env:windir\Microsoft.NET\Framework64\v4.0.30319", "$env:windir\Microsoft.NET\Framework\v4.0.30319")) {
        if (Test-Path (Join-Path $d 'mscorlib.dll')) { $refs = $d; "reference assemblies: $refs (runtime 4.x - no targeting pack installed)"; break }
    }
}
if (-not $refs) { throw 'no .NET Framework 4.x reference assemblies or runtime assemblies found' }

$setupExe = Join-Path $setupDir ("Venetium-Setup-$Version-$Flavor.exe")
$rsp = New-Object System.Collections.Generic.List[string]
$rsp.AddRange([string[]]@(
    '/nostdlib+', '/nologo', '/target:winexe', '/platform:anycpu', '/optimize+', '/langversion:7.3', '/codepage:65001',
    ('/out:"' + $setupExe + '"'),
    ('/win32icon:"' + $icon + '"'),
    ('/resource:"' + $icon + '",setup.icon.ico'),
    ('/resource:"' + (Join-Path $setupDir 'payload.list') + '",payload.list'),
    ('/r:"' + (Join-Path $refs 'mscorlib.dll') + '"'),
    ('/r:"' + (Join-Path $refs 'System.dll') + '"'),
    ('/r:"' + (Join-Path $refs 'System.Core.dll') + '"'),
    ('/r:"' + (Join-Path $refs 'System.Drawing.dll') + '"'),
    ('/r:"' + (Join-Path $refs 'System.Windows.Forms.dll') + '"')))
$rsp.AddRange([string[]]$res)
$rsp.Add('"' + $setupCs + '"')
$rsp.Add('"' + (Join-Path $setupDir 'SetupInfo.cs') + '"')
$rspFile = Join-Path $setupDir 'csc.rsp'
[System.IO.File]::WriteAllLines($rspFile, $rsp, (New-Object System.Text.UTF8Encoding($false)))
& $csc /noconfig "@$rspFile"
if ($LASTEXITCODE -ne 0) { throw "Setup build failed ($LASTEXITCODE)" }

# ── 5. verify it is MSIL and references only 4.0.0.0 assemblies ───────────────────────────────────────────────
$sarch = [System.Reflection.AssemblyName]::GetAssemblyName($setupExe).ProcessorArchitecture
$srefs = ([System.Reflection.Assembly]::ReflectionOnlyLoadFrom($setupExe).GetReferencedAssemblies() | ForEach-Object { $_.Name + ' ' + $_.Version }) -join ', '
"Setup arch = $sarch   refs = $srefs"
if ("$sarch" -ne 'MSIL') { throw "Setup: expected MSIL, got $sarch" }
if ($srefs -match ' [1-9][0-9]*\.[1-9]' -or $srefs -notmatch 'mscorlib 4\.0\.0\.0') { throw "Setup must reference only 4.0.0.0 assemblies: $srefs" }

# ── 6. publish ───────────────────────────────────────────────────────────────────────────────────────────────
$dist = Join-Path $Work 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item $setupExe $dist -Force
$exeOut = Join-Path $dist (Split-Path $setupExe -Leaf)
[System.IO.File]::WriteAllText((Join-Path $dist 'SHA256SUMS.txt'), (Sha256 $exeOut) + '  ' + (Split-Path $exeOut -Leaf) + "`n", (New-Object System.Text.UTF8Encoding($false)))
"setup:  $exeOut"
"size:   {0:N0} bytes" -f (Get-Item $exeOut).Length
"sha256: $(Sha256 $exeOut)"
