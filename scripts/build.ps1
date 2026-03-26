param(
    [string]$BuildDir = "build",
    [string]$Board = "meowkey_rp2350_usb",
    [string]$Preset = "",
    [int]$FlashSizeMB = 16,
    [int]$CredentialCapacity = 0,
    [int]$CredentialStoreKB = 64,
    [switch]$DisableDebugHid,
    [switch]$Probe,
    [ValidateSet("none", "gpio", "i2c-eeprom")]
    [string]$BoardIdMode = "none",
    [string]$BoardIdGpioPins = "",
    [ValidateSet("low", "high")]
    [string]$BoardIdGpioActiveState = "low",
    [ValidateSet("24c02", "24c32", "24c64", "custom")]
    [string]$BoardIdI2cPreset = "24c02",
    [int]$BoardIdI2cInstance = 0,
    [int]$BoardIdI2cSdaPin = 4,
    [int]$BoardIdI2cSclPin = 5,
    [string]$BoardIdI2cAddress = "",
    [int]$BoardIdI2cMemOffset = 0,
    [int]$BoardIdI2cMemAddressWidth = 1,
    [int]$BoardIdI2cReadLength = 8,
    [string]$ToolchainRoot = "",
    [string]$NinjaPath = "",
    [string]$LogPath = "",
    [string]$HostCc = "",
    [string]$HostCxx = "",
    [string]$HostAr = "",
    [string]$HostRanlib = "",
    [int]$VersionMajor = 0,
    [int]$VersionMinor = 1,
    [int]$VersionPatch = 0,
    [string]$VersionLabel = "dev",
    [string]$PicotoolFetchPath = "",
    [switch]$NoPicotool,
    [switch]$IgnoreGitGlobalConfig,
    [switch]$Clean
)

function Set-FromPreset {
    param(
        [string]$ParameterName,
        $PresetValue
    )

    if (-not $PSBoundParameters.ContainsKey($ParameterName) -and $null -ne $PresetValue) {
        Set-Variable -Name $ParameterName -Value $PresetValue -Scope Script
    }
}

$presetPath = Join-Path $PSScriptRoot "board-presets.json"
if ($Preset) {
    if (-not (Test-Path $presetPath)) {
        throw "Preset file was not found: $presetPath"
    }

    $presetFile = Get-Content -Path $presetPath -Raw | ConvertFrom-Json
    $presetConfig = $presetFile.presets.$Preset
    if (-not $presetConfig) {
        $availablePresets = @($presetFile.presets.PSObject.Properties.Name) -join ", "
        throw "Unknown preset '$Preset'. Available presets: $availablePresets"
    }

    Set-FromPreset -ParameterName "Board" -PresetValue ([string]$presetConfig.board)
    Set-FromPreset -ParameterName "FlashSizeMB" -PresetValue ([int]$presetConfig.flashSizeMB)
    Set-FromPreset -ParameterName "CredentialCapacity" -PresetValue ([int]$presetConfig.credentialCapacity)
    Set-FromPreset -ParameterName "CredentialStoreKB" -PresetValue ([int]$presetConfig.credentialStoreKB)
    Set-FromPreset -ParameterName "BoardIdMode" -PresetValue ([string]$presetConfig.boardIdMode)
    Set-FromPreset -ParameterName "BoardIdGpioPins" -PresetValue ([string]$presetConfig.boardIdGpioPins)
    Set-FromPreset -ParameterName "BoardIdGpioActiveState" -PresetValue ([string]$presetConfig.boardIdGpioActiveState)
    Set-FromPreset -ParameterName "BoardIdI2cPreset" -PresetValue ([string]$presetConfig.boardIdI2cPreset)
    Set-FromPreset -ParameterName "BoardIdI2cInstance" -PresetValue ([int]$presetConfig.boardIdI2cInstance)
    Set-FromPreset -ParameterName "BoardIdI2cSdaPin" -PresetValue ([int]$presetConfig.boardIdI2cSdaPin)
    Set-FromPreset -ParameterName "BoardIdI2cSclPin" -PresetValue ([int]$presetConfig.boardIdI2cSclPin)
    Set-FromPreset -ParameterName "BoardIdI2cAddress" -PresetValue ([string]$presetConfig.boardIdI2cAddress)
    Set-FromPreset -ParameterName "BoardIdI2cMemOffset" -PresetValue ([int]$presetConfig.boardIdI2cMemOffset)
    Set-FromPreset -ParameterName "BoardIdI2cMemAddressWidth" -PresetValue ([int]$presetConfig.boardIdI2cMemAddressWidth)
    Set-FromPreset -ParameterName "BoardIdI2cReadLength" -PresetValue ([int]$presetConfig.boardIdI2cReadLength)
}

if ($FlashSizeMB -le 0) {
    throw "FlashSizeMB must be greater than 0"
}

if ($CredentialCapacity -lt 0) {
    throw "CredentialCapacity must be 0 or greater"
}

if ($CredentialStoreKB -le 0 -or ($CredentialStoreKB % 4) -ne 0) {
    throw "CredentialStoreKB must be a positive multiple of 4"
}

if ($CredentialStoreKB -lt 12) {
    throw "CredentialStoreKB must be at least 12 KB because MeowKey reserves 8 KB for the sign-count journal."
}

if ($VersionMajor -lt 0 -or $VersionMinor -lt 0 -or $VersionPatch -lt 0) {
    throw "VersionMajor, VersionMinor, and VersionPatch must be 0 or greater"
}

if ($BoardIdI2cMemAddressWidth -lt 1 -or $BoardIdI2cMemAddressWidth -gt 2) {
    throw "BoardIdI2cMemAddressWidth must be 1 or 2"
}

if ($BoardIdI2cReadLength -lt 1 -or $BoardIdI2cReadLength -gt 16) {
    throw "BoardIdI2cReadLength must be between 1 and 16"
}

$boardIdModeValue = $BoardIdMode.ToLowerInvariant()
$boardIdI2cPresetValue = $BoardIdI2cPreset.ToLowerInvariant()

switch ($boardIdI2cPresetValue) {
    "24c02" {
        if (-not $BoardIdI2cAddress) {
            $BoardIdI2cAddress = "0x50"
        }
        $BoardIdI2cMemAddressWidth = 1
        if ($BoardIdI2cReadLength -eq 8) {
            $BoardIdI2cReadLength = 8
        }
    }
    "24c32" {
        if (-not $BoardIdI2cAddress) {
            $BoardIdI2cAddress = "0x50"
        }
        $BoardIdI2cMemAddressWidth = 2
    }
    "24c64" {
        if (-not $BoardIdI2cAddress) {
            $BoardIdI2cAddress = "0x50"
        }
        $BoardIdI2cMemAddressWidth = 2
    }
    "custom" {
        if (-not $BoardIdI2cAddress) {
            throw "BoardIdI2cAddress is required when BoardIdI2cPreset=custom"
        }
    }
}

if (-not $BoardIdI2cAddress) {
    $BoardIdI2cAddress = "0x50"
}

if ($boardIdModeValue -eq "gpio" -and [string]::IsNullOrWhiteSpace($BoardIdGpioPins)) {
    throw "BoardIdGpioPins is required when BoardIdMode=gpio"
}

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildPath = Join-Path $projectRoot $BuildDir

if (-not $NoPicotool -and -not $PicotoolFetchPath) {
    $PicotoolFetchPath = Join-Path $projectRoot ".cache\\picotool"
}

if ($Clean -and (Test-Path $buildPath)) {
    Remove-Item -Recurse -Force $buildPath
}

if (-not $ToolchainRoot) {
    $ToolchainRoot = Join-Path $projectRoot "tools"
}

if (-not (Test-Path (Join-Path $ToolchainRoot "bin\\arm-none-eabi-gcc.exe"))) {
    throw "arm-none-eabi-gcc.exe was not found under $ToolchainRoot"
}

if (-not $NinjaPath) {
    $vsNinja = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    if (Test-Path $vsNinja) {
        $NinjaPath = $vsNinja
    } else {
        $ninjaCommand = Get-Command ninja -ErrorAction SilentlyContinue
        if ($ninjaCommand) {
            $NinjaPath = $ninjaCommand.Source
        }
    }
}

if (-not $NinjaPath) {
    throw "ninja.exe was not found. Pass -NinjaPath explicitly."
}

$env:PICO_TOOLCHAIN_PATH = $ToolchainRoot

if ($IgnoreGitGlobalConfig) {
    $env:GIT_CONFIG_GLOBAL = "NUL"
}

if (-not $LogPath) {
    $LogPath = Join-Path $buildPath "build.log"
}

New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
if (-not $NoPicotool -and $PicotoolFetchPath) {
    New-Item -ItemType Directory -Force -Path $PicotoolFetchPath | Out-Null
    $env:PICOTOOL_FETCH_FROM_GIT_PATH = $PicotoolFetchPath
}
if (Test-Path $LogPath) {
    Remove-Item $LogPath -Force
}

if (-not $HostCc -or -not $HostCxx -or -not $HostAr -or -not $HostRanlib) {
    $zigCommand = Get-Command zig -ErrorAction SilentlyContinue
    if ($zigCommand) {
        $HostCc = Join-Path $ToolchainRoot "zig-cc.cmd"
        $HostCxx = Join-Path $ToolchainRoot "zig-cxx.cmd"
        $HostAr = Join-Path $ToolchainRoot "zig-ar.cmd"
        $HostRanlib = Join-Path $ToolchainRoot "zig-ranlib.cmd"
        $HostLlvmAr = Join-Path $ToolchainRoot "llvm-ar.cmd"
        $HostLlvmRanlib = Join-Path $ToolchainRoot "llvm-ranlib.cmd"
        $HostArCompat = Join-Path $ToolchainRoot "ar.cmd"
        $HostRanlibCompat = Join-Path $ToolchainRoot "ranlib.cmd"

        Set-Content -Path $HostCc -Encoding ascii -NoNewline -Value "@echo off`r`n`"$($zigCommand.Source)`" cc %*`r`n"
        Set-Content -Path $HostCxx -Encoding ascii -NoNewline -Value "@echo off`r`n`"$($zigCommand.Source)`" c++ %*`r`n"
        Set-Content -Path $HostAr -Encoding ascii -NoNewline -Value "@echo off`r`n`"$($zigCommand.Source)`" ar %*`r`n"
        Set-Content -Path $HostRanlib -Encoding ascii -NoNewline -Value "@echo off`r`n`"$($zigCommand.Source)`" ranlib %*`r`n"
        Copy-Item -Force $HostAr $HostLlvmAr
        Copy-Item -Force $HostRanlib $HostLlvmRanlib
        Copy-Item -Force $HostAr $HostArCompat
        Copy-Item -Force $HostRanlib $HostRanlibCompat
    }
}

if ($HostCc) {
    $env:PICOTOOL_HOST_CC = $HostCc
}

if ($HostCxx) {
    $env:PICOTOOL_HOST_CXX = $HostCxx
}

if ($HostAr) {
    $env:PICOTOOL_HOST_AR = $HostAr
}

if ($HostRanlib) {
    $env:PICOTOOL_HOST_RANLIB = $HostRanlib
}

$cmakeArgs = @(
    "-S", "$projectRoot",
    "-B", "$buildPath",
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$NinjaPath",
    "-DPICO_BOARD=$Board",
    "-DPICO_NO_PICOTOOL=$(if ($NoPicotool) { 'ON' } else { 'OFF' })",
    "-DPICO_FLASH_SIZE_BYTES=$([int64]$FlashSizeMB * 1MB)",
    "-DMEOWKEY_CREDENTIAL_CAPACITY=$CredentialCapacity",
    "-DMEOWKEY_CREDENTIAL_STORE_SECTORS=$([int]($CredentialStoreKB / 4))",
    "-DMEOWKEY_ENABLE_DEBUG_HID=$(if ($DisableDebugHid) { 'OFF' } else { 'ON' })",
    "-DMEOWKEY_BOARD_ID_MODE=$boardIdModeValue",
    "-DMEOWKEY_BOARD_ID_GPIO_PINS=$BoardIdGpioPins",
    "-DMEOWKEY_BOARD_ID_GPIO_ACTIVE_LOW=$(if ($BoardIdGpioActiveState -eq 'low') { 'ON' } else { 'OFF' })",
    "-DMEOWKEY_BOARD_ID_I2C_PRESET=$boardIdI2cPresetValue",
    "-DMEOWKEY_BOARD_ID_I2C_INSTANCE=$BoardIdI2cInstance",
    "-DMEOWKEY_BOARD_ID_I2C_SDA_PIN=$BoardIdI2cSdaPin",
    "-DMEOWKEY_BOARD_ID_I2C_SCL_PIN=$BoardIdI2cSclPin",
    "-DMEOWKEY_BOARD_ID_I2C_ADDRESS=$BoardIdI2cAddress",
    "-DMEOWKEY_BOARD_ID_I2C_MEM_OFFSET=$BoardIdI2cMemOffset",
    "-DMEOWKEY_BOARD_ID_I2C_MEM_ADDRESS_WIDTH=$BoardIdI2cMemAddressWidth",
    "-DMEOWKEY_BOARD_ID_I2C_READ_LENGTH=$BoardIdI2cReadLength",
    "-DMEOWKEY_VERSION_MAJOR=$VersionMajor",
    "-DMEOWKEY_VERSION_MINOR=$VersionMinor",
    "-DMEOWKEY_VERSION_PATCH=$VersionPatch",
    "-DMEOWKEY_VERSION_LABEL=$VersionLabel"
)

if (-not $NoPicotool -and $PicotoolFetchPath) {
    $cmakeArgs += "-DPICOTOOL_FETCH_FROM_GIT_PATH=$PicotoolFetchPath"
}

if ($NoPicotool) {
    Write-Warning "PICO_NO_PICOTOOL=ON: UF2 output will not be generated."
}

& cmake @cmakeArgs 2>&1 | Tee-Object -FilePath $LogPath -Append
$configureExit = $LASTEXITCODE
if ($configureExit -ne 0) {
    exit $configureExit
}

if ($HostCc) {
    $env:CC = $HostCc
}

if ($HostCxx) {
    $env:CXX = $HostCxx
}

if ($HostAr) {
    $env:AR = $HostAr
}

if ($HostRanlib) {
    $env:RANLIB = $HostRanlib
}

$env:PATH = "$ToolchainRoot;$env:PATH"

$buildCommand = @("--build", "$buildPath")
if ($Probe) {
    $buildCommand += @("--target", "meowkey_probe")
}

& cmake @buildCommand 2>&1 | Tee-Object -FilePath $LogPath -Append
exit $LASTEXITCODE
