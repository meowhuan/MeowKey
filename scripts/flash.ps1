param(
    [Parameter(Mandatory = $true)]
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
