# Baut das Debian-Paket. Der Linux-Bauvorgang läuft in einem Ubuntu-24.04-Container,
# nfpm packt das Ergebnis auf Windows.
$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Version = (Get-Content -Raw -Path (Join-Path $Root 'packaging\VERSION')).Trim()
$Build = Join-Path $Root 'build\linux'
$Dist = Join-Path $Root 'dist'

$sh = Join-Path $Root 'packaging\build-linux.sh'
$text = [IO.File]::ReadAllText($sh) -replace "`r`n", "`n" -replace "`r", "`n"
$utf8 = New-Object System.Text.UTF8Encoding $false
[IO.File]::WriteAllText($sh, $text, $utf8)

# Docker schreibt Hinweise auf stderr. Unter Windows PowerShell würde das den Lauf abbrechen.
$ErrorActionPreference = 'Continue'
docker info *> $null
if ($LASTEXITCODE -ne 0) {
    throw 'Docker läuft nicht. Docker Desktop starten und packaging/build-linux.ps1 erneut ausführen.'
}

Write-Host "Linux-Build in ubuntu:24.04"
docker run --rm -v "${Root}:/src" -w /src ubuntu:24.04 bash /src/packaging/build-linux.sh
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$ErrorActionPreference = 'Stop'

$depends = @(Get-Content -Path (Join-Path $Build 'depends.txt') | Where-Object { $_.Trim() -ne '' })
if ($depends.Count -eq 0) { throw 'depends.txt ist leer.' }

$dependLines = ($depends | ForEach-Object { '  - "' + ($_ -replace '"', '') + '"' }) -join "`n"
$desktop = '../../packaging/linux/yubigui.desktop' -replace '\\', '/'
$yaml = @"
name: yubigui
arch: amd64
platform: linux
version: "$Version"
version_schema: none
section: utils
priority: optional
maintainer: Stephan Winter <stephan.winter@online.de>
homepage: https://github.com/Target42/YubiGUI
description: |
  Qt-Oberfläche für den YubiKey.
  YubiGUI startet das installierte ykman und zeigt das Ergebnis im Fenster.
  ykman 6 liegt nicht in diesem Paket. Das Paket yubikey-manager der
  Distribution ist häufig die Fassung 5 und reicht dafür nicht.
  Qt kommt aus den Paketen von Ubuntu 24.04 (libqt6widgets6t64).
  pcscd und openssh-client sind empfohlen, nicht vorausgesetzt.
depends:
$dependLines
recommends:
  - pcscd
  - openssh-client
contents:
  - src: yubigui
    dst: /usr/bin/yubigui
    file_info:
      mode: 0755
  - src: $desktop
    dst: /usr/share/applications/yubigui.desktop
    file_info:
      mode: 0644
"@

New-Item -ItemType Directory -Force -Path $Build | Out-Null
$yamlPath = Join-Path $Build 'nfpm.yaml'
[IO.File]::WriteAllText($yamlPath, ($yaml -replace "`r`n", "`n"), $utf8)

New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$debName = "yubigui_${Version}_amd64.deb"
$debPath = Join-Path $Dist $debName
if (Test-Path $debPath) { Remove-Item $debPath }

Write-Host "Packe $debName"
$ErrorActionPreference = 'Continue'
Push-Location $Build
try {
    & nfpm package -f $yamlPath -p deb -t $Dist
} finally {
    Pop-Location
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $debPath)) { throw "nfpm hat $debPath nicht geschrieben." }

Write-Host "Fertig: $debPath"
