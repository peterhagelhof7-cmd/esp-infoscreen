# esp-infoscreen — Admin-Guide

Einrichtung und Betrieb des ESP32-8048S070 Wand-Infodisplays. Für die Kurzfassung
und Funktionsübersicht siehe das [README](../README.md).

---

## 1. Überblick

Das Display zeigt eine Slideshow aus Wetter, Terminen, Müllabfuhr, Internet-Status
usw. Bedient wird es ausschließlich über ein **Webinterface** (kein Touch) und
optional einen **Telegram-Bot**. Alle Einstellungen liegen im Gerät (NVS-Speicher)
und überstehen Neustart und Firmware-Update.

**Hardware:** ESP32-8048S070 (Variante **-N, ohne Touch**). Versorgung über den
USB-C-Port mit der Beschriftung **„USB"** (nicht den reinen Strom-Port).

---

## 2. Firmware flashen

### 2.1 Web-Flasher im Browser (am einfachsten, ohne Tool-Setup)

Kein Python/PlatformIO nötig – direkt aus dem Browser flashen:

1. Board per USB anschließen (der Port mit **„USB"**).
2. In **Chrome, Edge oder Opera am Desktop** die Seite
   **[peterhagelhof7-cmd.github.io/esp-infoscreen](https://peterhagelhof7-cmd.github.io/esp-infoscreen/)**
   öffnen.
3. **Verbinden** → im Port-Dialog das Gerät wählen → **Voll-Firmware flashen**.
4. Nach dem Flashen startet das Gerät und spannt den Einrichtungs-AP auf (→ Abschnitt 3).

> **Nur Chrome/Edge/Opera am Desktop** (die „Web Serial"-Schnittstelle fehlt in Firefox,
> Safari und auf iPhone/iPad). Der native USB-Serial-JTAG des Boards (kein Treiber nötig)
> funktioniert damit sofort.

**Diagnose-/Inventur-Firmware.** Auf derselben Seite gibt es eine kleine
**Diagnose-Firmware**, die nach dem Flashen im Browser eine „Inventur" ausgibt:
Chip-Typ/-Revision, Flash- und PSRAM-Größe, MAC, **ob der DHT22 verbaut ist** (inkl.
gefundenem GPIO) und **ob es die Touch-Variante ist** (über den I²C-Touch-Controller).
Praktisch, um vor der Inbetriebnahme zu sehen, was auf dem Tisch liegt. Die Diagnose
verändert die Konfiguration nicht; danach einfach die Voll-Firmware drüberflashen.

### 2.2 Neues / leeres Gerät (per USB)

Windows, im Projektordner:

```powershell
tools\Flash.ps1            # Board anschließen; Skript wartet, baut und flasht
tools\Flash.ps1 -Monitor   # zusätzlich seriellen Monitor (Bootlog) öffnen
```

Das Skript installiert bei Bedarf Python, PlatformIO und die ESP-Toolchain selbst.
Nach dem Flashen startet das Gerät und spannt den Einrichtungs-AP auf (→ Abschnitt 3).

### 2.3 Laufendes Gerät aktualisieren (OTA, ohne Kabel)

1. Aktuelle `esp-infoscreen-*.bin` aus den
   [Releases](https://github.com/peterhagelhof7-cmd/esp-infoscreen/releases) laden.
2. Webinterface öffnen → **⚙ Einstellungen** → Karte **Firmware-Update** →
   Datei wählen → **Hochladen & Neustart**.

Der Bootloader markiert die neue Version erst nach erfolgreichem Start als gültig.
**Bootet die neue Firmware nicht durch, wird automatisch auf die vorige zurückgerollt.**

> Verlorene Verbindung nach dem Update? Die Weboberfläche ist nach ~15 s wieder
> unter `http://<gerätename>.local` bzw. der IP erreichbar.

---

## 3. Erste Einrichtung (Einrichtungs-AP)

Beim ersten Start – oder wenn kein bekanntes WLAN erreichbar ist – öffnet das Gerät
einen eigenen Access Point:

| | |
|---|---|
| **SSID** | `installer` |
| **Passwort** | `installer` |
| **Adresse** | `http://192.168.4.1` |

1. Mit dem WLAN **„installer"** verbinden.
2. `http://192.168.4.1` im Browser öffnen → **⚙ Einstellungen**.
3. Unter **WLAN-Einrichtung** das eigene Netz (SSID) wählen, Passwort eingeben,
   **Speichern & Neustart**.
4. Das Gerät verbindet sich; danach ist es im Heimnetz unter
   `http://<gerätename>.local` erreichbar (Standardname `esp-infoscreen`).

> Das Gerät ist **2,4-GHz-only**. Eine reine 5-GHz-SSID funktioniert nicht (→ Abschnitt 8).

---

## 4. Webinterface

**Startseite = Message Board** (Telegram-Chat, mit Sendefeld). Über den Button
**⚙ Einstellungen** geht es zu allen Einstellungen; von dort **← Message Board** zurück.

### Gerät
- **Name** – zugleich der mDNS-Name (`http://<name>.local`) und WLAN-Hostname.
  Änderung erfordert einen Neustart.

### Anzeige
- **Helligkeit** – Schieberegler 5–100 %, wirkt sofort.
- **Um 180° drehen** – für Decken-/Überkopfmontage (Neustart).
- **Angezeigte Slides** – jede Slide einzeln an-/abwählbar.
- **Wechsel alle … Sekunden** – Anzeigedauer je Slide (3–120 s).

### WLAN
- Netzwerk-Auswahl + Passwort (wie in Abschnitt 3). „Netzwerke neu suchen" scannt erneut.

### Wetter (OpenWeatherMap)
- **API-Key** – kostenloser Key von openweathermap.org (→ Abschnitt 6.1).
- **Standort** – leer = Johannesberg; sonst z. B. `Aschaffenburg,DE`.

### Müll
- **Ortsteil-ID** (jumomind/MyMüll) – Standard `44886` (Johannesberg-Oberafferbach).
  Weitere IDs stehen als Hinweis in der Karte. Änderung wird **sofort** neu abgerufen.

### Warnungen (BBK/NINA)
- **Warngebiet (ARS)** – 12-stelliger amtlicher Regionalschlüssel für die
  Bevölkerungsschutz-Warnungen (BBK/NINA). Standard `096710000000` (Landkreis
  Aschaffenburg). Die Warnungen-Slide zeigt NINA-Warnungen vorrangig, sonst die
  DWD-Wetterwarnung (→ Abschnitt 6.4).

### Fritzbox
- **Adresse** – leer = Gateway; sonst IP/Hostname der Fritzbox. Voraussetzung: in der
  Fritzbox „Statusinformationen über UPnP übertragen" aktiv (→ Abschnitt 6.2).

### Telegram
- **Bot-Token** und **Gruppen-Chat-ID** (→ Abschnitt 5).

### Termine
- Termine mit Datum, optional Uhrzeit und Titel anlegen; Liste mit Löschen-Button.
  Vergangene Termine werden automatisch entfernt (kurz nach Mitternacht).

### Status
- Firmware-Version, Uptime, freier Heap (+ Minimum), freies PSRAM, WLAN (RSSI/IP).

### System
- **Einstellungen herunterladen / laden** – Backup/Restore als JSON-Datei.
- **Neustart**.
- **Werksreset** – löscht alle Einstellungen; muss durch Eingabe des **Gerätenamens**
  bestätigt werden.

---

## 5. Telegram-Bot einrichten

1. **Bot anlegen:** In Telegram [@BotFather](https://t.me/BotFather) öffnen →
   `/newbot` → Namen und Benutzernamen vergeben → **Token** notieren.
2. **Privacy-Mode ausschalten** (damit der Bot normale Gruppennachrichten fürs Board
   sieht): BotFather → `/setprivacy` → Bot wählen → **Disable**.
3. **Bot zur Gruppe hinzufügen** (als Mitglied).
4. **Gruppen-Chat-ID ermitteln:** z. B. den Bot
   [@getidsbot](https://t.me/getidsbot) oder [@RawDataBot](https://t.me/RawDataBot)
   kurz in die Gruppe holen – die Chat-ID einer Gruppe ist **negativ** (z. B.
   `-1001234567890`).
5. Im Webinterface unter **Telegram** Token und Chat-ID eintragen, speichern.

Nach dem nächsten Poll meldet sich der Bot mit „Wieder da!" in der Gruppe.

### Kommandos (Bot in der Gruppe mit `@name` erwähnen)

| Kommando | Antwort |
|---|---|
| `@name` (ohne Wort) | Kommandoliste |
| `@name Wetter` | aktuelle Spessartwetter-Werte |
| `@name vorhersage` | OpenWeatherMap-Vorhersage (aktuell + bis zu 3 Tage) |
| `@name innen` | Innenraum-Temperatur/-Luftfeuchte (DHT22) |
| `@name internet` | Fritzbox: IP, Down/Up |
| `@name termin` | nächste 2 Termine |
| `@name neu termin JJJJ-MM-TT Text` | Termin anlegen (auch `TT.MM.JJJJ` / `TT.MM.`) |
| `@name status` | Uptime, Heap, WLAN |
| `@name kino` | Kinoprogramm ab heute (→ Abschnitt 6.5) |
| `@name kino preview` | Kinoprogramm ab nächster Woche |

Zusätzlich postet der Bot automatisch in die Gruppe: **Warnungen** (BBK/NINA und
DWD-Wetterwarnungen) sowie eine **Windböen-Warnung** (ab 50 km/h laut Spessartwetter).

---

## 6. Datenquellen & Voraussetzungen

### 6.1 OpenWeatherMap
Kostenlosen Account auf openweathermap.org anlegen, API-Key erzeugen, im
Webinterface (Wetter) eintragen. Der Key wird **nur lokal** gespeichert. Das Gerät
bleibt mit ~80 Abfragen/Tag unter dem Gratis-Limit (100/Tag).

### 6.2 Fritzbox (UPnP/IGD)
In der Fritzbox unter **Heimnetz → Netzwerk → Netzwerkeinstellungen**
„**Statusinformationen über UPnP übertragen**" aktivieren. Es wird **kein Login**
benötigt (nur die öffentlichen IGD-Statuswerte, Port 49000).

### 6.3 Müllabfuhr
Läuft über die jumomind/MyMüll-API. Standard ist Johannesberg-Oberafferbach
(`44886`); andere Ortsteile per ID in den Einstellungen (siehe Hinweis in der Karte).
Kein Key nötig.

### 6.4 Warnungen (BBK/NINA & DWD)
Zwei kostenlose, keyless Quellen:
- **Katastrophen-/Bevölkerungsschutz (BBK/NINA)** über `warnung.bund.de` – gesteuert
  über das **Warngebiet (ARS)** in den Einstellungen (Standard Landkreis Aschaffenburg
  `096710000000`). Den ARS des eigenen Kreises/der Gemeinde findet man z. B. über die
  amtliche Regionalschlüssel-Liste (Kreisschlüssel + `0000000`).
- **DWD-Wetterwarnungen** über **Bright Sky** (`api.brightsky.dev`) für die
  hinterlegten Koordinaten. Dient als Fallback, wenn keine NINA-Warnung aktiv ist.

Kein Account, kein Key. Sind keine Warnungen aktiv, zeigt die Slide „Keine".

### 6.5 Kinoprogramm
Das Kinoprogramm wird über das keylose kino.de-Backend geladen und **nur per
Telegram-Bot** ausgegeben (`kino` / `kino preview`) – es gibt dafür keine Slide.
Das Kino wird über den Config-Schlüssel **`kino_id`** gewählt (Standard `1405` =
KINOPOLIS Aschaffenburg; `1711` = Casino Aschaffenburg). Die ID lässt sich über
**Einstellungen → System → Einstellungen laden** (JSON mit `"kino_id": "…"`) setzen;
ein eigenes Web-Feld gibt es dafür nicht. Höchstens ein Abruf pro Kalendertag.

---

## 7. Zugriff & Namen

- **Per Name (empfohlen):** `http://<gerätename>.local` (mDNS). Standard:
  `http://esp-infoscreen.local`.
- **Per IP:** wird im Bootlog und auf der Netzwerk-Slide angezeigt.
- **Im Einrichtungs-AP:** `http://192.168.4.1`.

---

## 8. Fehlersuche

**Kommt nicht ins WLAN / hängt im Einrichtungs-AP.**
Das Gerät verbindet sich mit der **stärksten** AP der SSID (kein BSSID-Zwang). Findet
es 90 s nach dem Start keine Verbindung, startet automatisch der Einrichtungs-AP.
Häufige Ursachen bei „SSID stimmt, geht trotzdem nicht":
- **5-GHz-only** – das Gerät kann nur 2,4 GHz. 2,4-GHz-Band der SSID aktivieren.
- **Funkkanal 12/13** – Router fest auf Kanal 1–11 stellen.
- **WPA3-only** – auf WPA2 oder WPA2/WPA3-gemischt umstellen.
- **MAC-Filter** im Router aktiv.

**Was tut das Gerät gerade?** Seriellen Monitor öffnen (`tools\Flash.ps1 -MonitorOnly`
oder `-Monitor` nach dem Flashen). Aussagekräftige Zeilen:
- `network: WLAN verbunden, IP …` – erfolgreich verbunden.
- `wifi: … reason: 201` (NO_AP_FOUND) – SSID auf keinem gescannten Kanal → 5 GHz/Kanal.
- `wifi: … reason: 15 / 205` – Authentifizierung/Passwort.
- `web: HTTP-Konfig-Server gestartet (Port 80)` – Weboberfläche läuft.

**Weboberfläche nicht erreichbar, Gerät läuft aber.** Prüfen, ob es im Heimnetz
oder im Einrichtungs-AP hängt (Netzwerk-Slide / Bootlog). Ggf. neue IP – `…​.local`
verwenden.

**Anzeige flackert kurz nach dem Einschalten.** Legt sich nach einigen Zyklen
(Startlast durch WLAN/NTP/Datenabrufe) – normal.

**Alles zurücksetzen.** Webinterface → System → **Werksreset** (Gerätename bestätigen).
Danach startet wieder der Einrichtungs-AP. Ohne Web-Zugang hilft ein USB-Reflash
(löscht die Einstellungen allerdings nur mit `Flash.ps1 -Erase`).

**Nach einem `git pull` bricht der Build** (z. B. `undefined _ext_ram_bss_start`).
`tools\Flash.ps1` erkennt Konfig-Änderungen und macht automatisch sauber; manuell:
`Remove-Item -Recurse -Force firmware\.pio\build`.

---

## 9. Sicherheitshinweis

Das Webinterface ist **nicht passwortgeschützt** – jeder im selben WLAN kann
Einstellungen ändern, ein Firmware-Update einspielen oder einen Werksreset auslösen.
Das Gerät gehört daher in ein vertrauenswürdiges Heimnetz (kein Gäste-/offenes WLAN
mit Fremdzugriff). API-Key und Telegram-Token liegen nur lokal im Gerät und sind
nicht Teil der Firmware.
