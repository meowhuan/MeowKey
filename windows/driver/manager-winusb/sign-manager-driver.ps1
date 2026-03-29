param(
    [string]$CertSubject = "CN=MeowKey Manager Driver Test",
    [string]$PfxPath = "",
    [string]$PfxPassword = "",
    [string]$TimestampUrl = "",
    [switch]$SkipCatalogGeneration
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$driverDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$infPath = Join-Path $driverDir "meowkey-manager-winusb.inf"
$catPath = Join-Path $driverDir "meowkey-manager-winusb.cat"

function Get-WindowsKitBinary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FileName,
        [string[]]$Architectures = @("x64", "x86", "arm64")
    )

    $roots = @(
        "C:\Program Files (x86)\Windows Kits\10\bin",
        "C:\Program Files\Windows Kits\10\bin"
    )

    foreach ($root in $roots) {
        if (-not (Test-Path $root)) {
            continue
        }

        $versionDirs = Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending

        foreach ($versionDir in $versionDirs) {
            foreach ($arch in $Architectures) {
                $candidate = Join-Path $versionDir.FullName $arch
                $candidate = Join-Path $candidate $FileName
                if (Test-Path $candidate) {
                    return $candidate
                }
            }
        }
    }

    $command = Get-Command $FileName -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) {
        return $command.Source
    }

    throw "$FileName was not found under the Windows Kits 10 bin directory. Install the WDK/SDK tools or pass them via PATH."
}

if (-not (Test-Path $infPath)) {
    throw "Driver INF not found: $infPath"
}

$signtoolPath = Get-WindowsKitBinary -FileName "signtool.exe" -Architectures @("x64", "x86", "arm64")

if ($SkipCatalogGeneration) {
    Write-Host "[driver] skipping catalog generation"
} else {
    $inf2catPath = Get-WindowsKitBinary -FileName "Inf2Cat.exe" -Architectures @("x86", "x64", "arm64")
    Write-Host "[driver] generating catalog"
    & $inf2catPath /driver:$driverDir /os:10_X64
    if ($LASTEXITCODE -ne 0) {
        throw "Inf2Cat failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path $catPath)) {
    throw "Catalog is missing: $catPath"
}

Write-Host "[driver] signing catalog"
if ($PfxPath) {
    if (-not (Test-Path $PfxPath)) {
        throw "PFX file not found: $PfxPath"
    }

    $signArgs = @("sign", "/fd", "SHA256", "/f", $PfxPath)
    if ($PfxPassword) {
        $signArgs += @("/p", $PfxPassword)
    }
    if (-not [string]::IsNullOrWhiteSpace($TimestampUrl)) {
        $signArgs += @("/tr", $TimestampUrl, "/td", "SHA256")
    }
    $signArgs += $catPath
    & $signtoolPath @signArgs
} else {
    $signArgs = @("sign", "/fd", "SHA256", "/n", $CertSubject)
    if (-not [string]::IsNullOrWhiteSpace($TimestampUrl)) {
        $signArgs += @("/tr", $TimestampUrl, "/td", "SHA256")
    }
    $signArgs += $catPath
    & $signtoolPath @signArgs
}
if ($LASTEXITCODE -ne 0) {
    throw "signtool failed with exit code $LASTEXITCODE."
}
