#include "firmware_manager.h"
#include "hardware.h"
#include "ota_update.h" // OTA_ASSET_NAME: se excluye del listado (no es un firmware "hijo")
#include <Update.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"

// ---------- Lista de firmwares (declarada extern en config.h) ----------
AppEntry apps[MAX_APPS];
int appCount = 0;
char lastAppLabel[40] = "";

void loadDescription(AppEntry& app) {
  app.desc[0] = '\0';
  String txtPath = String(app.path);
  txtPath.replace(".bin", ".txt");
  txtPath.replace(".BIN", ".txt");
  File f = SD.open(txtPath, FILE_READ);
  if (f) {
    String line = f.readStringUntil('\n');
    line.trim();
    strncpy(app.desc, line.c_str(), sizeof(app.desc) - 1);
    f.close();
  }
}

bool scanFirmwareDir() {
  appCount = 0;
  File dir = SD.open("/firmware");
  if (!dir || !dir.isDirectory()) return false;

  File entry = dir.openNextFile();
  while (entry && appCount < MAX_APPS) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      bool isLauncherOwnBin = name.equalsIgnoreCase(OTA_ASSET_NAME);
      if (!isLauncherOwnBin && (name.endsWith(".bin") || name.endsWith(".BIN"))) {
        AppEntry& app = apps[appCount];
        String full = String("/firmware/") + name;
        String label = name.substring(0, name.lastIndexOf('.'));
        strncpy(app.label, label.c_str(), sizeof(app.label) - 1);
        strncpy(app.path, full.c_str(), sizeof(app.path) - 1);
        app.size = entry.size();

        time_t t = entry.getLastWrite();
        if (t > 0) {
          struct tm* tmInfo = localtime(&t);
          strftime(app.dateStr, sizeof(app.dateStr), "%Y-%m-%d %H:%M", tmInfo);
        } else {
          strncpy(app.dateStr, "fecha desconocida", sizeof(app.dateStr) - 1);
        }

        loadDescription(app);
        appCount++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  return true;
}

// ---------- Backups guardados (/backups/) ----------
AppEntry backupApps[MAX_BACKUPS];
int backupCount = 0;

bool scanBackupsDir() {
  backupCount = 0;
  busToSD();
  File dir = SD.open("/backups");
  if (!dir || !dir.isDirectory()) return false;

  File entry = dir.openNextFile();
  while (entry && backupCount < MAX_BACKUPS) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      if (name.endsWith(".bin") || name.endsWith(".BIN")) {
        AppEntry& app = backupApps[backupCount];
        String full = String("/backups/") + name;
        String label = name.substring(0, name.lastIndexOf('.'));
        strncpy(app.label, label.c_str(), sizeof(app.label) - 1);
        strncpy(app.path, full.c_str(), sizeof(app.path) - 1);
        app.size = entry.size();
        app.desc[0] = '\0';

        time_t t = entry.getLastWrite();
        if (t > 0) {
          struct tm* tmInfo = localtime(&t);
          strftime(app.dateStr, sizeof(app.dateStr), "%Y-%m-%d %H:%M", tmInfo);
        } else {
          strncpy(app.dateStr, "fecha desconocida", sizeof(app.dateStr) - 1);
        }

        backupCount++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  return true;
}

bool deleteBackupFromSD(AppEntry& backup) {
  busToSD();
  return SD.remove(backup.path);
}

// ---------- Backup de app0 antes de sobrescribir ----------
bool backupCurrentApp0() {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
  if (!part) return false;

  busToSD();
  if (!SD.exists("/backups")) SD.mkdir("/backups");

  String name = lastAppLabel[0] ? String(lastAppLabel) : String("unknown");
  String path = "/backups/" + name + ".bin";

  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;

  const size_t CHUNK = 4096;
  static uint8_t buf[CHUNK]; // fuera de la pila: evita stack overflow en loopTask (8KB)
  size_t done = 0;

  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(RETRO_SIGN, RETRO_BG);
  tft.drawString("Haciendo backup...", tft.width() / 2, tft.height() / 2 - 20, 4);

  unsigned long lastDraw = 0;
  while (done < part->size) {
    size_t n = min(CHUNK, part->size - done);
    if (esp_partition_read(part, done, buf, n) != ESP_OK) {
      f.close();
      return false;
    }
    f.write(buf, n);
    done += n;
    if (millis() - lastDraw > 150) {
      char pct[8];
      snprintf(pct, sizeof(pct), "%d%%", (int)(100UL * done / part->size));
      tft.fillRect(0, tft.height() / 2 + 10, tft.width(), 30, RETRO_BG);
      tft.drawString(pct, tft.width() / 2, tft.height() / 2 + 20, 4);
      lastDraw = millis();
    }
  }
  f.close();
  return true;
}

// ---------- Verificación básica de imagen ----------
bool validateImage(File& f, size_t maxPartitionSize, String& errMsg) {
  if (f.size() < 32) { errMsg = "Archivo demasiado pequeno"; return false; }
  if (f.size() > maxPartitionSize) { errMsg = "No cabe en la particion destino"; return false; }

  uint8_t magic = f.read();
  f.seek(0);
  if (magic != 0xE9) { errMsg = "No es una imagen ESP32 valida"; return false; }

  return true;
}

// ---------- Flasheo desde SD hacia app0 vía Update.h ----------
void drawProgress(const char* label, size_t done, size_t total) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Flasheando:", tft.width() / 2, 40, 4);
  tft.drawString(label, tft.width() / 2, 70, 2);

  int barX = 20, barY = 130, barW = tft.width() - 40, barH = 24;
  tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
  int fillW = total ? (int)((uint64_t)(barW - 2) * done / total) : 0;
  tft.fillRect(barX + 1, barY + 1, fillW, barH - 2, TFT_GREEN);

  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", total ? (int)(100UL * done / total) : 0);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(pct, tft.width() / 2, barY + barH + 12, 4);
}

void showError(const char* line1, const char* line2) {
  ledError();
  beepError();
  tft.fillScreen(TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawString(line1, tft.width() / 2, tft.height() / 2 - (line2 ? 15 : 0), 2);
  if (line2) tft.drawString(line2, tft.width() / 2, tft.height() / 2 + 15, 2);
  delay(3000);
  ledReady();
}

bool flashFromSD(AppEntry& app) {
  busToSD();

  File f = SD.open(app.path, FILE_READ);
  if (!f) {
    showError("No se pudo abrir el .bin");
    return false;
  }

  const esp_partition_t* target = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
  size_t maxSize = target ? target->size : (size_t)-1;

  String errMsg;
  if (!validateImage(f, maxSize, errMsg)) {
    f.close();
    showError("Imagen invalida:", errMsg.c_str());
    return false;
  }

  // Backup de lo que hay antes de sobrescribir
  if (!backupCurrentApp0()) {
    // No es fatal, pero avisamos
    tft.fillScreen(RETRO_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_ORANGE, RETRO_BG);
    tft.drawString("Aviso: backup fallo, sigo igual", tft.width() / 2, tft.height() / 2, 2);
    delay(1500);
  }

  ledBusy();
  size_t fileSize = f.size();

  if (!Update.begin(fileSize, U_FLASH)) {
    f.close();
    showError("No cabe en la particion app0", "(revisa partitions.csv)");
    return false;
  }

  static uint8_t buf[4096]; // fuera de la pila: evita stack overflow en loopTask (8KB)
  size_t written = 0;
  drawProgress(app.label, 0, fileSize);
  unsigned long lastDraw = millis();

  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (Update.write(buf, n) != n) {
      f.close();
      Update.abort();
      showError("Error escribiendo flash");
      return false;
    }
    written += n;
    if (millis() - lastDraw > 120) {
      drawProgress(app.label, written, fileSize);
      lastDraw = millis();
    }
  }
  f.close();
  drawProgress(app.label, fileSize, fileSize);

  if (!Update.end(true) || !Update.isFinished()) {
    showError("Flasheo incompleto");
    return false;
  }

  strncpy(lastAppLabel, app.label, sizeof(lastAppLabel) - 1);
  prefs.putString("lastApp", lastAppLabel);

  return true;
}

// Borra el .bin y, si existe, su .txt de descripción asociado
bool deleteAppFromSD(AppEntry& app) {
  busToSD();
  bool ok = SD.remove(app.path);
  String txtPath = String(app.path);
  txtPath.replace(".bin", ".txt");
  txtPath.replace(".BIN", ".txt");
  if (SD.exists(txtPath)) SD.remove(txtPath);
  return ok;
}
