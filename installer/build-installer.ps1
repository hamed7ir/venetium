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

# ── 0. Venetium 0074: set-icon.exe into the out dir (portable.py's extras ship it, so the zip has it too) ─────
& (Join-Path $PSScriptRoot 'build-set-icon.ps1') -OutDir $OutDir
if ($LASTEXITCODE -ne 0) { throw "set-icon.exe build failed ($LASTEXITCODE)" }

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
# Venetium 0074: the Start tile manifest, set-icon.exe and the icons folder (portable.py extras = the zip's same files)
$extrasTsv = Join-Path $Work "extras-$Flavor.tsv"
& $py $portable extras $OutDir $extrasTsv | Out-Null
if ($LASTEXITCODE -ne 0) { throw "portable.py extras failed ($LASTEXITCODE)" }
foreach ($line in (Get-Content $extrasTsv | Where-Object { $_ })) {
    $relf, $srcf = $line -split "`t", 2
    if (-not (Test-Path $srcf)) { throw "extra file missing: $srcf" }
    $gz = Join-Path $setupDir ('payload\' + ($relf -replace '/', '\') + '.gz')
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

# ── 4. compile with Roslyn csc against .NET 4.0 reference assemblies ($csc, $refs: csc-common.ps1) ─────────────
. (Join-Path $PSScriptRoot 'csc-common.ps1')

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
$rsp.Add('"' + (Join-Path $repo 'installer\IconTools.cs') + '"')   # Venetium 0074, shared with set-icon.exe
$rsp.Add('"' + (Join-Path $setupDir 'SetupInfo.cs') + '"')
$rspFile = Join-Path $setupDir 'csc.rsp'
[System.IO.File]::WriteAllLines($rspFile, $rsp, (New-Object System.Text.UTF8Encoding($false)))
& $csc /noconfig "@$rspFile"
if ($LASTEXITCODE -ne 0) { throw "Setup build failed ($LASTEXITCODE)" }

# ── 5. verify it is MSIL and references only 4.0.0.0 assemblies ───────────────────────────────────────────────
# Read the facts in a CHILD Windows PowerShell: ReflectionOnlyLoadFrom in-process keeps $setupExe mapped until this
# powershell.exe exits, so a second build in the same console could not overwrite it (CS-Ray installer review, B1).
function AsmFacts([string]$path) {
    $helper = Join-Path ([System.IO.Path]::GetTempPath()) ('venetium-asmfacts-' + $PID + '.ps1')
    $code = @'
param([string]$p)
$a = [System.Reflection.Assembly]::ReflectionOnlyLoadFrom($p)
$r = ($a.GetReferencedAssemblies() | ForEach-Object { $_.Name + ' ' + $_.Version }) -join ', '
'A=' + [System.Reflection.AssemblyName]::GetAssemblyName($p).ProcessorArchitecture
'R=' + $r
'@
    [System.IO.File]::WriteAllText($helper, $code, (New-Object System.Text.UTF8Encoding($false)))
    try {
        $ps  = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $out = & $ps -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $helper $path
        if ($LASTEXITCODE -ne 0) { throw "could not read assembly facts of $path" }
    } finally { Remove-Item $helper -ErrorAction SilentlyContinue }
    $f = @{}
    foreach ($line in $out) { if ($line -match '^([AR])=(.*)$') { $f[$Matches[1]] = $Matches[2] } }
    return $f
}
$facts = AsmFacts $setupExe
$sarch = $facts['A']; $srefs = $facts['R']
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
