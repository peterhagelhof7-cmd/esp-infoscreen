# esp-infoscreen

7-Zoll-**Wand-Infodisplay** auf Basis des **ESP32-8048S070** (ESP32-S3-WROOM-1,
800×480 RGB-LCD, 8 MB PSRAM, 16 MB Flash). Zeigt als Slideshow Wetter, Termine,
Müllabfuhr, Internet-Status u. v. m. und wird komplett über ein Webinterface und
optional einen Telegram-Bot bedient – **ohne Touch, ohne Cloud-Konto** (außer den
genutzten Datenquellen).

> **Loslegen:** fertige Firmware unter [Releases](https://github.com/peterhagelhof7-cmd/esp-infoscreen/releases)
> herunterladen, per USB flashen (`tools\Flash.ps1`) und dem Einrichtungs-Assistenten
> folgen. Ausführliche Anleitung: **[docs/admin-guide.md](docs/admin-guide.md)**.

## Funktionen

**Slideshow (per Web an-/abwählbar, Reihenfolge fest, Intervall einstellbar):**

- **Uhr & Datum** – per NTP synchronisiert (Zeitzone Deutschland, Sommer-/Winterzeit)
- **Wetter (OpenWeatherMap)** – aktuelle Werte + **Wetter-Icons** und 3-Tage-Vorhersage
- **Kalender / Termine** – selbst angelegte Termine **plus Müllabfuhr**, nach Datum sortiert
- **DWD-Wetterwarnungen** – amtliche Warnungen (Bright Sky), farbcodiert nach Stufe
- **Spessartwetter** – Temperatur/Wind von spessartwetter.de
- **Innenraum** – Temperatur/Luftfeuchte vom lokalen DHT22-Sensor (P4-Anschluss)
- **Internet / Fritzbox** – externe IP, Down-/Upload, Auslastung (UPnP/IGD, ohne Login)
- **Netzwerk** – Modus, IP; **WLAN-Empfang** (dBm, 20-s-Mittel)
- **Message Board** – Chatverlauf einer Telegram-Gruppe

**Telegram-Bot (optional):**

- Zeigt/empfängt Gruppen-Nachrichten (auch im Webinterface, mit Sendefeld)
- Auf `@bot`-Erwähnung: **Wetter**, **internet** (Fritzbox), **termin** (nächste 2),
  **neu termin JJJJ-MM-TT Text**, **status** (Uptime/Heap/WLAN) – ohne Kommando: Hilfe
- Postet **DWD-Warnungen** in die Gruppe, meldet sich nach Neustart

**Webinterface** (`http://<gerätename>.local` oder per IP):

- **Message Board** als Startseite, Einstellungen hinter dem ⚙-Button
- Gerätename, Helligkeit, Slide-Auswahl & -Intervall, 180°-Drehung (Deckenmontage)
- OpenWeatherMap-Key & -Standort, Müll-Ortsteil, Fritzbox-Adresse, Telegram-Zugang
- Termine anlegen/löschen · **Status** (Uptime/Heap/WLAN) · **Einstellungen sichern/laden**
- **Firmware-Update (OTA)** · Neustart · **Werksreset**

**Sonstiges:** echte Umlaute (eigene Fonts), Erreichbarkeit per **mDNS** (`.local`),
Erstsetup per **Einrichtungs-AP**, **OTA mit automatischem Rollback**.

## Hardware

**ESP32-8048S070** (Sunton), Variante **-N (ohne Touch)**: ESP32-S3-WROOM-1,
7,0″ 800×480 RGB-Parallel-LCD (ST7262/EK9716), 8 MB Octal-PSRAM, 16 MB Flash.
Stromversorgung über den USB-C-Port (der mit „USB", nicht den reinen Strom-Port).

**Innenraumsensor (optional):** DHT22 am **P4**-Anschluss (4-Pin: 3V3 / GND /
IO17 / IO18) — Data an **IO18** (`board_config.h`, `DHT22_PIN`). Externer
Pull-up (4,7–10 kΩ) zwischen Data und 3V3 empfohlen, falls das Sensormodul
keinen eingebauten hat.

## Installation (fertige Firmware)

**Neues / leeres Gerät – per USB (Windows):**

```powershell
tools\Flash.ps1            # baut/holt alles, wartet aufs Board, flasht
tools\Flash.ps1 -Monitor   # zusätzlich seriellen Monitor öffnen
```

Das Skript installiert bei Bedarf Python, PlatformIO und die Toolchain selbst.
Alternativ die `.bin` aus dem [Release](https://github.com/peterhagelhof7-cmd/esp-infoscreen/releases)
über das Webinterface einspielen (Einstellungen → Firmware-Update).

**Erste Einrichtung:** Das Gerät spannt beim ersten Start (oder wenn kein WLAN
erreichbar ist) einen Access Point **„installer"** auf (Passwort `installer`).
Damit verbinden, `http://192.168.4.1` öffnen, WLAN wählen – fertig. Details und
alle weiteren Einstellungen: **[docs/admin-guide.md](docs/admin-guide.md)**.

## Aus dem Quellcode bauen

`firmware/` ist ein PlatformIO-Projekt (Framework **ESP-IDF**, kein Arduino).

```
cd firmware
pio run -e esp32-8048S070N -t upload
```

Empfohlen ist der Wrapper `tools\Flash.ps1` – er zieht vor dem Bauen den neuesten
Git-Stand und erzwingt nach Konfig-Änderungen automatisch einen sauberen Reconfigure.

## Struktur

- `firmware/` — ESP-IDF-Projekt (PlatformIO); Quellcode in `firmware/main/`
- `tools/` — `Flash.ps1` (Bauen/Flashen, Abhängigkeits-Setup)
- `docs/` — Admin-Guide, Lastenheft, recherchierte Datenquellen
- `referenzen/` — Datenblätter, Herstellerzeichnungen, CAD-Assets

## Bekannte Probleme

- **Countdown bis zum nächsten Slide wird nicht angezeigt.** Der Zähler ist im
  Code angelegt (`firmware/main/slideshow.c`, Label `s_countdown`, unten rechts,
  Aktualisierung in `tick_cb`), erscheint aber auf dem Display bisher nicht.
  Noch zu untersuchen (mögliche Ursachen: Sichtbarkeit/Farbe/Position, Z-Order
  gegenüber dem Inhaltsbereich, oder Redraw des Eck-Labels). *(offen, eingeführt
  mit `fb74134`)*

## Über dieses Projekt

Entsteht in Zusammenarbeit mit [Claude](https://claude.com/claude-code) (Anthropic)
als KI-Coding-Assistent.
