# Venetium installer + set-icon.exe: finds Roslyn csc ($csc) and the .NET Framework 4.x reference assemblies ($refs).
# Dot-sourced by build-installer.ps1 and build-set-icon.ps1.
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
