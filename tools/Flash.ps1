#requires -Version 5.1
<#
.SYNOPSIS
    Baut und flasht die esp-infoscreen-Firmware auf das ESP32-8048S070 und
    installiert dabei bei Bedarf alle Abhaengigkeiten.

.DESCRIPTION
    Wrapper um PlatformIO. Sorgt selbststaendig dafuer, dass alles vorhanden ist:
      1. PlatformIO Core (wird per offiziellem Installer nachinstalliert, wenn es
         fehlt - benoetigt Python 3).
      2. Plattform espressif32 + ESP-IDF-Toolchain + deklarierte Bibliotheken
         (via `pio pkg install`).
      3. ESP-IDF-Managed-Components (lvgl, esp_lvgl_port) - werden beim ersten
         Build vom IDF-Component-Manager geladen.
    Danach: bauen, flashen (nativer USB-Serial-JTAG, VID 303A / PID 1001) und auf
    Wunsch den seriellen Monitor oeffnen.

    Board am USB-C-Port ("USB", nicht den reinen Strom-Port) anschliessen. Der
    Upload-Port wird automatisch erkannt; mit -Port ueberschreibbar.

.PARAMETER Port         Optionaler COM-Port (sonst Auto-Erkennung).
.PARAMETER Monitor      Nach dem Flashen den seriellen Monitor (115200) oeffnen.
.PARAMETER SkipBuild    Nicht neu bauen, nur flashen.
.PARAMETER SkipDeps     Abhaengigkeits-Check/-Installation ueberspringen (schneller).
.PARAMETER Erase        Vor dem Flashen den kompletten Flash loeschen (Recovery).
.PARAMETER MonitorOnly  Nur den seriellen Monitor oeffnen.

.EXAMPLE
    .\Flash.ps1 -Monitor
    .\Flash.ps1 -Port COM7 -Monitor
    .\Flash.ps1 -Erase -Monitor
    .\Flash.ps1 -SkipDeps            # wenn alles schon installiert ist
#>
param(
    [string]$Port,
    [switch]$Monitor,
    [switch]$SkipBuild,
    [switch]$SkipDeps,
    [switch]$Erase,
    [switch]$MonitorOnly
)

$ErrorActionPreference = 'Stop'

# --- PlatformIO finden, sonst installieren -----------------------------------
function Get-PlatformIO {
    # 1. Standardpfad
    $penv = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'
    if (Test-Path $penv) { return $penv }
    # 2. im PATH
    $cmd = Get-Command platformio -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Install-PlatformIO {
    Write-Host 'PlatformIO Core nicht gefunden - installiere es ...' -ForegroundColor Yellow

    # Python 3 suchen (fuer den offiziellen Installer noetig)
    $python = $null
    foreach ($cand in @('py', 'python', 'python3')) {
        $c = Get-Command $cand -ErrorAction SilentlyContinue
        if ($c) {
            # 'py' braucht -3; bei python/python3 direkt
            if ($cand -eq 'py') { $python = @($c.Source, '-3') } else { $python = @($c.Source) }
            break
        }
    }
    if (-not $python) {
        Write-Host 'Kein Python 3 gefunden. Bitte Python 3 installieren (https://www.python.org/downloads/)' -ForegroundColor Red
        Write-Host 'oder PlatformIO manuell einrichten (https://platformio.org/install).' -ForegroundColor Red
        exit 1
    }

    $installer = Join-Path $env:TEMP 'get-platformio.py'
    $url = 'https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py'
    Write-Host "  Lade Installer: $url" -ForegroundColor DarkGray
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing

    Write-Host '  Fuehre PlatformIO-Installer aus (kann einige Minuten dauern) ...' -ForegroundColor DarkGray
    & $python[0] @($python[1..($python.Count-1)]) $installer
    if ($LASTEXITCODE -ne 0) {
        Write-Host "PlatformIO-Installation fehlgeschlagen (Exit $LASTEXITCODE)." -ForegroundColor Red
        exit $LASTEXITCODE
    }

    $pio = Get-PlatformIO
    if (-not $pio) {
        Write-Host 'PlatformIO wurde installiert, aber die CLI wurde nicht gefunden. Terminal neu starten und erneut versuchen.' -ForegroundColor Red
        exit 1
    }
    Write-Host "  PlatformIO installiert: $pio" -ForegroundColor Green
    return $pio
}

$pio = Get-PlatformIO
if (-not $pio) { $pio = Install-PlatformIO }

# --- Ins firmware/-Verzeichnis wechseln (dieses Skript liegt in tools/) ------
$firmwareDir = Resolve-Path (Join-Path $PSScriptRoot '..\firmware')
Set-Location $firmwareDir
$envName = 'esp32-8048S070N'

function Invoke-Pio([string[]]$PioArgs, [string]$What) {
    Write-Host ">> $What" -ForegroundColor Cyan
    & $pio @PioArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FEHLGESCHLAGEN: $What (Exit $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# Port-Argumente
$portArgs = @(); if ($Port) { $portArgs = @('--upload-port', $Port) }
$monPortArgs = @(); if ($Port) { $monPortArgs = @('--port', $Port) }

# --- Nur-Monitor-Modus -------------------------------------------------------
if ($MonitorOnly) {
    Write-Host 'Serieller Monitor (Strg+C zum Beenden) ...' -ForegroundColor Green
    & $pio device monitor -e $envName @monPortArgs
    exit $LASTEXITCODE
}

# --- Abhaengigkeiten installieren (Plattform, Toolchain, Bibliotheken) -------
# Managed Components (lvgl, esp_lvgl_port) zieht der IDF-Component-Manager beim
# ersten Build automatisch nach.
if (-not $SkipDeps) {
    Invoke-Pio @('pkg', 'install') 'Abhaengigkeiten installieren (Plattform/Toolchain/Libs)'
}

# --- Optional: Flash loeschen ------------------------------------------------
if ($Erase) {
    Invoke-Pio (@('run', '-e', $envName, '-t', 'erase') + $portArgs) 'Flash loeschen (erase)'
}

# --- Bauen -------------------------------------------------------------------
if (-not $SkipBuild) {
    Invoke-Pio @('run', '-e', $envName) 'Firmware bauen'
}

# --- Flashen -----------------------------------------------------------------
Invoke-Pio (@('run', '-e', $envName, '-t', 'upload') + $portArgs) 'Firmware flashen'
Write-Host 'Fertig geflasht.' -ForegroundColor Green

# --- Optional: Monitor -------------------------------------------------------
if ($Monitor) {
    Write-Host 'Serieller Monitor (Strg+C zum Beenden) ...' -ForegroundColor Green
    & $pio device monitor -e $envName @monPortArgs
}
