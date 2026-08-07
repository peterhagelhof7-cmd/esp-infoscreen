#requires -Version 5.1
<#
.SYNOPSIS
    Baut und flasht die esp-infoscreen-Firmware auf das ESP32-8048S070.

.DESCRIPTION
    Wrapper um PlatformIO. Baut das Environment esp32-8048S070N, flasht per
    nativem USB-Serial-JTAG (der ESP32-S3 meldet sich als VID 303A / PID 1001)
    und oeffnet auf Wunsch den seriellen Monitor.

    Board am USB-C-Port ("USB", nicht den reinen Strom-Port) anschliessen.
    Der Upload-Port wird von PlatformIO automatisch erkannt; mit -Port ist er
    ueberschreibbar (z. B. "COM7"), falls mehrere Geraete angeschlossen sind.

.PARAMETER Port
    Optionaler COM-Port (sonst Auto-Erkennung durch PlatformIO).

.PARAMETER Monitor
    Nach dem Flashen den seriellen Monitor (115200) oeffnen.

.PARAMETER SkipBuild
    Nicht neu bauen, nur den vorhandenen Build flashen.

.PARAMETER Erase
    Vor dem Flashen den kompletten Flash loeschen (Werksreset/Recovery).

.PARAMETER MonitorOnly
    Nur den seriellen Monitor oeffnen, nicht bauen/flashen.

.EXAMPLE
    .\Flash.ps1
    .\Flash.ps1 -Monitor
    .\Flash.ps1 -Port COM7 -Monitor
    .\Flash.ps1 -Erase -Monitor
    .\Flash.ps1 -MonitorOnly
#>
param(
    [string]$Port,
    [switch]$Monitor,
    [switch]$SkipBuild,
    [switch]$Erase,
    [switch]$MonitorOnly
)

$ErrorActionPreference = 'Stop'

# --- PlatformIO finden -------------------------------------------------------
$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'
if (-not (Test-Path $pio)) {
    $cmd = Get-Command platformio -ErrorAction SilentlyContinue
    if ($cmd) { $pio = $cmd.Source }
    else {
        Write-Host 'PlatformIO nicht gefunden (weder unter %USERPROFILE%\.platformio noch im PATH).' -ForegroundColor Red
        Write-Host 'Installiere die PlatformIO Core CLI oder die VS Code-Erweiterung.' -ForegroundColor Yellow
        exit 1
    }
}

# --- Ins firmware/-Verzeichnis wechseln (dieses Skript liegt in tools/) ------
$firmwareDir = Resolve-Path (Join-Path $PSScriptRoot '..\firmware')
Set-Location $firmwareDir
$env = 'esp32-8048S070N'

function Invoke-Pio([string[]]$PioArgs, [string]$What) {
    Write-Host ">> $What" -ForegroundColor Cyan
    & $pio @PioArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FEHLGESCHLAGEN: $What (Exit $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# Port-Argumente (nur wenn -Port gesetzt)
$portArgs = @()
if ($Port) { $portArgs = @('--upload-port', $Port) }
$monPortArgs = @()
if ($Port) { $monPortArgs = @('--port', $Port) }

# --- Nur-Monitor-Modus -------------------------------------------------------
if ($MonitorOnly) {
    Write-Host 'Serieller Monitor (Strg+C zum Beenden) ...' -ForegroundColor Green
    & $pio device monitor -e $env @monPortArgs
    exit $LASTEXITCODE
}

# --- Optional: Flash loeschen ------------------------------------------------
if ($Erase) {
    Invoke-Pio (@('run', '-e', $env, '-t', 'erase') + $portArgs) 'Flash loeschen (erase)'
}

# --- Bauen -------------------------------------------------------------------
if (-not $SkipBuild) {
    Invoke-Pio @('run', '-e', $env) 'Firmware bauen'
}

# --- Flashen -----------------------------------------------------------------
Invoke-Pio (@('run', '-e', $env, '-t', 'upload') + $portArgs) 'Firmware flashen'

Write-Host 'Fertig geflasht.' -ForegroundColor Green

# --- Optional: Monitor -------------------------------------------------------
if ($Monitor) {
    Write-Host 'Serieller Monitor (Strg+C zum Beenden) ...' -ForegroundColor Green
    & $pio device monitor -e $env @monPortArgs
}
