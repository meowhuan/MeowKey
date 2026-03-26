param(
    [string]$Port = "",
    [string]$InputPath = "",
    [string]$OutputPath = "",
    [string]$TextOutputPath = "",
    [string]$PresetName = "custom-probe-v1",
    [string]$Board = "meowkey_rp2350_usb",
    [int]$FlashSizeMB = 16,
    [int]$CredentialCapacity = 0,
    [int]$CredentialStoreKB = 64,
    [ValidateSet("auto", "none", "gpio", "i2c-eeprom")]
    [string]$BoardIdMode = "auto",
    [string]$BoardIdGpioPins = "",
    [ValidateSet("low", "high")]
    [string]$BoardIdGpioActiveState = "low",
    [ValidateSet("auto", "24c02", "24c32", "24c64", "custom")]
    [string]$BoardIdI2cPreset = "auto",
    [int]$BoardIdI2cInstance = -1,
    [int]$BoardIdI2cSdaPin = -1,
    [int]$BoardIdI2cSclPin = -1,
    [string]$BoardIdI2cAddress = "",
    [int]$BoardIdI2cMemOffset = 0,
    [int]$BoardIdI2cMemAddressWidth = 0,
    [int]$BoardIdI2cReadLength = 0,
    [int]$TimeoutSeconds = 8
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Read-ProbeReportFromFile {
    param([string]$Path)

    $raw = Get-Content -Path $Path -Raw
    return $raw | ConvertFrom-Json
}

function Read-ProbeReportFromSerialPort {
    param(
        [string]$PortName,
        [int]$TimeoutSeconds
    )

    $serial = [System.IO.Ports.SerialPort]::new($PortName, 115200)
    $serial.NewLine = "`n"
    $serial.ReadTimeout = 500
    $serial.DtrEnable = $true
    $serial.RtsEnable = $true

    try {
        $serial.Open()
        $serial.DiscardInBuffer()

        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $collecting = $false
        $lines = New-Object System.Collections.Generic.List[string]

        while ([DateTime]::UtcNow -lt $deadline) {
            try {
                $line = $serial.ReadLine().Trim()
            } catch [System.TimeoutException] {
                continue
            }

            if ($line -eq "MEOWKEY_PROBE_JSON_BEGIN") {
                $collecting = $true
                $lines.Clear()
                continue
            }

            if ($line -eq "MEOWKEY_PROBE_JSON_END" -and $collecting) {
                if ($lines.Count -eq 0) {
                    throw "Probe payload from $PortName was empty."
                }
                return (($lines -join "`n") | ConvertFrom-Json)
            }

            if ($collecting) {
                $lines.Add($line)
            }
        }
    } finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        $serial.Dispose()
    }

    throw "Timed out waiting for a MeowKey probe report on $PortName."
}

function Resolve-ProbeReport {
    if ($InputPath) {
        return Read-ProbeReportFromFile -Path $InputPath
    }

    if ($Port) {
        return Read-ProbeReportFromSerialPort -PortName $Port -TimeoutSeconds $TimeoutSeconds
    }

    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    if (-not $ports -or $ports.Count -eq 0) {
        throw "No serial ports were found. Flash the probe firmware first, then rerun with -Port if needed."
    }

    foreach ($candidate in $ports) {
        try {
            Write-Host "[probe] trying $candidate"
            return Read-ProbeReportFromSerialPort -PortName $candidate -TimeoutSeconds $TimeoutSeconds
        } catch {
            continue
        }
    }

    throw "No serial port returned a valid MeowKey probe report. Retry with -Port to force a specific device."
}

function Parse-GpioPins {
    param([string]$PinsText)

    if ([string]::IsNullOrWhiteSpace($PinsText)) {
        return [int[]]@()
    }

    return [int[]]@(
        $PinsText.Split(",", [System.StringSplitOptions]::RemoveEmptyEntries) |
            ForEach-Object { [int]($_.Trim()) }
    )
}

function Get-ForcedPinsFromReport {
    param($Report)

    $pins = New-Object System.Collections.Generic.List[int]
    foreach ($pin in @($Report.gpio.pins)) {
        if ($pin.classification -eq "forced-high" -or $pin.classification -eq "forced-low") {
            $pins.Add([int]$pin.pin)
        }
    }
    return [int[]]@($pins)
}

function Resolve-I2cCandidate {
    param(
        $Report,
        [System.Collections.Generic.List[string]]$Notes
    )

    $candidates = @($Report.i2c.eepromCandidates)

    if ($BoardIdI2cInstance -ge 0 -or
        $BoardIdI2cSdaPin -ge 0 -or
        $BoardIdI2cSclPin -ge 0 -or
        $BoardIdI2cAddress -or
        $BoardIdI2cPreset -ne "auto" -or
        $BoardIdI2cMemAddressWidth -gt 0 -or
        $BoardIdI2cReadLength -gt 0) {
        $selected = [ordered]@{
            instance = if ($BoardIdI2cInstance -ge 0) { $BoardIdI2cInstance } elseif ($candidates.Count -gt 0) { [int]$candidates[0].instance } else { 0 }
            sdaPin = if ($BoardIdI2cSdaPin -ge 0) { $BoardIdI2cSdaPin } elseif ($candidates.Count -gt 0) { [int]$candidates[0].sdaPin } else { 4 }
            sclPin = if ($BoardIdI2cSclPin -ge 0) { $BoardIdI2cSclPin } elseif ($candidates.Count -gt 0) { [int]$candidates[0].sclPin } else { 5 }
            address = if ($BoardIdI2cAddress) { $BoardIdI2cAddress.ToLowerInvariant() } elseif ($candidates.Count -gt 0) { [string]$candidates[0].address } else { "0x50" }
            memAddressWidth = if ($BoardIdI2cMemAddressWidth -gt 0) { $BoardIdI2cMemAddressWidth } elseif ($candidates.Count -gt 0) { [int]$candidates[0].addressWidth } else { 1 }
            readLength = if ($BoardIdI2cReadLength -gt 0) { $BoardIdI2cReadLength } else { 8 }
            preset = $null
            source = if ($candidates.Count -gt 0) { "override+probe" } else { "override" }
        }

        if ($BoardIdI2cPreset -ne "auto") {
            $selected.preset = $BoardIdI2cPreset
        } elseif ($candidates.Count -gt 0 -and $candidates[0].suggestedPreset) {
            $selected.preset = [string]$candidates[0].suggestedPreset
        } elseif ($selected.memAddressWidth -eq 1) {
            $selected.preset = "24c02"
        } else {
            $selected.preset = "custom"
        }

        return $selected
    }

    if ($candidates.Count -eq 0) {
        return $null
    }

    if ($candidates.Count -gt 1) {
        $Notes.Add("Multiple I2C EEPROM/ID candidates were detected. The first candidate was chosen for the draft preset.")
    }

    $first = $candidates[0]
    return [ordered]@{
        instance = [int]$first.instance
        sdaPin = [int]$first.sdaPin
        sclPin = [int]$first.sclPin
        address = [string]$first.address
        memAddressWidth = [int]$first.addressWidth
        readLength = 8
        preset = if ($BoardIdI2cPreset -ne "auto") { $BoardIdI2cPreset } elseif ($first.suggestedPreset) { [string]$first.suggestedPreset } elseif ([int]$first.addressWidth -eq 1) { "24c02" } else { "custom" }
        source = "probe"
    }
}

function Resolve-UserPresenceDraft {
    param(
        $Report,
        [System.Collections.Generic.List[string]]$Notes
    )

    $candidates = @()
    if ($Report.PSObject.Properties.Name -contains "userPresence" -and
        $Report.userPresence -and
        $Report.userPresence.PSObject.Properties.Name -contains "gpioButtonCandidates") {
        $candidates = @($Report.userPresence.gpioButtonCandidates)
    }
    if ($candidates.Count -gt 0) {
        $candidateSummary = ($candidates | ForEach-Object {
            "GPIO $($_.pin) active-$($_.activeState)"
        }) -join ", "
        $Notes.Add("Potential UP button GPIO candidates were observed: $candidateSummary. The draft preset keeps userPresenceSource=none until you manually confirm a dedicated button.")
    } else {
        $Notes.Add("No dedicated UP button GPIO transitions were observed during the probe window. The draft preset keeps userPresenceSource=none.")
    }

    return [ordered]@{
        source = "none"
        gpioPin = -1
        gpioActiveState = "low"
        tapCount = 2
        gestureWindowMs = 750
        timeoutMs = 8000
    }
}

function Resolve-UserVerificationDraft {
    param(
        $Report,
        [System.Collections.Generic.List[string]]$Notes
    )

    $candidates = @()
    if ($Report.PSObject.Properties.Name -contains "userVerification" -and
        $Report.userVerification -and
        $Report.userVerification.PSObject.Properties.Name -contains "i2cCandidates") {
        $candidates = @($Report.userVerification.i2cCandidates)
    }
    if ($candidates.Count -gt 0) {
        $candidateSummary = ($candidates | ForEach-Object {
            "I2C$($_.instance) $($_.sdaPin)/$($_.sclPin) @$($_.address)"
        }) -join ", "
        $Notes.Add("Potential UV-related external peripherals were observed on I2C: $candidateSummary. This is only a heuristic; the draft preset keeps userVerificationSource=none until you manually confirm the module.")
    } else {
        $Notes.Add("No UV peripheral candidates were inferred automatically. The draft preset keeps userVerificationSource=none.")
    }

    return [ordered]@{
        source = "none"
        module = ""
        i2cInstance = -1
        i2cSdaPin = -1
        i2cSclPin = -1
        i2cAddress = ""
    }
}

function Resolve-PresetDraft {
    param($Report)

    $notes = New-Object System.Collections.Generic.List[string]
    $forcedPins = @(Get-ForcedPinsFromReport -Report $Report)
    $selectedGpioPins = @(Parse-GpioPins -PinsText $BoardIdGpioPins)
    $selectedI2c = Resolve-I2cCandidate -Report $Report -Notes $notes
    $userPresence = Resolve-UserPresenceDraft -Report $Report -Notes $notes
    $userVerification = Resolve-UserVerificationDraft -Report $Report -Notes $notes
    $resolvedMode = $BoardIdMode

    if ($selectedGpioPins.Count -eq 0 -and $forcedPins.Count -gt 0) {
        $selectedGpioPins = @($forcedPins | Select-Object -First 4)
        if ($forcedPins.Count -gt 4) {
            $notes.Add("More than four forced GPIO pins were detected. The draft preset only keeps the first four pins; verify the actual board ID strap pins manually.")
        }
    }

    if ($resolvedMode -eq "auto") {
        if ($selectedI2c) {
            $resolvedMode = "i2c-eeprom"
            if ($selectedGpioPins.Count -gt 0) {
                $notes.Add("Both GPIO strap candidates and I2C ID candidates were detected. The draft preset prefers I2C EEPROM/ID mode.")
            }
        } elseif ($selectedGpioPins.Count -gt 0) {
            $resolvedMode = "gpio"
        } else {
            $resolvedMode = "none"
        }
    }

    if ($resolvedMode -eq "gpio" -and $selectedGpioPins.Count -eq 0) {
        $notes.Add("GPIO mode was requested, but no forced GPIO pins were detected. The draft falls back to boardIdMode=none.")
        $resolvedMode = "none"
    }

    if ($resolvedMode -eq "i2c-eeprom" -and -not $selectedI2c) {
        $notes.Add("I2C EEPROM mode was requested, but no EEPROM/ID candidate was detected. The draft falls back to boardIdMode=none.")
        $resolvedMode = "none"
    }

    if ($selectedI2c -and $selectedI2c.memAddressWidth -eq 2 -and $selectedI2c.preset -eq "custom") {
        $notes.Add("The probe detected a 16-bit EEPROM offset width. The draft keeps boardIdI2cPreset=custom; replace it with 24c32/24c64 if you know the exact chip.")
    }

    $preset = [ordered]@{
        description = "Generated from MeowKey probe on $(Get-Date -Format "yyyy-MM-ddTHH:mm:ssK")"
        board = $Board
        flashSizeMB = $FlashSizeMB
        credentialCapacity = $CredentialCapacity
        credentialStoreKB = $CredentialStoreKB
        userPresenceSource = [string]$userPresence.source
        userPresenceGpioPin = [int]$userPresence.gpioPin
        userPresenceGpioActiveState = [string]$userPresence.gpioActiveState
        userPresenceTapCount = [int]$userPresence.tapCount
        userPresenceGestureWindowMs = [int]$userPresence.gestureWindowMs
        userPresenceTimeoutMs = [int]$userPresence.timeoutMs
        userVerificationSource = [string]$userVerification.source
        userVerificationModule = [string]$userVerification.module
        userVerificationI2cInstance = [int]$userVerification.i2cInstance
        userVerificationI2cSdaPin = [int]$userVerification.i2cSdaPin
        userVerificationI2cSclPin = [int]$userVerification.i2cSclPin
        userVerificationI2cAddress = [string]$userVerification.i2cAddress
        boardIdMode = $resolvedMode
        boardIdGpioPins = ""
        boardIdGpioActiveState = $BoardIdGpioActiveState
        boardIdI2cPreset = "24c02"
        boardIdI2cInstance = 0
        boardIdI2cSdaPin = 4
        boardIdI2cSclPin = 5
        boardIdI2cAddress = "0x50"
        boardIdI2cMemOffset = $BoardIdI2cMemOffset
        boardIdI2cMemAddressWidth = 1
        boardIdI2cReadLength = 8
    }

    switch ($resolvedMode) {
        "gpio" {
            $preset.boardIdGpioPins = ($selectedGpioPins -join ",")
            if ($selectedGpioPins.Count -eq 0) {
                $notes.Add("No GPIO pins were selected for the draft preset.")
            }
        }

        "i2c-eeprom" {
            $preset.boardIdI2cPreset = [string]$selectedI2c.preset
            $preset.boardIdI2cInstance = [int]$selectedI2c.instance
            $preset.boardIdI2cSdaPin = [int]$selectedI2c.sdaPin
            $preset.boardIdI2cSclPin = [int]$selectedI2c.sclPin
            $preset.boardIdI2cAddress = [string]$selectedI2c.address
            $preset.boardIdI2cMemAddressWidth = [int]$selectedI2c.memAddressWidth
            $preset.boardIdI2cReadLength = [int]$selectedI2c.readLength
        }

        Default {
            $notes.Add("No reliable board ID source was inferred. The draft preset keeps boardIdMode=none.")
        }
    }

    return [ordered]@{
        resolvedMode = $resolvedMode
        notes = @($notes)
        preset = $preset
        presetSnippet = [ordered]@{
            $PresetName = $preset
        }
    }
}

function ConvertTo-PrettyJson {
    param($Value)

    return ($Value | ConvertTo-Json -Depth 16)
}

function New-ProbeTextReport {
    param(
        $Draft,
        $Report
    )

    $newline = [System.Environment]::NewLine
    $notesText = if ($Draft.notes.Count -eq 0) {
        "- no extra notes"
    } else {
        ($Draft.notes | ForEach-Object { "- $_" }) -join $newline
    }

    $sections = @(
        "=== Suggested preset snippet ===$newline$(ConvertTo-PrettyJson -Value $Draft.presetSnippet)"
        "=== Notes ===$newline$notesText"
        "=== Raw probe report ===$newline$(ConvertTo-PrettyJson -Value $Report)"
    )

    return ($sections -join ($newline + $newline))
}

$report = Resolve-ProbeReport
if (-not $report.schemaVersion -or [int]$report.schemaVersion -lt 1) {
    throw "The probe report is missing a supported schemaVersion."
}

$draft = Resolve-PresetDraft -Report $report
$result = [ordered]@{
    presetName = $PresetName
    resolvedMode = $draft.resolvedMode
    notes = $draft.notes
    preset = $draft.preset
    presetSnippet = $draft.presetSnippet
    report = $report
}

$textReport = New-ProbeTextReport -Draft $draft -Report $report
Write-Output $textReport

if ($TextOutputPath) {
    $textReport | Set-Content -Path $TextOutputPath -Encoding utf8
    Write-Host ""
    Write-Host "Saved text report to $TextOutputPath"
}

if ($OutputPath) {
    $result | ConvertTo-Json -Depth 16 | Set-Content -Path $OutputPath -Encoding utf8
    Write-Host ""
    Write-Host "Saved probe bundle to $OutputPath"
}
