/*
 * CYD Multi-Boot Launcher (carga firmwares desde microSD)
 * ---------------------------------------------------------
 * Vive en la partición "launcher" (subtipo ota_1). Escanea /firmware/*.bin
 * en la SD, muestra un menú táctil, y al elegir uno lo escribe en el slot
 * "app0" (ota_0) vía Update.h, luego reinicia hacia él.
 *
 * Este archivo solo orquesta setup()/loop() y la máquina de estados del
 * gesto táctil (toque corto = info/borrar, mantener 1s = flashear). El
 * resto de la lógica vive modularizada en:
 *
 *   hardware.*          buses SPI, LED, beep, calibración táctil, apagado
 *   firmware_manager.*  escaneo SD, backup, validación, flasheo, borrado
 *   ui_menu.*           splash + menú principal (identicons, barra de carga)
 *   ui_screens.*        info de app/dispositivo, confirmaciones, submenús
 *   wifi_upload.*        servidor WiFi de subida de .bin
 *
 * Funciones:
 *  - Backup automático de app0 a /backups/ antes de sobrescribirlo
 *  - Verificación básica de imagen (magic byte + tamaño) antes de flashear
 *  - Confirmación táctil antes de flashear (con animación de carga 1s)
 *  - Iconos "identicon" pixel-art por app (color/patrón derivado del nombre)
 *  - Toque corto = info (tamaño, fecha, descripción .txt) + botón Borrar
 *  - Recuerda la última app usada (NVS) y la resalta
 *  - Beep (altavoz) + LED RGB de estado (listo/flasheando/error)
 *  - Menú Opciones: Pantalla (calibrar/rotar), WiFi upload, Device Info, Apagar
 *
 * IMPORTANTE — dos slots OTA reales, no "factory":
 *   Muchos firmwares de terceros (closed-source o no) no se pueden
 *   parchear con un gesto propio de "volver al launcher". Por eso esta
 *   partition table usa DOS particiones OTA reales (ota_0 = app0,
 *   ota_1 = launcher) en vez de un slot "factory" — así
 *   esp_ota_get_next_update_partition() (lo que usa tanto este launcher
 *   como cualquier firmware que traiga su propia opción de "actualizar
 *   por SD" vía Update.h estándar de Arduino) siempre encuentra el otro
 *   slot como destino válido.
 *
 *   Para volver al launcher desde un firmware hijo: si ese firmware
 *   trae su propia opción de actualizar/flashear desde SD, úsala
 *   eligiendo /firmware/CYD-Launcher.bin.
 */

#include "config.h"
#include "hardware.h"
#include "firmware_manager.h"
#include "ui_menu.h"
#include "ui_screens.h"
#include "wifi_upload.h"

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  ledSetup();
  ledReady();

  prefs.begin("launcher", false);
  String saved = prefs.getString("lastApp", "");
  strncpy(lastAppLabel, saved.c_str(), sizeof(lastAppLabel) - 1);

  bool wasCalibrated = prefs.getBool("calibrated", false);
  if (wasCalibrated) {
    tsMinX = prefs.getInt("tsMinX", tsMinX);
    tsMaxX = prefs.getInt("tsMaxX", tsMaxX);
    tsMinY = prefs.getInt("tsMinY", tsMinY);
    tsMaxY = prefs.getInt("tsMaxY", tsMaxY);
  }
  screenRotation = prefs.getInt("rotation", 1);

  tft.init();
  tft.setRotation(screenRotation);

  drawSplash();

  busToTouch();
  if (!wasCalibrated) {
    calibrateTouch(true);
  }

  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(RETRO_TITLE, RETRO_BG);
  tft.drawString("Leyendo SD...", tft.width() / 2, tft.height() / 2, 4);

  busToSD();
  if (!SD.begin(SD_CS, sharedSPI, 20000000)) {
    showError("No se detecta la SD");
  } else {
    scanFirmwareDir();
  }

  busToTouch();
  drawMenu();
}

// ---------- Máquina de estados del gesto táctil sobre un item ----------
int lastTouched = -1;
unsigned long touchStart = 0;
const unsigned long HOLD_MS = 1000; // 1s de "carga" para llegar a flashear
bool longPressFired = false;

// Se completó la carga de 1s -> confirmar y flashear esa app
void handleHoldFlash(int idx) {
  if (confirmFlash(apps[idx])) {
    if (flashFromSD(apps[idx])) {
      ledReady();
      beepOk();
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("Listo, arrancando...", tft.width() / 2, tft.height() / 2, 4);
      delay(500);
      esp_restart(); // Update.end(true) ya deja app0 como boot activo
    } else {
      busToTouch();
      drawMenu();
    }
  } else {
    busToTouch();
    drawMenu();
  }
}

// Toque corto y soltado -> pantalla de info (con opción de borrar)
void handleQuickTapInfo(int idx) {
  AppEntry appCopy = apps[idx]; // copia: si se borra, el array se reescribe al re-escanear
  drawAppInfo(appCopy);
  while (ts.touched()) delay(10);
  delay(100);
  while (!ts.touched()) delay(20);

  TS_Point p = ts.getPoint();
  int tapX = touchScreenX(p.x), tapY = touchScreenY(p.y);
  bool wantsDelete = hitDeleteButton(tapX, tapY);
  while (ts.touched()) delay(10);

  if (wantsDelete && confirmDelete(appCopy)) {
    if (deleteAppFromSD(appCopy)) {
      beepOk();
    } else {
      showError("No se pudo borrar el archivo");
    }
    busToSD();
    scanFirmwareDir();
  }

  busToTouch();
  drawMenu();
}

void loop() {
  if (appCount == 0) {
    delay(500);
    return;
  }

  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int x = touchScreenX(p.x);
    int y = touchScreenY(p.y);

    if (hitMenuButton(x, y)) {
      while (ts.touched()) delay(10);
      runOptionsMenu();
      busToTouch();
      drawMenu();
      lastTouched = -1;
      delay(150);
      return;
    }

    int idx = hitTest(x, y);

    if (idx != lastTouched) {
      // Dedo se posó en un item nuevo (o se movió fuera): reinicia el conteo
      lastTouched = idx;
      touchStart = millis();
      longPressFired = false;
      if (idx != -1) {
        drawMenu(idx);
        drawChargeBarFrame(idx); // marco vacío de la barra, se rellena después
        beepTick();
      }
    } else if (idx >= 0 && !longPressFired) {
      unsigned long elapsed = millis() - touchStart;
      if (elapsed >= HOLD_MS) {
        // Carga completa -> ir a flashear
        longPressFired = true;
        beepOk();
        handleHoldFlash(idx);
        lastTouched = -1;
      } else {
        // Solo actualiza el relleno de la barra, no redibuja el resto del item
        updateChargeBar(idx, elapsed / (float)HOLD_MS);
      }
    }
  } else {
    // Se soltó el dedo
    if (lastTouched >= 0) {
      if (!longPressFired) {
        handleQuickTapInfo(lastTouched); // toque corto y soltado -> info/borrar
      } else {
        drawMenu(-1);
      }
    }
    lastTouched = -1;
    longPressFired = false;
  }

  delay(20);
}
