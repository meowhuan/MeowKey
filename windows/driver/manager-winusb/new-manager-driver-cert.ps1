param(
    [string]$Subject = "CN=MeowKey Manager Driver Test",
    [string]$CertPath = "meowkey-manager-driver-test.cer",
    [switch]$InstallToTrustedRoots
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$driverDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$certOutputPath = Join-Path $driverDir $CertPath

$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject $Subject `
    -KeyAlgorithm RSA `
    -KeyLength 2048 `
    -HashAlgorithm SHA256 `
    -CertStoreLocation "Cert:\CurrentUser\My"

Export-Certificate -Cert $cert -FilePath $certOutputPath | Out-Null

if ($InstallToTrustedRoots) {
    Import-Certificate -FilePath $certOutputPath -CertStoreLocation "Cert:\CurrentUser\Root" | Out-Null
    Import-Certificate -FilePath $certOutputPath -CertStoreLocation "Cert:\CurrentUser\TrustedPublisher" | Out-Null
}

Write-Host "[driver] certificate created: $certOutputPath"
Write-Host "[driver] thumbprint: $($cert.Thumbprint)"
