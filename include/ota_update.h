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
#define OTA_ASSET_NAME     "CYD-Launcher.bin"
#define OTA_ASSET_TXT_NAME "CYD-Launcher.txt" // descripción/versión, se muestra en la pantalla de info

enum class UpdateResult { Failed, AlreadyLatest, Downloaded };

// Conecta a `ssid`/`pass` (STA), compara la version actual (LAUNCHER_VERSION)
// contra el tag_name del último release; si coinciden no descarga nada
// (AlreadyLatest). Si son distintas (o no se pudo comprobar), descarga el
// .bin y lo guarda en /firmware/CYD-Launcher.bin. Dibuja el progreso en
// pantalla.
UpdateResult downloadLatestRelease(const String& ssid, const String& pass);

// Flashea /firmware/CYD-Launcher.bin (ya descargado) directamente en la
// partición "launcher" (ota_1) — sin pasar por USB. No reinicia por sí
// sola: el llamador decide cuándo hacer esp_restart() (normalmente tras
// responder al navegador que disparó la actualización).
bool flashLauncherSelfUpdate();
