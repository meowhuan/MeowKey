param(
    [switch]$SkipCertificateImport
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$driverDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$infPath = Join-Path $driverDir "meowkey-manager-winusb.inf"
$cerPath = Join-Path $driverDir "meowkey-manager-winusb.cer"

if (-not (Test-Path $infPath)) {
    throw "Driver INF not found: $infPath"
}

$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    throw "Run this script in an elevated PowerShell window (Run as Administrator)."
}

if ((Test-Path $cerPath) -and (-not $SkipCertificateImport)) {
    Write-Host "[driver] importing signer certificate into Root and TrustedPublisher: $cerPath"
    & certutil.exe -addstore -f Root $cerPath
    if ($LASTEXITCODE -ne 0) {
        throw "certutil failed to import certificate into LocalMachine\\Root (exit code $LASTEXITCODE)."
    }
    & certutil.exe -addstore -f TrustedPublisher $cerPath
    if ($LASTEXITCODE -ne 0) {
        throw "certutil failed to import certificate into LocalMachine\\TrustedPublisher (exit code $LASTEXITCODE)."
    }
} elseif (-not (Test-Path $cerPath)) {
    Write-Host "[driver] signer certificate not bundled; skipping certificate import"
}

Write-Host "[driver] installing $infPath"
pnputil /add-driver $infPath /install
