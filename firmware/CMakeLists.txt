# esp-infoscreen — ESP-IDF Projekt
cmake_minimum_required(VERSION 3.16)
# Eindeutige Firmware-Kennung fuer den Status-Screen/Telegram-Status. Ueberschreibt
# die sonst aus "git describe" abgeleitete Version (esp_app_desc.version, max 31 B),
# damit nach einem OTA klar ist, welcher Build laeuft. Bei jeder relevanten
# Aenderung hochzaehlen/anpassen.
set(PROJECT_VER "v1.2.3-wifi-psram")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-infoscreen)
