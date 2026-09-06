#include "ota_update.h"
#include "hardware.h"
#include "firmware_manager.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include "esp_partition.h"

static void drawUpdateScreen(const char* line1, const char* line2 = nullptr) {
  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(RETRO_TITLE, RETRO_BG);
  tft.drawString("Buscando actualizacion", tft.width() / 2, 10, 4);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, RETRO_BG);
  tft.drawString(line1, tft.width() / 2, tft.height() / 2 - (line2 ? 12 : 0), 2);
  if (line2) tft.drawString(line2, tft.width() / 2, tft.height() / 2 + 16, 2);
}

static void drawUpdateProgress(int written, int total) {
  int barX = 20, barY = 150, barW = tft.width() - 40, barH = 24;
  tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
  int fillW = total > 0 ? (int)((uint64_t)(barW - 2) * written / total) : 0;
  tft.fillRect(barX + 1, barY + 1, fillW, barH - 2, TFT_GREEN);

  char pct[16];
  if (total > 0) snprintf(pct, sizeof(pct), "%d%%", (int)(100LL * written / total));
  else snprintf(pct, sizeof(pct), "%d KB", written / 1024);
  tft.fillRect(0, barY + barH + 4, tft.width(), 24, RETRO_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, RETRO_BG);
  tft.drawString(pct, tft.width() / 2, barY + barH + 8, 4);
}

bool downloadLatestRelease(const String& ssid, const String& pass) {
  drawUpdateScreen("Conectando a WiFi...", "(solo redes 2.4GHz)");

  // AP_STA: mantiene el punto de acceso vivo (para que el navegador que
  // disparó esto siga respondiendo) mientras además nos conectamos como
  // cliente a la red real para salir a internet.
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  wl_status_t status;
  const unsigned long CONNECT_TIMEOUT_MS = 10000;
  while ((status = WiFi.status()) != WL_CONNECTED && millis() - start < CONNECT_TIMEOUT_MS) {
    // Fallo definitivo (SSID no encontrado / auth mal) -> no esperar el timeout completo
    if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED) break;
    delay(200);
  }

  if (WiFi.status() != WL_CONNECTED) {
    drawUpdateScreen("No se pudo conectar (timeout)", "Revisa que tu WiFi sea 2.4GHz");
    delay(3000);
    WiFi.mode(WIFI_AP);
    return false;
  }

  drawUpdateScreen("Descargando ultima version...");

  WiFiClientSecure client;
  client.setInsecure(); // sin validar CA: suficiente para este proyecto hobby

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // GitHub redirige a objects.githubusercontent.com
  http.setTimeout(15000);

  String url = "https://github.com/" OTA_GITHUB_OWNER "/" OTA_GITHUB_REPO
               "/releases/latest/download/" OTA_ASSET_NAME;

  if (!http.begin(client, url)) {
    drawUpdateScreen("No se pudo iniciar la descarga");
    delay(2500);
    WiFi.mode(WIFI_AP);
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    char line[32];
    snprintf(line, sizeof(line), "Error HTTP %d", code);
    const char* hint = (code == 404)
      ? "Aun no hay ningun release publicado"
      : "Toca la pantalla para volver";
    drawUpdateScreen(line, hint);
    delay(3000);
    http.end();
    WiFi.mode(WIFI_AP);
    return false;
  }

  int total = http.getSize(); // -1 si el servidor no manda Content-Length
  WiFiClient* stream = http.getStreamPtr();

  busToSD();
  if (!SD.exists("/firmware")) SD.mkdir("/firmware");
  File f = SD.open("/firmware/" OTA_ASSET_NAME, FILE_WRITE);
  if (!f) {
    drawUpdateScreen("No se pudo escribir en la SD");
    delay(2500);
    http.end();
    WiFi.mode(WIFI_AP);
    return false;
  }

  static uint8_t buf[4096]; // fuera de la pila, igual que en firmware_manager
  int written = 0;
  drawUpdateProgress(0, total);
  unsigned long lastDraw = millis();

  while (http.connected() && (total < 0 || written < total)) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (n <= 0) break;
      f.write(buf, n);
      written += n;
      if (millis() - lastDraw > 120) {
        drawUpdateProgress(written, total);
        lastDraw = millis();
      }
    } else if (!http.connected()) {
      break;
    } else {
      delay(1);
    }
  }

  f.close();
  http.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP); // vuelve a solo-AP para que la página de subida siga sirviendo

  bool ok = written > 0 && (total < 0 || written >= total);
  drawUpdateScreen(ok ? "Descarga completa" : "Descarga incompleta");
  delay(1500);
  return ok;
}

// Escribe /firmware/CYD-Launcher.bin directamente en la partición "launcher"
// (ota_1) — la misma desde la que corre este código — y deja el chip listo
// para reiniciar hacia la nueva versión. No necesita USB/bootloader: el
// truco es sobrescribir la partición activa vía flash SPI mientras el
// código ya cargado sigue corriendo desde caché; solo tiene efecto al
// reiniciar (por eso el reinicio se hace aparte, después de responder al
// navegador — ver handleWifiUpdate en wifi_upload.cpp).
bool flashLauncherSelfUpdate() {
  busToSD();
  File f = SD.open("/firmware/" OTA_ASSET_NAME, FILE_READ);
  if (!f) return false;

  const esp_partition_t* target = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, "launcher");
  size_t maxSize = target ? target->size : (size_t)-1;

  String errMsg;
  if (!validateImage(f, maxSize, errMsg)) {
    f.close();
    drawUpdateScreen("Imagen invalida:", errMsg.c_str());
    delay(3000);
    return false;
  }

  // Target explícito por label "launcher": sin esto, Update.begin() por
  // defecto apuntaría al "siguiente" slot OTA (app0), no al nuestro.
  if (!Update.begin(f.size(), U_FLASH, -1, LOW, "launcher")) {
    f.close();
    drawUpdateScreen("No cabe en la particion launcher");
    delay(3000);
    return false;
  }

  static uint8_t buf[4096];
  size_t writtenBytes = 0;
  size_t fileSize = f.size();
  drawUpdateScreen("Actualizando el launcher...");
  drawUpdateProgress(0, fileSize);
  unsigned long lastDraw = millis();

  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (Update.write(buf, n) != n) {
      f.close();
      Update.abort();
      drawUpdateScreen("Error escribiendo flash");
      delay(3000);
      return false;
    }
    writtenBytes += n;
    if (millis() - lastDraw > 120) {
      drawUpdateProgress(writtenBytes, fileSize);
      lastDraw = millis();
    }
  }
  f.close();

  if (!Update.end(true) || !Update.isFinished()) {
    drawUpdateScreen("Actualizacion incompleta");
    delay(3000);
    return false;
  }

  drawUpdateScreen("Listo! Reiniciando...");
  return true;
}
