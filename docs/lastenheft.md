# esp-infoscreen — Lastenheft (Entwurf)

Stand 2026-08-07. ESP-basiertes Wand-Infodisplay, Bedienung/Konfiguration über
Webinterface, Erstinstallation über AP-„installer"-Methode (wie ESP-BMC /
Sensormeter-Familie).

## Zielhardware

**ESP32-8048S070** (Sunton/Guition-Klasse):
- ESP32-S3-WROOM-1, Dual-Core bis 240 MHz, 512 KB SRAM, **8 MB PSRAM**, 16 MB Flash
- 7,0" **800×480** RGB-Parallel-LCD (16-bit RGB565), Panel-Treiber **EK9716**
- **ohne** Touch (kein Touch-Controller bestückt) → Bedienung ausschließlich Web
- microSD-Slot, IO-Header, Backlight-Steuerung

## Anzeige (Slideshow, horizontal)

Rotierende Slides auf dem integrierten Display, Ausrichtung horizontal,
**per Webinterface um 180° drehbar** (Deckenmontage/Kabelrichtung).

| # | Slide | Datenquelle | Aktualisierung |
|---|---|---|---|
| 1 | Wetter Johannesberg | OpenWeatherMap API (JSON, API-Key) | z. B. 10–15 min |
| 2 | Kalender / nächste 5 Termine | lokal (Webinterface anlegbar, persistent) | live |
| 3 | Müllabfuhr Johannesberg/Oberafferbach | **Quelle offen** (ICS/API o. manuell) → in Kalender integriert | täglich |
| 4 | Temperatur + Windgeschwindigkeit | spessartwetter.de `custom.html` (HTML-Scrape) | alle 30 min |
| 5 | Fritzbox: externe IP, Internet-Speed, Auslastung | Fritzbox TR-064 (SOAP, Login) | 1–5 min |
| 6 | Wetterwarnung Johannesberg | DWD-Warnungen (JSON, Warncell-ID) | 5–15 min |
| 7 | Uhrzeit + Datum | NTP | live |
| 8 | Eigene IP + WLAN-Empfang (dBm) | lokal (WiFi.localIP / RSSI) | live |

## Konfiguration / Bedienung (Webinterface)

- WLAN-Einrichtung (AP-„installer"-Fallback, 192.168.4.1)
- API-Keys / Zugangsdaten: OpenWeatherMap-Key, Fritzbox-User+Passwort,
  Standort-IDs (OWM, DWD-Warncell, Müll-Bezirk)
- Termine anlegen/löschen (Slide 2)
- Display-Drehung 0°/180°
- Slide-Auswahl / -Dauer (welche Slides, wie lange)

## Erstinstallation

AP-„installer"-Methode (bewährt aus ESP-BMC/Sensormeter): Board startet ohne
WLAN einen eigenen Access Point, Nutzer verbindet sich, trägt WLAN + Zugangsdaten
im Webinterface ein.

## Datenquellen-Machbarkeit (ehrliche Einschätzung, 2026-08-07)

| Quelle | Aufwand | Anmerkung / Risiko |
|---|---|---|
| OpenWeatherMap | 🟢 gering | offizielle JSON-API, HTTPS, API-Key. Standard. |
| NTP, IP, RSSI | 🟢 trivial | Bordmittel. |
| Lokale Termine | 🟢 gering | Persistenz LittleFS/NVS + Webformular. |
| Fritzbox TR-064 | 🟡 mittel | SOAP über HTTP, **Digest-Auth**. GetExternalIPAddress (WANIPConnection), GetCommonLinkProperties (Layout-Speed), GetAddonInfos (aktuelle Byte-Raten = Auslastung). TR-064 muss in der Fritzbox aktiviert sein. |
| DWD-Warnung | 🟡 mittel | Braucht korrekten **Warncell-/Gemeinde-ID** für Johannesberg (Lkr. Aschaffenburg) + erreichbaren JSON-Endpoint (DWD/NINA). Quelle recherchieren. |
| Müllabfuhr | 🟠 offen | **Beste Lösung: ICS/iCal-Feed** des Entsorgers (Lkr. Aschaffenburg / AWG) falls vorhanden → iCal parsen. Sonst manuell im Webinterface pflegen. Quelle muss recherchiert werden. |
| spessartwetter custom.html | 🟠 fragil | **HTML-Scraping** einer Fremdseite. Nur machbar, wenn die Werte als statischer Text im HTML stehen (nicht per JS nachgeladen). Muss geprüft werden; Layout-Änderung der Seite bricht das Scraping. Höflich pollen (30 min ok). |

## Recherchierte Quellen (2026-08-07) — konkrete Endpoints

Framework-Entscheid: **ESP-IDF** (bestätigt).

| Slide | Konkrete Quelle | Status |
|---|---|---|
| Müllabfuhr | **MyMüll/jumomind JSON-API** (kein Key): `https://mymuell.jumomind.com/mmapp/api.php?r=dates&city_id=44886&area_id=44886` → JSON `[{id,title,trash_name,day(YYYY-MM-DD),color}]`. **Johannesberg-Oberafferbach = ID 44886**. Andere Ortsteile: Johannesberg 29018, Breunsberg 7639, Rückersbach 53323, Steinbach 59146, Sternberg 59533. Städteliste: `.../mmapp/loxone/lox.php?r=cities`. | 🟢 GELÖST — JSON statt iCal, ideal für cJSON. In Kalender/Slide 2+3 mergen. |
| DWD-Warnung | **Bright Sky Alerts** (kein Key, HTTPS): `https://api.brightsky.dev/alerts?lat=50.008&lon=9.216` → `{alerts:[{event,severity,headline,description,onset,expires,...}], location{warn_cell_id}}`. Für Johannesberg lieferte lat/lon Warncell 809671130 (Hösbach-Zelle, benachbart). | 🟢 GELÖST — verifiziert (aktuell 0 Warnungen). ⚠ ggf. exakte Johannesberg-Warncell gegenprüfen. |
| spessartwetter | `https://www.spessartwetter.de/webimg/custom.html` — Werte stehen als **statischer Text** im HTML (Temp „25,0 °C", Wind „7,9 km/h" + Beaufort, dazu Feuchte/Druck/Taupunkt/Böen/…). Per String-Suche extrahierbar. | 🟡 SCRAPEBAR — funktioniert, aber bricht bei Layout-Änderung der Seite. Alle 30 min pollen. |
| OpenWeatherMap | `api.openweathermap.org` (Johannesberg ≈ lat 50.008, lon 9.216), API-Key nötig. | 🟢 Standard. |
| Fritzbox | TR-064/SOAP (Digest-Auth): GetExternalIPAddress · GetCommonLinkProperties (Layout-Speed) · GetAddonInfos (Byte-Raten = Auslastung). TR-064 in Box aktivieren. | 🟡 wie geplant. |
| NTP / IP / RSSI | Bordmittel. | 🟢 trivial. |

## Offene technische Punkte

1. **Display-Bring-up (RGB-Parallel):** exakte Panel-Timings + Pin-Map des
   ESP32-8048S070 (der klassisch heikelste Teil). Gut dokumentiert für diese
   Board-Familie, aber board-spezifisch verifizieren.
2. **Framework:** ESP-IDF (esp_lcd RGB-Panel + LVGL via esp_lvgl_port) vs.
   Arduino (Arduino_GFX + LVGL). Siehe Empfehlung in den Projektnotizen.
3. **HTTPS/TLS:** OWM & DWD sind HTTPS → CA-Zertifikate + TLS-RAM (8 MB PSRAM
   hilft). Cert-Handling planen.
4. **180°-Rotation** eines 800×480×16-bit-RGB-Panels: für eine Slideshow
   (seltene Updates) unkritisch; LVGL-SW-Rotation 180° ist günstig.
5. **Framebuffer:** 800×480×2 B = 768 KB → in PSRAM; Doppelpuffer ~1,5 MB passt.
