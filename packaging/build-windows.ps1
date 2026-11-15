# Baut yubigui mit dem MinGW-Kit von Qt 6.10, legt die Qt-Laufzeit dazu
# und packt ein Setup mit Inno Setup.
$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Version = (Get-Content -Raw -Path (Join-Path $Root 'packaging\VERSION')).Trim()
$QtRoot = if ($env:YUBIGUI_QT) { $env:YUBIGUI_QT } else { 'D:\Qt\6.10.0\mingw_64' }
$Mingw = if ($env:YUBIGUI_MINGW) { $env:YUBIGUI_MINGW } else { 'D:\Qt\Tools\mingw1310_64' }
$Build = Join-Path $Root 'build\windows'
$Stage = Join-Path $Root 'build\windows-dist'
$Dist = Join-Path $Root 'dist'

$qmake = Join-Path $QtRoot 'bin\qmake.exe'
$deploy = Join-Path $QtRoot 'bin\windeployqt.exe'
$make = Join-Path $Mingw 'bin\mingw32-make.exe'
foreach ($tool in @($qmake, $deploy, $make)) {
    if (-not (Test-Path $tool)) { throw "Werkzeug fehlt: $tool" }
}

function Find-Iscc {
    $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 7\ISCC.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

$env:PATH = "$(Join-Path $QtRoot 'bin');$(Join-Path $Mingw 'bin');$env:PATH"

if (Test-Path $Build) { Remove-Item -Recurse -Force $Build }
New-Item -ItemType Directory -Force -Path $Build | Out-Null
Push-Location $Build
try {
    Write-Host "qmake $Version"
    & $qmake (Join-Path $Root 'YubiGUI.pro') -spec win32-g++ 'CONFIG+=release' 'CONFIG-=debug_and_release' "VERSION=$Version"
    if ($LASTEXITCODE -ne 0) { throw "qmake endete mit $LASTEXITCODE" }
    Write-Host "mingw32-make"
    & $make -j8
    if ($LASTEXITCODE -ne 0) { throw "mingw32-make endete mit $LASTEXITCODE" }
} finally {
    Pop-Location
}

$exe = @(
    (Join-Path $Build 'release\yubigui.exe'),
    (Join-Path $Build 'yubigui.exe')
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { throw 'yubigui.exe wurde nicht erzeugt.' }

if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Copy-Item $exe (Join-Path $Stage 'yubigui.exe')

Write-Host "windeployqt"
& $deploy --release --compiler-runtime --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $Stage 'yubigui.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt endete mit $LASTEXITCODE" }

$required = @(
    'yubigui.exe',
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Widgets.dll',
    'platforms\qwindows.dll'
)
foreach ($rel in $required) {
    if (-not (Test-Path (Join-Path $Stage $rel))) { throw "Nach windeployqt fehlt $rel" }
}
$gcc = Get-ChildItem -Path $Stage -Filter 'libgcc*.dll' -File
if (-not $gcc) { throw 'Die MinGW-Laufzeit (libgcc) fehlt neben yubigui.exe.' }

Copy-Item (Join-Path $Root 'packaging\windows\hinweis.txt') (Join-Path $Stage 'LIESMICH.txt')
Copy-Item (Join-Path $Root 'packaging\windows\qt-hinweis.txt') (Join-Path $Stage 'Qt-HINWEIS.txt')
$qtLicense = 'D:\Qt\Licenses\LICENSE'
if (-not (Test-Path $qtLicense)) { throw "Qt-Lizenz fehlt: $qtLicense" }
Copy-Item $qtLicense (Join-Path $Stage 'Qt-LICENSE.txt')

Write-Host "Startprüfung"
$proc = Start-Process -FilePath (Join-Path $Stage 'yubigui.exe') -WorkingDirectory $Stage -PassThru
Start-Sleep -Seconds 2
if ($proc.HasExited) {
    throw "yubigui.exe endete sofort mit Exit $($proc.ExitCode). Eine Qt-DLL fehlt vermutlich."
}
Stop-Process -Id $proc.Id -Force
Get-Process -Name yubigui -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$setup = Join-Path $Dist "YubiGUI-$Version-windows-x64-setup.exe"
if (Test-Path $setup) { Remove-Item -Force $setup }

$iscc = Find-Iscc
if (-not $iscc) {
    throw 'Inno Setup fehlt. winget install --id JRSoftware.InnoSetup -e'
}

$bom = New-Object System.Text.UTF8Encoding $true
foreach ($note in @(
    (Join-Path $Root 'packaging\windows\hinweis.txt'),
    (Join-Path $Stage 'LIESMICH.txt'),
    (Join-Path $Stage 'Qt-HINWEIS.txt')
)) {
    $noteText = [IO.File]::ReadAllText($note)
    [IO.File]::WriteAllText($note, $noteText, $bom)
}

Write-Host "Inno Setup ($iscc)"
& $iscc "/DMyAppVersion=$Version" (Join-Path $Root 'packaging\windows\yubigui.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC endete mit $LASTEXITCODE" }
if (-not (Test-Path $setup)) { throw "Setup fehlt: $setup" }

Write-Host "Fertig: $setup"
