# esp-infoscreen

ESP-basiertes Info-Display-Projekt. *(Neu angelegt 2026-08-07 — Details folgen.)*

7,0"-Wand-Infodisplay auf Basis des **ESP32-8048S070** (ESP32-S3-WROOM-1,
800×480 RGB, 8 MB PSRAM, 16 MB Flash). Zeigt als Slideshow: Wetter
(OpenWeatherMap), Kalender/Termine (web-anlegbar), Müllabfuhr Johannesberg,
spessartwetter-Werte, Fritzbox-Status, DWD-Warnungen, Uhrzeit und Netzwerk-Info.
Konfiguration über Webinterface, Erstinstallation per AP-„installer"-Methode.

## Status

Firmware-Skeleton (ESP-IDF) angelegt, **Display-Bring-up** als erster Schritt —
baut sauber, Verifikation auf echter Hardware steht aus. Datenquellen recherchiert
(siehe `docs/lastenheft.md`).

## Firmware

`firmware/` ist ein PlatformIO-Projekt (Framework **ESP-IDF**, kein Arduino).

```powershell
# Bauen + flashen + serieller Monitor (Windows):
tools\Flash.ps1 -Monitor
```

Manuell:

```
cd firmware
pio run -e esp32-8048S070N -t upload
```

## Struktur

- `firmware/` — ESP-IDF-Projekt (PlatformIO)
- `tools/` — Flash-/Hilfsskripte (PowerShell)
- `docs/` — Lastenheft, Entscheidungen, recherchierte Datenquellen
- `referenzen/` — Datenblätter, Herstellerzeichnungen, CAD-Assets

## Über dieses Projekt

Entsteht in Zusammenarbeit mit [Claude](https://claude.com/claude-code) (Anthropic)
als KI-Coding-Assistent.
