#pragma once
// ============================================================================
// ESP32-8048S070N — Board-/Display-Konfiguration
// ----------------------------------------------------------------------------
// VERIFIZIERTE Werte aus der getesteten PlatformIO-Boarddefinition
// rzeldent/platformio-espressif32-sunton  ->  esp32-8048S070N.json
// (die "N"-Variante = OHNE Touch, exakt dieses Board). Panel: ST7262 / EK9716,
// 7,0" 800x480, 16-bit RGB565 parallel, Framebuffer in PSRAM.
//
// WICHTIG: Es gibt Board-Revisionen (v1.1/v1.3) mit ggf. abweichender Init.
// Diese Pin-/Timing-Werte stammen aus der aktuellen, gepflegten Definition.
// Auf echter Hardware verifizieren (Display-Bring-up ist der Risikoteil).
// ============================================================================

// --- Aufloesung ---
#define DISP_H_RES              800
#define DISP_V_RES              480

// --- Steuer-Pins ---
#define DISP_PIN_HSYNC          39
#define DISP_PIN_VSYNC          40
#define DISP_PIN_DE             41
#define DISP_PIN_PCLK           42
#define DISP_PIN_DISP           (-1)   // GPIO_NUM_NC (nicht belegt)
#define DISP_PIN_BCKL           2      // Hintergrundbeleuchtung (aktiv HIGH)

// --- 16-bit RGB565 Datenleitungen ---
#define DISP_PIN_R0             15
#define DISP_PIN_R1             7
#define DISP_PIN_R2             6
#define DISP_PIN_R3             5
#define DISP_PIN_R4             4
#define DISP_PIN_G0             9
#define DISP_PIN_G1             46
#define DISP_PIN_G2             3
#define DISP_PIN_G3             8
#define DISP_PIN_G4             16
#define DISP_PIN_G5             1
#define DISP_PIN_B0             14
#define DISP_PIN_B1             21
#define DISP_PIN_B2             47
#define DISP_PIN_B3             48
#define DISP_PIN_B4             45

// --- Panel-Timings (800x480) ---
#define DISP_PCLK_HZ            (12500000)  // 12,5 MHz
#define DISP_HSYNC_PULSE        30
#define DISP_HSYNC_BACK         16
#define DISP_HSYNC_FRONT        210
#define DISP_VSYNC_PULSE        13
#define DISP_VSYNC_BACK         10
#define DISP_VSYNC_FRONT        22
// Signal-Flags: HSYNC/VSYNC idle low, DE idle low, PCLK auf fallender Flanke.

// --- microSD (TF) — SPI, fuer spaeter (nicht im Display-Bring-up genutzt) ---
#define TF_PIN_CS               10
#define TF_PIN_MOSI             11
#define TF_PIN_SCLK             12
#define TF_PIN_MISO             13
