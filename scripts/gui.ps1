param(
    [string]$BindHost = "127.0.0.1",
    [int]$Port = 8765
)

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
& python (Join-Path $projectRoot "scripts\gui_server.py") --host $BindHost --port $Port
