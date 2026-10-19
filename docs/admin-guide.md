# esp-infoscreen — Admin-Guide

Einrichtung und Betrieb des ESP32-8048S070 Wand-Infodisplays. Für die Kurzfassung
und Funktionsübersicht siehe das [README](../README.md).

---

## 1. Überblick

Das Display zeigt eine Slideshow aus Wetter, Terminen, Müllabfuhr, Internet-Status
usw. Bedient wird es ausschließlich über ein **Webinterface** (kein Touch) und
optional einen **Telegram-Bot**. Alle Einstellungen liegen im Gerät (NVS-Speicher)
und überstehen Neustart und Firmware-Update.

**Zwei Betriebsarten (ab v1.3.0):**

| | Direkt (wie bis v1.2.x) | Client von home-central (empfohlen) |
|---|---|---|
| Internet-Abrufe | im Infoscreen selbst | auf dem Server **home-central** |
| Telegram-Bot | im Infoscreen | auf home-central |
| Einstellungen | Webinterface des Infoscreens | Weboberfläche von home-central |
| Uhrzeit | `pool.ntp.org` | NTP von home-central |
| Freier interner RAM | ~23 KB | ~87 KB |

Die Datenquelle steht ab Werk auf **Automatisch**: Gibt es im Netz einen home-central-Server,
koppelt sich der Infoscreen nach einer Freigabe selbst (→ Abschnitt 2.4); sonst arbeitet er direkt.

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

1. Aktuelle `esp-infoscreen-*-ota.bin` aus `firmware/bin/` bzw. den
   [Releases](https://github.com/peterhagelhof7-cmd/esp-infoscreen/releases) nehmen.
2. Webinterface öffnen → **⚙ Einstellungen** → Karte **Firmware-Update** →
   Datei wählen → **Hochladen & Neustart**.

Alternativ von der Kommandozeile (Windows, `curl.exe`):

```powershell
curl.exe -m 900 -H "Expect:" -H "Content-Type: application/octet-stream" `
  --data-binary "@esp-infoscreen-v1.3.1-ota.bin" http://<gerät>/ota
```

Der Upload dauert **mehrere Minuten** (~1,5 MB bei ~4 KB/s, solange das Gerät im Direkt-Modus
seine Abrufe macht) – die Seite bzw. `curl` nicht vorher abbrechen. Ein abgebrochener Upload
wird verworfen; das Gerät läuft mit der alten Version weiter.

Der Bootloader markiert die neue Version erst nach erfolgreichem Start als gültig.
**Bootet die neue Firmware nicht durch, wird automatisch auf die vorige zurückgerollt.**
Die Einstellungen bleiben erhalten; vorher trotzdem **System → Einstellungen herunterladen**.

> Verlorene Verbindung nach dem Update? Die Weboberfläche ist nach ~15 s wieder
> unter `http://<gerätename>.local` bzw. der IP erreichbar.

### 2.4 Betrieb mit home-central (ab v1.3.0)

[home-central](https://github.com/peterhagelhof7-cmd/home-central) ist ein kleiner Server im LAN
(Debian/Python), der alle Internet-Abrufe, den Telegram-Bot und die Einstellungen übernimmt. Der
Infoscreen zeigt dann nur noch an – das entlastet RAM und WLAN deutlich.

**Umstellung (einmalig):**

1. Firmware ≥ v1.3.0 aufspielen (OTA, Abschnitt 2.3, oder Web-Flasher). Datenquelle steht ab Werk
   auf **Automatisch**: Der Infoscreen sucht home-central per mDNS (`_home-central._tcp`) und fragt
   die Kopplung an. Bis dahin läuft er unverändert mit direkten Abfragen weiter.
2. Optional vorher: in home-central unter **Einstellungen → System** den Export des Infoscreens
   importieren (übernimmt alles außer WLAN und Gerätename).
3. In der Übersicht von home-central erscheint **„Kopplungsanfrage“** mit Name, IP, MAC und
   Firmware → **Freigeben**.
4. Der Infoscreen speichert Adresse + Token und startet im **Client-Modus**.
5. Den **Telegram-Token** in home-central eintragen. Der Bot läuft im Client-Modus nur dort –
   nie in beiden gleichzeitig.

**Im Client-Modus:**
- alle 15 s Abruf von `/api/v1/screen` (Wetter, Warnungen, Müll, Termine, Feiertage, Kurse,
  Fritzbox, Flugzeuge, Nachtobjekte, Heizung, Message Board, Anzeige-Einstellungen),
- **Heizung** (ab 1.4.0): Slide mit Zustand, Störung (roter Balken) und bis zu 12 Werten vom esp-p4d.
  Er ist nur an, wenn die Heizung in home-central eingerichtet ist (home-central ab 0.6.0); ohne
  home-central (Direkt-Modus, Notbetrieb) gibt es ihn nicht. Ist das esp-p4d nicht erreichbar, zeigt
  der Slide die letzten Werte mit orangem Hinweis und „Stand vor … min“,
- jede Minute Statusmeldung an home-central (Innenraum-Sensor inkl. Verlauf, WLAN, Status,
  Task-Stacks, **Slide-Katalog** – neue Slides künftiger Versionen erscheinen dadurch automatisch
  in home-central),
- Uhrzeit per NTP von home-central,
- Anzeige (Helligkeit, Slides, Intervall, Drehung) und alle Datenquellen werden **in home-central**
  eingestellt. Helligkeit wirkt sofort; Drehung, Intervall oder Slide-Auswahl übernimmt der
  Infoscreen mit einem automatischen Neustart.
- Das Webinterface des Infoscreens zeigt nur noch Gerät, Datenquelle, Firmware-Update, Status und
  System. Das Message Board sendet über den Bot von home-central.
- home-central kann den Infoscreen neu starten (Übersicht oder Bot-Kommando `reboot`).

**Wenn home-central nicht erreichbar ist (ab v1.3.1):**

| Dauer | Verhalten |
|---|---|
| sofort | letzte Daten bleiben stehen |
| ab 5 min | unten links: „home-central nicht erreichbar – Daten von HH:MM“; Adresse wird per mDNS neu gesucht (Server umgezogen?) |
| ab 30 min (WLAN da) | **Notbetrieb:** Neustart mit direkten Abfragen und der gespiegelten Einstellungs-Kopie; Hinweis „Notbetrieb …“; Telegram-Bot läuft dann im Infoscreen |
| im Notbetrieb alle 2 min | Prüfung `/api/v1/health` – ist home-central zurück, Neustart in den Client-Modus |

- **Einstellungs-Kopie:** home-central liefert einen Prüfwert seiner Einstellungen mit; ändert er
  sich, holt der Infoscreen alle Einstellungen (außer WLAN, Gerätename, Anzeige) und speichert sie im
  eigenen NVS. Leere Keys überschreiben nie vorhandene.
- **Daten-Cache:** die letzte Server-Antwort wird höchstens alle 15 min in der Flash-Partition
  `storage` gesichert (Ring aus 64 Plätzen à 64 KB, CRC-geprüft). Nach einem Neustart ohne Server
  zeigt der Infoscreen diese Daten statt leerer Slides.

**Zurück / neu koppeln:** Einstellungen → **Datenquelle** → „Nur direkte Abfragen“ (Neustart,
keine Suche mehr) bzw. **„Kopplung lösen & neu suchen“** (löscht Adresse + Token). Ein in
home-central neu erzeugter API-Token wird automatisch nachgeholt.

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

> **Im Client-Modus** (→ 2.4) gibt es nur die Karten **Gerät, Datenquelle, Firmware-Update,
> Status, Task-Stacks und System**; alle anderen hier beschriebenen Einstellungen werden in
> home-central gemacht.

### Datenquelle
- Statuszeile: aktiver Modus, Server-Adresse, letzter Abruf – bzw. „home-central gefunden,
  bitte dort freigeben“ / „im Netz nicht gefunden“ / „Notbetrieb“.
- **Automatisch** (Standard): home-central suchen, koppeln und nutzen (siehe 2.4).
- **Nur direkte Abfragen**: wie bis v1.2.x, keine Suche.
- **Kopplung lösen & neu suchen** (nur im Client-Modus): löscht Adresse + Token.
- Jede Änderung startet das Gerät neu (die Betriebsart wird nur beim Start bestimmt).

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
- **Datenquelle** – *MyMüll (jumomind)* **oder** *Abfall-App (AWIDO / cubefour)*.
  Es ist immer nur **eine** Quelle aktiv; die andere wird gar nicht abgefragt.
  Beide Einstellungen bleiben gespeichert, man kann also jederzeit zurückwechseln.
- **MyMüll:** Ortsteil-ID – Standard `44886` (Johannesberg-Oberafferbach), weitere
  IDs stehen als Hinweis in der Karte. Der Landkreis Aschaffenburg bedient MyMüll
  nur noch **bis 31.12.2026**.
- **Abfall-App (AWIDO):** Auswahl **Mandant → Gemeinde → Ortsteil/Straße →
  (Hausnummer)**. Die Listen holt das Gerät live von AWIDO; die Hausnummer erscheint
  nur, wo der Mandant danach unterscheidet. Die **AWIDO-ID** wird durch die Auswahl
  gesetzt (kann auch direkt eingetragen werden). Standard: Landkreis Aschaffenburg /
  Johannesberg / Oberafferbach (`4aa475dd-3890-45c2-8909-2a75dd1a7700`).
  Fehlt der Mandant in der Liste: „Anderer Mandant" wählen und das Kürzel eintragen
  (steht in der Adresse des Web-Abfuhrkalenders: `awido.cubefour.de/Customer/<kürzel>/…`).
- **Speichern:** Ändert sich die Quelle oder die ID der aktiven Quelle, **startet das
  Gerät neu** (die Quelle wird nur beim Start gewählt). Oben in der Karte steht die
  aktive Quelle mit Anzahl Termine und Zeitpunkt des letzten erfolgreichen Abrufs.

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
5. Token und Chat-ID eintragen und speichern – **im Client-Modus in home-central**
   (Einstellungen → Telegram), sonst im Webinterface des Infoscreens unter **Telegram**.

Der Bot darf nur an **einer** Stelle laufen (sonst holen sich beide die Nachrichten gegenseitig
weg). Im Client-Modus läuft er automatisch nur auf home-central; im Notbetrieb übernimmt der
Infoscreen. Nach dem Start meldet sich der Bot in der Gruppe („Wieder da!“ bzw. „home-central
gestartet“).

### Kommandos (Bot in der Gruppe mit `@name` erwähnen)

| Kommando | Antwort |
|---|---|
| `@name` (ohne Wort) | Kommandoliste |
| `@name Wetter` | aktuelle Spessartwetter-Werte |
| `@name vorhersage` | OpenWeatherMap-Vorhersage (aktuell + bis zu 3 Tage) |
| `@name planes` | Flugzeuge in der Nähe (mit Route) |
| `@name feiertag` | die nächsten 3 Feiertage des Bundeslandes |
| `@name innen` | Innenraum-Temperatur/-Luftfeuchte (DHT22) |
| `@name internet` | Fritzbox: IP, Down/Up |
| `@name termin` | die nächsten 5 Termine (eigene + Müll + Feiertage) |
| `@name neu termin JJJJ-MM-TT Text` | Termin anlegen (auch `TT.MM.JJJJ` / `TT.MM.`) |
| `@name status` | Infoscreen: Firmware, Uptime, RAM, WLAN (im Client-Modus zusätzlich Zustand von home-central) |
| `@name stacks` | Stack-Reserve je Task des Infoscreens (Diagnose) |
| `@name reboot` | Infoscreen neu starten |
| `@name alarm` | alle aktiven Warnungen (DWD, NINA, Windböen) |
| `@name nacht` | sichtbare Satelliten-Überflüge + erdnahe Objekte |
| `@name kurse` | BTC-EUR und EUR-USD mit 24h-Trend |
| `@name kino` | Kinoprogramm ab heute (→ Abschnitt 6.5) |
| `@name kino preview` | Kinoprogramm ab nächster Woche |

Zusätzlich postet der Bot automatisch in die Gruppe: **Warnungen** (BBK/NINA und
DWD-Wetterwarnungen), eine **Windböen-Warnung** (ab 50 km/h laut Spessartwetter),
sonntags ab 10 Uhr das **Kinoprogramm**, eine Meldung, wenn eine Datenquelle **über 1 h
nicht erreichbar** ist, und – nur auf home-central – wenn der **Infoscreen über 10 min
nicht mehr abgefragt** hat bzw. wieder online ist.

Jeden Morgen um **6 Uhr** kommt ein **Tagesbericht** (Wetter, Feiertag, Termine). Aus der
OpenWeatherMap-Vorhersage für heute hängt er bei markantem Wetter Hinweise an:

| Hinweis | Auslöser |
|---|---|
| Gewitter | Gewitter in mind. einem 3-h-Abschnitt |
| Eis und Glätte | gefrierender Regen/Schneeregen, oder Tiefstwert ≤ 1 °C bei Niederschlag |
| Frost | Tiefstwert ≤ 0 °C (ohne Niederschlag) |
| Schneefall | Schnee ≥ 1 mm |
| Viel Regen / Regen ab ca. HH Uhr | Regen ≥ 10 mm bzw. ≥ 1 mm (mit Uhrzeit des Beginns) |
| Stürmische Böen | Böen ≥ 60 km/h |
| Hitze | Höchstwert ≥ 30 °C |
| Nebel | aktuell oder vorhergesagt Nebel bzw. Sicht < 1 km |
| Nachtfrost | morgen früh (bis ~8 Uhr) ≤ 0 °C |
| DWD | aktive amtliche DWD-Warnung (Kopfzeile, ggf. „+N weitere“) |

Die Schwellen stehen als `HW_*`-Defines in `firmware/main/telegram.c` (bzw. in home-central
in `hc/telegram.py`).

---

## 6. Datenquellen & Voraussetzungen

> Im **Client-Modus** fragt home-central diese Quellen ab (gleiche Intervalle und Auswertung);
> die Voraussetzungen (Keys, Fritzbox-UPnP) gelten dann für home-central. Die Fritzbox ist dort
> standardmäßig das Gateway des Servers.

### 6.1 OpenWeatherMap
Kostenlosen Account auf openweathermap.org anlegen, API-Key erzeugen, im
Webinterface (Wetter) eintragen. Der Key wird **nur lokal** gespeichert. Das Gerät
bleibt mit ~80 Abfragen/Tag unter dem Gratis-Limit (100/Tag).

### 6.2 Fritzbox (UPnP/IGD)
In der Fritzbox unter **Heimnetz → Netzwerk → Netzwerkeinstellungen**
„**Statusinformationen über UPnP übertragen**" aktivieren. Es wird **kein Login**
benötigt (nur die öffentlichen IGD-Statuswerte, Port 49000).

### 6.3 Müllabfuhr
Zwei alternative, keyless Quellen (Auswahl in der Karte „Müll", siehe oben):
- **MyMüll / jumomind** (`mymuell.jumomind.com`, JSON). Standard Johannesberg-Oberafferbach
  (`44886`). Im Landkreis Aschaffenburg nur noch bis 31.12.2026 – danach auf die
  Abfall-App umstellen.
- **Abfall-App / AWIDO** (`awido.cubefour.de`, Betreiber cubefour; dahinter steht u. a.
  die „LK Aschaffenburg Abfall-App"). Termine kommen als iCal-Datei je Jahr; ab Oktober
  wird zusätzlich das Folgejahr geladen. Angezeigt werden nur Termine der gewählten
  Gemeinde (z. B. entfällt die stationäre Schadstoffsammlung Mömbris), Titel ohne
  „in <Ort>". Die Mandantenliste (48 Landkreise/Städte, überwiegend Bayern) ist fest
  in der Firmware hinterlegt, weil AWIDO keine Liste aller Mandanten anbietet.

Abruf alle 6 h, nach einem Fehlschlag erneut nach 15 min.

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
KINOPOLIS Aschaffenburg; `1711` = Casino Aschaffenburg). Im Client-Modus gibt es dafür in
home-central die Karte **Kino**; im Direkt-Modus lässt sich die ID nur über **Einstellungen →
System → Einstellungen laden** (JSON mit `"kino_id": "…"`) setzen. Höchstens ein Abruf pro
Kalendertag (home-central: alle 6 h).

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

**Keine Kopplungsanfrage in home-central.** Statuszeile der Karte **Datenquelle** prüfen:
- „im Netz nicht gefunden“ – mDNS kommt nicht durch: Infoscreen und home-central müssen im selben
  Netz/Subnetz sein (kein Gast-WLAN, keine Client-Isolation); auf home-central muss `avahi-daemon`
  laufen. Die Suche wiederholt sich alle 30 s.
- „Nur direkte Abfragen“ – Datenquelle steht auf direkt → auf **Automatisch** stellen.
- Anfragen verfallen in home-central nach 15 min, werden aber alle 30 s erneuert.

**„home-central nicht erreichbar“ unten links.** Server/Dienst prüfen (`systemctl status
home-central`). Nach 30 min schaltet der Infoscreen in den Notbetrieb und kehrt danach von selbst
zurück (→ 2.4). Hat home-central eine neue IP, findet der Infoscreen sie per mDNS.

**Infoscreen im Client-Modus zeigt alte Einstellungen.** Drehung/Intervall/Slides werden erst nach
dem automatischen Neustart wirksam (passiert beim nächsten Abruf, ≤ 15 s nach dem Speichern).

**OTA-Upload bricht ab.** Der Upload dauert mehrere Minuten; Zeitlimit des Tools hoch genug setzen
(→ 2.3). Ein abgebrochener Upload ändert nichts am Gerät.

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

Im Client-Modus speichert der Infoscreen zusätzlich den **API-Token von home-central** (für den
Datenabruf) und eine Kopie der Einstellungen von home-central (Notbetrieb). home-central selbst ist
mit einem Passwort geschützt; neue Infoscreens erhalten den Token erst nach **Freigabe** in dessen
Weboberfläche.
