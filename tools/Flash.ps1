#requires -Version 5.1
<#
.SYNOPSIS
    Baut und flasht die esp-infoscreen-Firmware auf das ESP32-8048S070 und
    installiert dabei bei Bedarf alle Abhaengigkeiten.

.DESCRIPTION
    Wrapper um PlatformIO. Zieht vor jedem Lauf den neuesten Git-Stand
    (git pull --ff-only, abschaltbar mit -NoUpdate) und sorgt dafuer, dass alles
    vorhanden ist:
      1. Python 3 (falls noetig via winget, sonst offizieller Installer) -
         Voraussetzung fuer den PlatformIO-Installer.
      2. PlatformIO Core (wird per offiziellem get-platformio.py nachinstalliert,
         wenn es fehlt).
      3. Plattform espressif32 + ESP-IDF-Toolchain + deklarierte Bibliotheken
         (via `pio pkg install`).
      4. ESP-IDF-Managed-Components (lvgl, esp_lvgl_port) - werden beim ersten
         Build vom IDF-Component-Manager geladen.
    Danach: bauen, flashen (nativer USB-Serial-JTAG, VID 303A / PID 1001) und auf
    Wunsch den seriellen Monitor oeffnen.

    Board am USB-C-Port ("USB", nicht den reinen Strom-Port) anschliessen. Der
    Upload-Port wird automatisch erkannt; mit -Port ueberschreibbar.

.PARAMETER Port         Optionaler COM-Port (sonst Auto-Erkennung).
.PARAMETER Monitor      Nach dem Flashen den seriellen Monitor (115200) oeffnen.
.PARAMETER SkipBuild    Nicht neu bauen, nur flashen.
.PARAMETER SkipDeps     Abhaengigkeits-Check/-Installation ueberspringen (schneller).
.PARAMETER NoUpdate     Kein 'git pull' vor dem Bauen (mit lokalem Stand bauen).
.PARAMETER Erase        Vor dem Flashen den kompletten Flash loeschen (Recovery).
.PARAMETER MonitorOnly  Nur den seriellen Monitor oeffnen.
.PARAMETER NoWait       Nicht auf das Board warten (sonst wartet es vor dem Flashen,
                        bis ein passender USB-Serial-Port erscheint).

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
    [switch]$NoUpdate,
    [switch]$Erase,
    [switch]$MonitorOnly,
    [switch]$NoWait
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

# PATH in der laufenden Session aus Machine+User neu laden (nach einer Installation
# taucht das frisch installierte Programm sonst nicht auf).
function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user    = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = (($machine, $user) -join ';')
}

# Liefert das Python-3-Kommando als Array (@(exe) bzw. @(py.exe,'-3')) oder $null.
function Find-Python {
    foreach ($cand in @('py', 'python', 'python3')) {
        $c = Get-Command $cand -ErrorAction SilentlyContinue
        if ($c) {
            # Microsoft-Store-Alias 'python.exe' (0-Byte-Stub) aussortieren
            if ($c.Source -like '*\WindowsApps\*' -and $cand -ne 'py') { continue }
            if ($cand -eq 'py') { return @($c.Source, '-3') } else { return @($c.Source) }
        }
    }
    return $null
}

# Stellt Python 3 sicher - installiert es bei Bedarf (winget, sonst Direkt-Download).
function Ensure-Python {
    $python = Find-Python
    if ($python) { return $python }

    Write-Host 'Python 3 nicht gefunden - installiere es ...' -ForegroundColor Yellow

    # 1. Bevorzugt winget (auf Windows 10/11 vorhanden), per-User, ohne Admin
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($winget) {
        Write-Host '  Installiere Python 3 via winget (Python.Python.3.12) ...' -ForegroundColor DarkGray
        & winget install --id Python.Python.3.12 -e --source winget --scope user `
            --accept-package-agreements --accept-source-agreements
        Update-SessionPath
        $python = Find-Python
        if ($python) { Write-Host "  Python installiert: $($python[0])" -ForegroundColor Green; return $python }
        Write-Host '  winget-Installation lief, Python aber (noch) nicht im PATH - versuche Direkt-Download ...' -ForegroundColor Yellow
    }

    # 2. Fallback: offiziellen Python-Installer herunterladen und still (per-User) installieren
    $pyVer = '3.12.7'
    $exe = Join-Path $env:TEMP "python-$pyVer-amd64.exe"
    $url = "https://www.python.org/ftp/python/$pyVer/python-$pyVer-amd64.exe"
    Write-Host "  Lade Python-Installer: $url" -ForegroundColor DarkGray
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $url -OutFile $exe -UseBasicParsing
    Write-Host '  Installiere Python (still, per-User, PATH setzen) ...' -ForegroundColor DarkGray
    Start-Process -FilePath $exe -ArgumentList '/quiet InstallAllUsers=0 PrependPath=1 Include_pip=1 Include_launcher=1' -Wait
    Update-SessionPath
    $python = Find-Python
    if ($python) { Write-Host "  Python installiert: $($python[0])" -ForegroundColor Green; return $python }

    Write-Host 'Python konnte nicht automatisch installiert werden. Bitte Python 3 manuell installieren:' -ForegroundColor Red
    Write-Host '  https://www.python.org/downloads/  (beim Setup "Add python.exe to PATH" anhaken)' -ForegroundColor Red
    Write-Host 'danach Terminal neu starten und dieses Skript erneut ausfuehren.' -ForegroundColor Red
    exit 1
}

function Install-PlatformIO {
    Write-Host 'PlatformIO Core nicht gefunden - installiere es ...' -ForegroundColor Yellow

    # Python 3 sicherstellen (fuer den offiziellen PlatformIO-Installer noetig)
    $python = Ensure-Python

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

# Stellt Git sicher - installiert es bei Bedarf (winget), nur fuer den Fall,
# dass das Skript einzeln kopiert wurde und das Repo geklont werden muss.
function Ensure-Git {
    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($git) { return $git.Source }
    Write-Host 'Git nicht gefunden - installiere es ...' -ForegroundColor Yellow
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($winget) {
        & winget install --id Git.Git -e --source winget --scope user `
            --accept-package-agreements --accept-source-agreements
        Update-SessionPath
        $git = Get-Command git -ErrorAction SilentlyContinue
        if ($git) { return $git.Source }
    }
    Write-Host 'Git konnte nicht automatisch installiert werden. Bitte Git installieren' -ForegroundColor Red
    Write-Host '(https://git-scm.com/download/win) ODER das komplette Repo laden:' -ForegroundColor Red
    Write-Host '  https://github.com/peterhagelhof7-cmd/esp-infoscreen' -ForegroundColor Red
    exit 1
}

# Findet das firmware/-Verzeichnis unabhaengig davon, von wo das Skript laeuft.
# Faellt zurueck auf einen Klon des oeffentlichen Repos, wenn das Skript einzeln
# (ohne das restliche Projekt) kopiert wurde.
$RepoUrl = 'https://github.com/peterhagelhof7-cmd/esp-infoscreen.git'

# Aktualisiert das Repo, in dem $dir liegt, per 'git pull --ff-only' (best effort:
# ohne Git / kein Repo / offline / lokale Aenderungen -> nur Hinweis, kein Abbruch).
# Mit -NoUpdate wird der Schritt uebersprungen.
function Update-Repo([string]$dir) {
    if ($NoUpdate) { return }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { return }
    & git -C $dir rev-parse --is-inside-work-tree 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) { return }   # kein Git-Repo -> lokal bauen
    Write-Host '>> Projekt aktualisieren (git pull --ff-only) ...' -ForegroundColor Cyan
    & git -C $dir pull --ff-only
    if ($LASTEXITCODE -ne 0) {
        Write-Host '   Hinweis: kein Update moeglich (lokale Aenderungen / offline / divergiert) - baue mit lokalem Stand.' -ForegroundColor Yellow
    }
}

function Resolve-FirmwareDir {
    $candidates = @(
        (Join-Path $PSScriptRoot '..\firmware'),   # Skript liegt in tools/ (Normalfall)
        (Join-Path $PSScriptRoot 'firmware'),        # Skript liegt im Repo-Root
        (Join-Path (Get-Location) 'firmware'),       # aus dem Repo-Root gestartet
        (Get-Location)                               # aktuelles Verzeichnis IST firmware/
    )
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c 'platformio.ini')) {
            $p = (Resolve-Path $c).Path
            Update-Repo $p     # vor jedem Lauf auf den neuesten Git-Stand ziehen
            return $p
        }
    }
    # Nicht gefunden -> Skript wurde einzeln kopiert. Repo klonen.
    Write-Host 'firmware/ nicht gefunden - hole das Projekt-Repo ...' -ForegroundColor Yellow
    Ensure-Git | Out-Null
    $dest = Join-Path $PSScriptRoot 'esp-infoscreen'
    $destFw = Join-Path $dest 'firmware'
    if (Test-Path (Join-Path $destFw 'platformio.ini')) {
        if (-not $NoUpdate) {
            Write-Host "  Vorhandenen Klon aktualisieren: $dest" -ForegroundColor DarkGray
            & git -C $dest pull --ff-only | Out-Null
        }
    } else {
        Write-Host "  Klone $RepoUrl -> $dest" -ForegroundColor DarkGray
        & git clone --depth 1 $RepoUrl $dest
        if ($LASTEXITCODE -ne 0) {
            Write-Host "git clone fehlgeschlagen (Exit $LASTEXITCODE)." -ForegroundColor Red
            exit $LASTEXITCODE
        }
    }
    if (Test-Path (Join-Path $destFw 'platformio.ini')) { return (Resolve-Path $destFw).Path }
    Write-Host 'firmware/ auch nach dem Klonen nicht gefunden.' -ForegroundColor Red
    exit 1
}

$pio = Get-PlatformIO
if (-not $pio) { $pio = Install-PlatformIO }

# --- firmware/-Verzeichnis finden (oder Repo klonen) ------------------------
$firmwareDir = Resolve-FirmwareDir
Set-Location $firmwareDir
Write-Host "Projekt: $firmwareDir" -ForegroundColor DarkGray
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

# --- Auf angeschlossenes Board warten ----------------------------------------
# Sucht COM-Ports mit den ueblichen ESP-Programmier-USB-IDs (Espressif nativer
# USB, CP210x, CH340, FTDI). Wartet, bis eines erscheint (mit -NoWait aus).
function Get-EspComPorts {
    Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '\(COM\d+\)' -and $_.PNPDeviceID -match 'VID_(303A|10C4|1A86|0403)' } |
        ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } }
}
function Wait-ForBoard([int]$timeoutSec = 180) {
    if ($NoWait) { return }
    if (Get-EspComPorts) { return }   # schon angeschlossen
    Write-Host 'Warte auf das Board - bitte per USB anschliessen (Strg+C zum Abbrechen) ...' -ForegroundColor Yellow
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 800
        $p = Get-EspComPorts
        if ($p) {
            Write-Host "  Board erkannt: $($p -join ', ')" -ForegroundColor Green
            Start-Sleep -Milliseconds 700   # kurz einschwingen lassen
            return
        }
    }
    Write-Host '  Timeout - versuche trotzdem zu flashen.' -ForegroundColor Yellow
}

# --- Nur-Monitor-Modus -------------------------------------------------------
if ($MonitorOnly) {
    Wait-ForBoard
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

# --- Auf Board warten, dann flashen ------------------------------------------
Wait-ForBoard
Invoke-Pio (@('run', '-e', $envName, '-t', 'upload') + $portArgs) 'Firmware flashen'
Write-Host 'Fertig geflasht.' -ForegroundColor Green

# --- Optional: Monitor -------------------------------------------------------
if ($Monitor) {
    Write-Host 'Serieller Monitor (Strg+C zum Beenden) ...' -ForegroundColor Green
    & $pio device monitor -e $envName @monPortArgs
}
