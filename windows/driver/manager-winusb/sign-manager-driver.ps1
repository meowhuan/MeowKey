param(
    [string]$CertSubject = "CN=MeowKey Manager Driver Test",
    [string]$PfxPath = "",
    [string]$PfxPassword = "",
    [string]$TimestampUrl = "",
    [string]$ExportSignerCertificatePath = "",
    [switch]$UseMakeCatFallback,
    [switch]$AllowUntrustedRootVerification,
    [switch]$SkipCatalogVerification,
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
    $inf2catPath = $null
    try {
        $inf2catPath = Get-WindowsKitBinary -FileName "Inf2Cat.exe" -Architectures @("x86", "x64", "arm64")
    } catch {
        if (-not $UseMakeCatFallback) {
            throw
        }
        Write-Host "[driver] Inf2Cat.exe not found, attempting MakeCat fallback"
    }

    if (Test-Path $catPath) {
        Remove-Item -Force $catPath
    }

    if ($inf2catPath) {
        Write-Host "[driver] generating catalog via Inf2Cat"
        & $inf2catPath /driver:$driverDir /os:10_X64
        if ($LASTEXITCODE -ne 0) {
            throw "Inf2Cat failed with exit code $LASTEXITCODE."
        }
    } else {
        $makecatPath = Get-WindowsKitBinary -FileName "MakeCat.exe" -Architectures @("x86", "x64", "arm64")
        $cdfPath = Join-Path $driverDir "meowkey-manager-winusb.cdf"
        $cdfContent = @"
[CatalogHeader]
Name=meowkey-manager-winusb.cat
ResultDir=.
PublicVersion=0x0000001
EncodingType=0x00010001
CATATTR1=0x10010001:OSAttr:2:10.0

[CatalogFiles]
<hash>meowkey-manager-winusb.inf=meowkey-manager-winusb.inf
"@
        Set-Content -Path $cdfPath -Value $cdfContent -Encoding ASCII
        try {
            Push-Location $driverDir
            & $makecatPath /v meowkey-manager-winusb.cdf
            if ($LASTEXITCODE -ne 0) {
                throw "MakeCat failed with exit code $LASTEXITCODE."
            }
        } finally {
            Pop-Location
            Remove-Item -Force $cdfPath -ErrorAction SilentlyContinue
        }
        Write-Warning "[driver] catalog generated via MakeCat fallback; Inf2Cat is preferred for INF driver packages."
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

    if (-not [string]::IsNullOrWhiteSpace($ExportSignerCertificatePath)) {
        $flags = [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::Exportable
        $signingCert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($PfxPath, $PfxPassword, $flags)
        $signerCerBytes = $signingCert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)
        [System.IO.File]::WriteAllBytes($ExportSignerCertificatePath, $signerCerBytes)
        Write-Host "[driver] exported signer certificate: $ExportSignerCertificatePath"
    }
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

if ($SkipCatalogVerification) {
    Write-Host "[driver] skipping post-sign catalog verification"
} else {
    function Test-InfHashMembershipInCatalog {
        param(
            [Parameter(Mandatory = $true)]
            [string]$CatalogPath,
            [Parameter(Mandatory = $true)]
            [string]$InfFilePath
        )

        $certutilPath = Join-Path $env:WINDIR "System32\certutil.exe"
        if (-not (Test-Path $certutilPath)) {
            throw "certutil.exe not found at expected path: $certutilPath"
        }

        $infSha256 = (Get-FileHash -Path $InfFilePath -Algorithm SHA256).Hash.ToLowerInvariant()
        $infSha1 = (Get-FileHash -Path $InfFilePath -Algorithm SHA1).Hash.ToLowerInvariant()
        $dump = & $certutilPath -dump $CatalogPath 2>&1
        $dumpText = ($dump | ForEach-Object { $_.ToString() }) -join "`n"
        $dumpText = $dumpText.ToLowerInvariant()
        $dumpHexOnly = [regex]::Replace($dumpText, '[^0-9a-f]', '')
        return $dumpHexOnly.Contains($infSha256) -or $dumpHexOnly.Contains($infSha1)
    }

    function Invoke-SignToolVerify {
        param(
            [string[]]$VerifyArgs,
            [string]$Context,
            [switch]$CheckInfMembershipOnUntrustedRoot
        )

        $previousErrorActionPreference = $ErrorActionPreference
        $hasNativePref = $null -ne (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue)
        $previousNativePref = $null
        if ($hasNativePref) {
            $previousNativePref = $PSNativeCommandUseErrorActionPreference
        }

        try {
            # Prevent signtool stderr lines from being raised as terminating PowerShell errors.
            $ErrorActionPreference = "Continue"
            if ($hasNativePref) {
                $PSNativeCommandUseErrorActionPreference = $false
            }
            $verifyOutput = & $signtoolPath @VerifyArgs 2>&1
            $verifyExitCode = $LASTEXITCODE
        } finally {
            if ($hasNativePref) {
                $PSNativeCommandUseErrorActionPreference = $previousNativePref
            }
            $ErrorActionPreference = $previousErrorActionPreference
        }

        $verifyOutput | ForEach-Object { Write-Host $_ }
        if ($verifyExitCode -eq 0) {
            return
        }

        $verifyText = ($verifyOutput | ForEach-Object { $_.ToString() }) -join "`n"
        $hasUntrustedRoot = $verifyText -match 'not trusted by the trust provider|CERT_E_UNTRUSTEDROOT|0x800B0109|terminated in a root certificate'
        $hasCatalogHashMismatch = $verifyText -match 'hash value.*catalog|not in the specified catalog|specified catalog file|catalog.*invalid|TRUST_E_BAD_DIGEST|0x80096010|file not valid'

        if ($AllowUntrustedRootVerification -and $hasUntrustedRoot) {
            if ($CheckInfMembershipOnUntrustedRoot) {
                $inCatalog = Test-InfHashMembershipInCatalog -CatalogPath $catPath -InfFilePath $infPath
                if ($inCatalog) {
                    Write-Warning "[driver] $Context verification reported an untrusted root on this runner. Hash membership confirmed via certutil dump; continuing due to -AllowUntrustedRootVerification."
                    return
                }
                throw "Catalog hash membership check failed: $infPath hash is not present in $catPath."
            } elseif (-not $hasCatalogHashMismatch) {
                Write-Warning "[driver] $Context verification reported an untrusted root on this runner. Continuing due to -AllowUntrustedRootVerification. Target machines must trust the signer certificate."
                return
            }
        }

        throw "signtool verify failed for $Context with exit code $verifyExitCode."
    }

    Write-Host "[driver] verifying catalog signature"
    Invoke-SignToolVerify -VerifyArgs @("verify", "/v", "/pa", $catPath) -Context "catalog signature"

    Write-Host "[driver] verifying INF hash membership in catalog"
    Invoke-SignToolVerify -VerifyArgs @("verify", "/v", "/pa", "/c", $catPath, $infPath) -Context "INF catalog membership" -CheckInfMembershipOnUntrustedRoot
}
