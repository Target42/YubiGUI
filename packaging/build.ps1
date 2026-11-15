# Baut das Windows-Setup und das Debian-Paket nach dist/.
$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$failed = $false

foreach ($script in @('build-windows.ps1', 'build-linux.ps1')) {
    Write-Host ""
    Write-Host "=== $script ==="
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $script)
    if ($LASTEXITCODE -ne 0) {
        Write-Host "$script ist fehlgeschlagen ($LASTEXITCODE)."
        $failed = $true
    }
}

$dist = Join-Path $Root 'dist'
if (Test-Path $dist) {
    $files = Get-ChildItem -Path $dist -File | Where-Object { $_.Name -ne 'SHA256SUMS' }
    $lines = foreach ($file in $files) {
        $hash = (Get-FileHash -Algorithm SHA256 -Path $file.FullName).Hash.ToLower()
        '{0}  {1}' -f $hash, $file.Name
    }
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [IO.File]::WriteAllText((Join-Path $dist 'SHA256SUMS'), (($lines -join "`n") + "`n"), $utf8)
    Write-Host ""
    Write-Host "Prüfsummen:"
    $lines | ForEach-Object { Write-Host $_ }
}

if ($failed) { exit 1 }
