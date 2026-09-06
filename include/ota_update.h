/*
 * ota_update.h — descarga la última versión publicada del launcher
 * desde GitHub Releases (vía WiFi STA) y la guarda en la SD como
 * /firmware/CYD-Launcher.bin.
 */
#pragma once

#include "config.h"

// Repo de GitHub del que se descargan las releases
#define OTA_GITHUB_OWNER "PavDev3"
#define OTA_GITHUB_REPO  "cyd-launcher"
#define OTA_ASSET_NAME   "CYD-Launcher.bin"

// Conecta a `ssid`/`pass` (STA), descarga la última release y la guarda
// en /firmware/CYD-Launcher.bin. Dibuja el progreso en pantalla.
// Devuelve true si la descarga se completó con éxito.
bool downloadLatestRelease(const String& ssid, const String& pass);

// Flashea /firmware/CYD-Launcher.bin (ya descargado) directamente en la
// partición "launcher" (ota_1) — sin pasar por USB. No reinicia por sí
// sola: el llamador decide cuándo hacer esp_restart() (normalmente tras
// responder al navegador que disparó la actualización).
bool flashLauncherSelfUpdate();
