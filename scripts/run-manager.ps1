param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$projectPath = Join-Path $projectRoot "windows\gui\MeowKey.Manager\MeowKey.Manager.csproj"

if (-not (Test-Path $projectPath)) {
    throw "WinUI manager project not found: $projectPath"
}

dotnet run --project $projectPath -c $Configuration
