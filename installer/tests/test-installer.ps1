# End-to-end test of Venetium-Setup-<ver>-<flavor>.exe against a SCRATCH folder. Build the installer first:
#     .\installer\build-installer.ps1 -Flavor arm32   (or x86)
#     powershell -NoProfile -ExecutionPolicy Bypass -File installer\tests\test-installer.ps1 -Flavor arm32
#
# Adapted from CS-Ray's installer test (the shared AnyCPU/.NET-4.0 installer). Safe on a dev box: it installs only under
# -Work (default %TEMP%\venetium-installer-test), backs up and restores your Start-menu Venetium.lnk, removes the
# uninstall key only if it didn't exist before, never touches a real install and never starts or kills the real browser.
# Close Venetium first (the in-use checks look for venetium processes). The byte-identical check compares installed files
# to the build output dir (-OutDir).
param(
    [ValidateSet('arm32', 'x86')][string]$Flavor = 'arm32',
    [string]$Setup,
    [string]$OutDir,
    [string]$Work
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $OutDir) { $OutDir = $(if ($Flavor -eq 'x86') { 'F:\cr\src\out\x86-rt21' } else { 'F:\cr\src\out\arm-rt21' }) }
if (-not $Setup) {
    foreach ($d in @((Join-Path $root "installer\..\..\..\installer-build-$Flavor\dist"), "F:\cr\installer-build-$Flavor\dist", 'C:\handoff\release-0051')) {
        $c = Get-ChildItem (Join-Path $d "Venetium-Setup-*-$Flavor.exe") -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
        if ($c) { $Setup = $c.FullName; break }
    }
}
if (-not $Setup -or -not (Test-Path $Setup)) { throw "No Venetium-Setup-*-$Flavor.exe found. Build the installer first." }
if (-not (Test-Path $OutDir)) { throw "OutDir not found: $OutDir" }
if (-not $Work) { $Work = Join-Path $env:TEMP 'venetium-installer-test' }
$version = [regex]::Match((Split-Path $Setup -Leaf), "Venetium-Setup-(.+)-$Flavor\.exe$").Groups[1].Value
"Setup:   $Setup"
"OutDir:  $OutDir"
"Version: $version   Flavor: $Flavor"
$fails = 0
function Check([string]$name, [bool]$ok, [string]$detail = '') {
    $tag = if ($ok) { '  ok  ' } else { '  FAIL' }
    Write-Output ("{0} {1,-58} {2}" -f $tag, $name, $detail)
    if (-not $ok) { $script:fails++ }
}
function RunSetup([string[]]$a) { $p = Start-Process -FilePath $Setup -ArgumentList $a -PassThru -Wait -WindowStyle Hidden; return $p.ExitCode }

$csc = if ($env:VENETIUM_CSC) { $env:VENETIUM_CSC } else { Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe' }
$here = $PSScriptRoot
if (Test-Path $Work) { Remove-Item $Work -Recurse -Force }
New-Item -ItemType Directory $Work | Out-Null
$dir = Join-Path $Work 'inst'
$programs = [Environment]::GetFolderPath('Programs')
$lnk = Join-Path $programs 'Venetium.lnk'
$lnkBackup = Join-Path $Work 'Venetium.lnk.backup'
$hadLnk = Test-Path $lnk
if ($hadLnk) { Copy-Item $lnk $lnkBackup }
$ukey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Venetium'
$hadKey = Test-Path $ukey

# user-data sentinel in the real profile path the installer must NEVER touch (%LocalAppData%\Venetium\User Data)
$userData = Join-Path (Join-Path $env:LOCALAPPDATA 'Venetium') 'User Data'
$sentinel = Join-Path $userData ('__installer_test_' + [guid]::NewGuid().ToString('N') + '.txt')
$madeUserData = $false; $madeVenetium = $false
$venetiumRoot = Join-Path $env:LOCALAPPDATA 'Venetium'
if (-not (Test-Path $venetiumRoot)) { New-Item -ItemType Directory $venetiumRoot | Out-Null; $madeVenetium = $true }
if (-not (Test-Path $userData)) { New-Item -ItemType Directory $userData | Out-Null; $madeUserData = $true }
Set-Content $sentinel 'user data - must survive uninstall'
$sentinelBefore = (Get-FileHash $sentinel).Hash

try {
    # 1. requirements report
    $rep = Join-Path $Work 'check.txt'
    $code = RunSetup @('--check', "`"$rep`"")
    Check '--check writes a report' ((Test-Path $rep) -and $code -in 0, 2, 3) "exit $code"

    # 2. silent install to scratch
    $code = RunSetup @('--silent', "`"$dir`"", '--no-desktop')
    Check '--silent install exit 0' ($code -eq 0) "exit $code"
    foreach ($f in 'venetium.exe', 'chrome.dll', 'libGLESv2.dll', 'resources.pak', 'uninstall.exe', 'locales\en-US.pak') {
        Check "  installed: $f" (Test-Path (Join-Path $dir $f))
    }
    $installed = Get-ChildItem $dir -File -Recurse | Where-Object { $_.Name -ne 'uninstall.exe' }
    $diff = $installed | Where-Object {
        $rel = $_.FullName.Substring($dir.Length + 1)
        $srcf = Join-Path $OutDir $rel
        (-not (Test-Path $srcf)) -or ((Get-FileHash $_.FullName).Hash -ne (Get-FileHash $srcf).Hash)
    }
    Check 'installed files byte-identical to the build output' ($diff.Count -eq 0) (($diff | Select-Object -First 3 | ForEach-Object Name) -join ', ')
    $k = Get-ItemProperty $ukey -ErrorAction SilentlyContinue
    Check 'uninstall entry written (location + version)' ($k -and $k.InstallLocation -eq $dir -and $k.DisplayVersion -eq $version) ("" + $k.DisplayVersion + " " + $k.InstallLocation)
    $sh = New-Object -ComObject WScript.Shell
    Check 'start menu shortcut -> installed exe' ((Test-Path $lnk) -and $sh.CreateShortcut($lnk).TargetPath -eq (Join-Path $dir 'venetium.exe'))
    Check 'no desktop shortcut with --no-desktop' (-not (Test-Path (Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Venetium.lnk')))

    # 3. a running venetium.exe from the folder blocks upgrade + uninstall (exit 4), and is never killed
    & $csc /nologo "/out:$Work\venetium.exe" "$here\Sleeper.cs" | Out-Null
    Copy-Item (Join-Path $dir 'venetium.exe') (Join-Path $Work 'real-venetium.exe')
    Copy-Item "$Work\venetium.exe" (Join-Path $dir 'venetium.exe') -Force
    $sleeper = Start-Process (Join-Path $dir 'venetium.exe') -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 700
    $code = RunSetup @('--silent', "`"$dir`"", '--no-desktop')
    Check 'install over a running Venetium -> exit 4' ($code -eq 4) "exit $code"
    $code = RunSetup @('--uninstall', "`"$dir`"", '--silent')
    Check 'uninstall while Venetium runs -> exit 4' ($code -eq 4) "exit $code"
    Check 'the running Venetium was not killed' (-not $sleeper.HasExited)
    Stop-Process -Id $sleeper.Id -Force; $sleeper.WaitForExit()
    Copy-Item (Join-Path $Work 'real-venetium.exe') (Join-Path $dir 'venetium.exe') -Force

    # 4. upgrade in place: a file Venetium never shipped stays; an unknown --silent switch is rejected
    Set-Content (Join-Path $dir 'my-notes.txt') 'not ours'
    $code = RunSetup @('--silent', "`"$dir`"", '--no-desktop')
    Check 'upgrade in place -> exit 0' ($code -eq 0) "exit $code"
    Check 'upgrade keeps a file Venetium never shipped' (Test-Path (Join-Path $dir 'my-notes.txt'))
    $code = RunSetup @('--silent', '/S')
    Check '--silent with an unknown switch (/S) -> exit 2' ($code -eq 2) "exit $code"

    # 5. folder guard (internal SafeTarget.Problem, via reflection on the AnyCPU Setup assembly)
    $asm = [System.Reflection.Assembly]::LoadFrom($Setup)
    $bf = [System.Reflection.BindingFlags]'NonPublic,Static,Public'
    $st = $asm.GetType('VenetiumSetup.SafeTarget').GetMethod('Problem', $bf)
    $prob = { param($d) $st.Invoke($null, @(, [string]$d)) }
    $docs0 = [Environment]::GetFolderPath('MyDocuments'); $desk0 = [Environment]::GetFolderPath('DesktopDirectory')
    Check 'guard refuses Documents / Desktop / profile / drive root / LocalAppData\Programs' (
        (& $prob $docs0) -and (& $prob $desk0) -and (& $prob $env:USERPROFILE) -and (& $prob 'C:\') -and (& $prob (Join-Path $env:LOCALAPPDATA 'Programs')))
    Check 'guard refuses a name with % or ^' ((& $prob 'C:\foo%bar') -and (& $prob 'C:\foo^bar'))
    Check 'guard accepts a new folder under Documents' ($null -eq (& $prob (Join-Path $docs0 'Venetium-new-folder-that-does-not-exist')))

    # 5b. a non-empty FOREIGN folder is refused by install and by uninstall, and its files are kept
    $foreign = Join-Path $Work 'foreign'; New-Item -ItemType Directory $foreign | Out-Null; Set-Content (Join-Path $foreign 'keep.txt') 'user data'
    $code = RunSetup @('--silent', "`"$foreign`"", '--no-desktop')
    Check '--silent into a non-empty foreign folder -> exit 5, file kept' ($code -eq 5 -and (Test-Path (Join-Path $foreign 'keep.txt'))) "exit $code"
    $code = RunSetup @('--uninstall', "`"$foreign`"", '--silent')
    Start-Sleep -Seconds 2
    Check '--uninstall of a foreign folder -> exit 5, file kept' ($code -eq 5 -and (Test-Path (Join-Path $foreign 'keep.txt'))) "exit $code"

    # 6. uninstall with the user's own files in the folder: ours go, theirs stay (my-notes.txt is still here from the
    # upgrade test above, keep-me.txt is added now - both are the user's, both must survive).
    Set-Content (Join-Path $dir 'keep-me.txt') 'mine'
    $code = RunSetup @('--uninstall', "`"$dir`"", '--silent')
    Start-Sleep -Seconds 9   # the detached cmd waits for Setup to exit, then deletes uninstall.exe and rmdir's if empty
    $left = @(Get-ChildItem $dir -Recurse -Force -ErrorAction SilentlyContinue | Where-Object { -not $_.PSIsContainer } | ForEach-Object { $_.FullName.Substring($dir.Length + 1) })
    Check 'uninstall with user files: exit 0, only theirs left' ($code -eq 0 -and (($left | Sort-Object) -join ',') -eq 'keep-me.txt,my-notes.txt') ("exit $code, left: " + ($left -join ','))
    Remove-Item $dir -Recurse -Force

    # 6b. install to a path given with a trailing backslash: stored without it, and it really installed
    $code = RunSetup @('--silent', "`"$dir\\`"", '--no-desktop')
    $k2 = Get-ItemProperty $ukey -ErrorAction SilentlyContinue
    Check 'trailing-backslash path: exit 0, normalised location' ($code -eq 0 -and $k2 -and $k2.InstallLocation -eq $dir -and (Test-Path (Join-Path $dir 'venetium.exe'))) ("exit $code " + $k2.InstallLocation)

    # 6c. clean uninstall: folder gone, uninstall key gone, our shortcut gone, user data untouched
    $code = RunSetup @('--uninstall', "`"$dir`"", '--silent')
    Start-Sleep -Seconds 9
    Check '--uninstall exit 0' ($code -eq 0) "exit $code"
    Check 'install folder removed' (-not (Test-Path $dir))
    Check 'uninstall entry removed' (-not (Test-Path $ukey) -or $hadKey)
    Check 'our start menu shortcut removed' (-not (Test-Path $lnk))
    Check 'user data (%LocalAppData%\Venetium\User Data) untouched' ((Test-Path $sentinel) -and (Get-FileHash $sentinel).Hash -eq $sentinelBefore)
}
finally {
    if ($hadLnk -and (Test-Path $lnkBackup)) { Copy-Item $lnkBackup $lnk -Force }
    if (-not $hadKey -and (Test-Path $ukey)) { Remove-Item $ukey -Recurse -Force }
    try { if (Test-Path $sentinel) { Remove-Item $sentinel -Force } } catch { }
    try { if ($madeUserData -and (Test-Path $userData) -and -not (Get-ChildItem $userData -Force)) { Remove-Item $userData -Force } } catch { }
    try { if ($madeVenetium -and (Test-Path $venetiumRoot) -and -not (Get-ChildItem $venetiumRoot -Force)) { Remove-Item $venetiumRoot -Force } } catch { }
    $sh2 = New-Object -ComObject WScript.Shell
    "restored start menu shortcut -> " + $(if (Test-Path $lnk) { $sh2.CreateShortcut($lnk).TargetPath } else { '(none)' })
}
if ($fails -eq 0) { 'ALL OK' } else { "$fails FAILURE(S)" }
