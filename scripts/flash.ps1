param(
    [string]$Uf2Path,
    [string]$DriveRoot
)

function Find-Uf2Drive {
    Get-PSDrive -PSProvider FileSystem | ForEach-Object {
        $infoPath = Join-Path $_.Root "INFO_UF2.TXT"
        if (Test-Path $infoPath) {
            $info = Get-Content $infoPath -ErrorAction SilentlyContinue
            if ($info -match "RP2350" -or $info -match "RPI-RP2") {
                $_.Root
            }
        }
    } | Select-Object -First 1
}

function Find-DefaultUf2 {
    $matches = Get-ChildItem -Path $PSScriptRoot -Filter *.uf2 -File -ErrorAction SilentlyContinue
    if ($matches.Count -eq 1) {
        return $matches[0].FullName
    }
    if ($matches.Count -gt 1) {
        throw "Multiple UF2 files were found next to flash.ps1. Pass -Uf2Path explicitly."
    }
    return $null
}

if (-not $Uf2Path) {
    $Uf2Path = Find-DefaultUf2
}

if (-not $Uf2Path) {
    throw "UF2 path is required when no package-local UF2 file is available."
}

$resolvedUf2 = Resolve-Path $Uf2Path
if (-not $resolvedUf2) {
    throw "UF2 file not found: $Uf2Path"
}

$targetRoot = $DriveRoot
if (-not $targetRoot) {
    $targetRoot = Find-Uf2Drive
}

if (-not $targetRoot) {
    throw "No mounted RP2350 UF2 drive was found."
}

Copy-Item $resolvedUf2 $targetRoot -Force
Write-Host "Flashed $resolvedUf2 to $targetRoot"
