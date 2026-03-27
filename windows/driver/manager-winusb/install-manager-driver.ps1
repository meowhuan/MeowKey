param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$driverDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$infPath = Join-Path $driverDir "meowkey-manager-winusb.inf"

if (-not (Test-Path $infPath)) {
    throw "Driver INF not found: $infPath"
}

Write-Host "[driver] installing $infPath"
pnputil /add-driver $infPath /install
