#pragma once

// Startet den HTTP-Konfig-Server (Port 80). Bietet eine WLAN-Einrichtungsseite
// (Netz-Scan + SSID/Passwort) an - erreichbar im Installer-AP unter
// http://192.168.4.1 und spaeter auch ueber die LAN-IP.
void web_server_start(void);
