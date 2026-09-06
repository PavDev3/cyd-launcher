/*
 * config.h — pines, tema visual, estructuras y estado global compartido
 * entre todos los módulos del launcher.
 *
 * Los `const int`/`#define` de aquí son seguros de incluir en varios .cpp
 * (enlace interno / macros de texto). Los objetos reales (tft, ts, prefs,
 * apps[], etc.) se DEFINEN una sola vez en su .cpp dueño y aquí solo se
 * declaran `extern` para que el resto de módulos los puedan usar.
 */
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>

// ---------- Versión del firmware ----------
// Debe coincidir EXACTAMENTE con el tag de git del release correspondiente
// (formato CalVer vYYYY.MM.DD[.N]). Se usa para no re-descargar/flashear
// si ya estás en la última versión. Bump manual antes de cada `git tag`.
#define LAUNCHER_VERSION "v2026.09.06.2"

// ---------- Pines SD (bus físico propio de la CYD) ----------
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define SD_CS    5

// ---------- Pines LED RGB y altavoz (CYD estándar) ----------
#define LED_R 4
#define LED_G 16
#define LED_B 17
#define LED_ACTIVE_LOW true   // RGB de la CYD es cátodo común en la mayoría de unidades
#define SPEAKER_PIN 26
#define BEEP_LEDC_CHANNEL 4   // canal libre, evita choques con otros usos de LEDC

// ---------- Pines táctil XPT2046 ----------
// (definidos también en platformio.ini como TOUCH_*, se reutilizan aquí)

// ---------- Objetos compartidos (definidos en hardware.cpp) ----------
extern TFT_eSPI tft;
extern SPIClass sharedSPI;
extern XPT2046_Touchscreen ts;
extern Preferences prefs;

// Calibración táctil y rotación (definidos en hardware.cpp)
extern int tsMinX, tsMaxX, tsMinY, tsMaxY;
extern int screenRotation;

// ---------- Lista de firmwares encontrados en la SD ----------
#define MAX_APPS 12
struct AppEntry {
  char label[40];    // nombre de archivo sin extensión
  char path[64];      // ruta completa en la SD
  uint32_t size;
  char dateStr[24];   // fecha de modificación, formateada
  char desc[80];       // primera línea de <label>.txt si existe
};

// Definidos en firmware_manager.cpp
extern AppEntry apps[MAX_APPS];
extern int appCount;
extern char lastAppLabel[40];

// ---------- Tema retro / pixel art ----------
#define RETRO_BG      TFT_BLACK
#define RETRO_TITLE   TFT_YELLOW
#define RETRO_SHADOW  TFT_RED
#define RETRO_SIGN    TFT_CYAN
#define RETRO_ITEM_BG TFT_NAVY
#define RETRO_SELECT  TFT_GREEN
#define RETRO_BORDER  TFT_BLACK
#define RETRO_FOOTER  TFT_CYAN
#define RETRO_LAST    TFT_ORANGE

// ---------- Layout del menú principal ----------
const int MARGIN = 16;
const int ITEM_H = 46;
const int ITEM_GAP = 8;
const int LIST_TOP = 56;
const int ICON_SIZE = 30;
const int MAX_LABEL_W = 130; // ancho máx. en px para el nombre antes de truncar con "..."
const int TITLE_Y = 8;
const int MENU_BTN_W = 46, MENU_BTN_H = 22, MENU_BTN_Y = 6;

// ---------- Layout de sub-menús de lista (Opciones / Pantalla) ----------
const int OPT_TOP = 28;
const int OPT_ITEM_H = 30;
const int OPT_ITEM_GAP = 4;

// ---------- Botones Sí/No comunes (confirmFlash, confirmDelete) ----------
const int BTN_W = 100, BTN_H = 44;

// ---------- Botón "Borrar .bin" en la pantalla de info ----------
const int DELETE_BTN_W = 130, DELETE_BTN_H = 30;

// ---------- Barra de carga (mantener presionado 1s) ----------
const int BAR_H = 6;
const int BAR_MARGIN = 6;
