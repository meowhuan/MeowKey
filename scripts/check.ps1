param(
    [string]$BuildDir = "build-check",
    [string]$HardenedBuildDir = "build-check-hardened",
    [string]$ProbeBuildDir = "build-check-probe",
    [switch]$SkipFirmware,
    [switch]$SkipDesktop
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$boardPresetsPath = Join-Path $PSScriptRoot "board-presets.json"
$guiServerPath = Join-Path $PSScriptRoot "gui_server.py"
$probeScriptPath = Join-Path $PSScriptRoot "probe-board.ps1"
$cargoManifestPath = Join-Path $projectRoot "native-rs\meowkey-manager\Cargo.toml"
$winUiProjectPath = Join-Path $projectRoot "windows\gui\MeowKey.Manager\MeowKey.Manager.csproj"

Write-Host "[check] validating board presets JSON"
$presets = Get-Content -Path $boardPresetsPath -Raw | ConvertFrom-Json
if (-not $presets.presets) {
    throw "board-presets.json must define a 'presets' object."
}

Write-Host "[check] validating gui_server.py syntax"
$tempPyc = Join-Path ([System.IO.Path]::GetTempPath()) "meowkey_gui_server.pyc"
python -c "import py_compile; py_compile.compile(r'$guiServerPath', doraise=True, cfile=r'$tempPyc')"

Write-Host "[check] validating probe-board.ps1 with sample input"
$probeSamplePath = Join-Path ([System.IO.Path]::GetTempPath()) "meowkey_probe_sample.json"
$probeTextPath = Join-Path ([System.IO.Path]::GetTempPath()) "meowkey_probe_sample.txt"
$probeBundlePath = Join-Path ([System.IO.Path]::GetTempPath()) "meowkey_probe_bundle.json"
@'
{
  "schemaVersion": 1,
  "tool": "meowkey-probe",
  "uniqueId": "00112233445566778899aabbccddeeff",
  "flashSizeBytes": 16777216,
  "gpio": {
    "pinCount": 30,
    "forcedPinCandidates": [2, 3],
    "pins": [
      {"pin": 2, "raw": false, "pullUp": false, "pullDown": false, "classification": "forced-low"},
      {"pin": 3, "raw": true, "pullUp": true, "pullDown": true, "classification": "forced-high"}
    ]
  },
  "i2c": {
    "pairs": [
      {"instance": 0, "sdaPin": 4, "sclPin": 5, "devices": ["0x50"]}
    ],
    "eepromCandidates": [
      {"instance": 0, "sdaPin": 4, "sclPin": 5, "address": "0x50", "readWith8BitOffset": true, "readWith16BitOffset": false, "addressWidth": 1, "suggestedPreset": "24c02", "rawHex": "0011223344556677"}
    ]
  }
}
'@ | Set-Content -Path $probeSamplePath -Encoding utf8
powershell -NoProfile -ExecutionPolicy Bypass -File $probeScriptPath `
    -InputPath $probeSamplePath `
    -TextOutputPath $probeTextPath `
    -OutputPath $probeBundlePath | Out-Null

if (-not (Test-Path $probeTextPath)) {
    throw "probe-board.ps1 did not create the expected text report."
}

$probeText = Get-Content -Path $probeTextPath -Raw
if ($probeText -notmatch "=== Suggested preset snippet ===" -or
    $probeText -notmatch "=== Raw probe report ===") {
    throw "probe-board.ps1 text report is missing expected sections."
}

if (-not (Test-Path $probeBundlePath)) {
    throw "probe-board.ps1 did not create the expected JSON bundle."
}

$probeBundle = Get-Content -Path $probeBundlePath -Raw | ConvertFrom-Json
if ($probeBundle.presetName -ne "custom-probe-v1") {
    throw "probe-board.ps1 JSON bundle is missing the preset name."
}

if (-not $SkipFirmware) {
    Write-Host "[check] building default firmware"
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") `
        -BuildDir $BuildDir `
        -NoPicotool `
        -IgnoreGitGlobalConfig

    Write-Host "[check] building hardened firmware"
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") `
        -BuildDir $HardenedBuildDir `
        -NoPicotool `
        -IgnoreGitGlobalConfig `
        -DisableDebugHid

    Write-Host "[check] building probe firmware"
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") `
        -BuildDir $ProbeBuildDir `
        -NoPicotool `
        -IgnoreGitGlobalConfig `
        -Probe
}

if (-not $SkipDesktop) {
    Write-Host "[check] cargo check"
    cargo check --locked --manifest-path $cargoManifestPath

    Write-Host "[check] dotnet build WinUI manager"
    dotnet build $winUiProjectPath -c Release -p:ContinuousIntegrationBuild=true
}

Write-Host "[check] all checks passed"
