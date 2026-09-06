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

// Consulta la API de GitHub por el tag_name del último release.
// Devuelve "" si falla (sin bloquear el flujo: se sigue igual a la descarga).
static String fetchLatestTag() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);

  String url = "https://api.github.com/repos/" OTA_GITHUB_OWNER "/" OTA_GITHUB_REPO "/releases/latest";
  if (!http.begin(client, url)) return "";
  http.addHeader("User-Agent", OTA_GITHUB_OWNER "-cyd-launcher");
  http.addHeader("Accept", "application/vnd.github+json");

  int code = http.GET();
  String tag = "";
  if (code == HTTP_CODE_OK) {
    String body = http.getString();
    int idx = body.indexOf("\"tag_name\"");
    if (idx >= 0) {
      int colon = body.indexOf(':', idx);
      int q1 = body.indexOf('"', colon + 1);
      int q2 = (q1 >= 0) ? body.indexOf('"', q1 + 1) : -1;
      if (q1 >= 0 && q2 > q1) tag = body.substring(q1 + 1, q2);
    }
  }
  http.end();
  return tag;
}

// Asume que el WiFi ya está conectado (STA o AP_STA). Comprueba versión y,
// si hay una distinta, descarga el .bin (+ .txt best-effort). No toca el
// modo WiFi al terminar — eso lo decide cada llamador (ver más abajo).
static UpdateResult checkAndDownload() {
  drawUpdateScreen("Comprobando version...");
  String latestTag = fetchLatestTag();
  if (latestTag.length() > 0 && latestTag == LAUNCHER_VERSION) {
    drawUpdateScreen("Ya tienes la ultima version", LAUNCHER_VERSION);
    delay(2000);
    return UpdateResult::AlreadyLatest;
  }
  // Si la consulta de version falla (latestTag vacio), seguimos igual:
  // la descarga directa de releases/latest/download/ es independiente.

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
    return UpdateResult::Failed;
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
    return UpdateResult::Failed;
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
    return UpdateResult::Failed;
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

  bool ok = written > 0 && (total < 0 || written >= total);

  // Descarga best-effort del .txt de versión (se muestra luego en la
  // pantalla de info del archivo). Si falla, no afecta el resultado del
  // .bin, que es lo que realmente importa.
  if (ok) {
    HTTPClient httpTxt;
    httpTxt.setTimeout(8000);
    String txtUrl = "https://github.com/" OTA_GITHUB_OWNER "/" OTA_GITHUB_REPO
                     "/releases/latest/download/" OTA_ASSET_TXT_NAME;
    if (httpTxt.begin(client, txtUrl)) {
      if (httpTxt.GET() == HTTP_CODE_OK) {
        File txtFile = SD.open("/firmware/" OTA_ASSET_TXT_NAME, FILE_WRITE);
        if (txtFile) {
          txtFile.print(httpTxt.getString());
          txtFile.close();
        }
      }
      httpTxt.end();
    }
  }

  drawUpdateScreen(ok ? "Descarga completa" : "Descarga incompleta");
  delay(1500);
  return ok ? UpdateResult::Downloaded : UpdateResult::Failed;
}

UpdateResult downloadLatestRelease(const String& ssid, const String& pass) {
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
    return UpdateResult::Failed;
  }

  syncTimeViaNtp(); // aprovecha la conexion para poner hora real (fechas de archivos)

  UpdateResult result = checkAndDownload();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP); // vuelve a solo-AP para que la página de subida siga sirviendo
  return result;
}

// Comprobación silenciosa al arrancar: usa el WiFi guardado (sin AP, sin
// pantalla de subida) para conectar, comprobar versión y, si hay una
// distinta, descargarla y auto-flashearse. No requiere menú ni navegador.
// Devuelve true solo si terminó actualizando (en cuyo caso ya reinició).
bool autoCheckAndUpdateOnBoot() {
  String ssid = prefs.getString("staSsid", "");
  if (ssid.length() == 0) return false; // WiFi nunca configurado -> no hacer nada
  String pass = prefs.getString("staPass", "");

  char line2[48];
  snprintf(line2, sizeof(line2), "Red: %s", ssid.c_str());
  drawUpdateScreen("Conectando WiFi guardado...", line2);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  wl_status_t status;
  const unsigned long BOOT_CONNECT_TIMEOUT_MS = 6000; // rapido: no alargar el arranque si no hay WiFi
  while ((status = WiFi.status()) != WL_CONNECTED && millis() - start < BOOT_CONNECT_TIMEOUT_MS) {
    if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED) break;
    delay(150);
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_OFF);
    return false; // sin red disponible ahora mismo, seguimos arrancando normal
  }

  syncTimeViaNtp();

  UpdateResult result = checkAndDownload();
  bool flashed = false;
  if (result == UpdateResult::Downloaded) {
    flashed = flashLauncherSelfUpdate();
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  if (flashed) {
    delay(500);
    esp_restart();
  }
  return flashed;
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

// ---------- Confirmación antes de actualizar (flujo manual) ----------
static bool confirmUpdate(const String& newVersion) {
  int yesX = tft.width() / 2 - BTN_W - 10, btnY = tft.height() / 2 + 25;
  int noX = tft.width() / 2 + 10;

  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(RETRO_TITLE, RETRO_BG);
  tft.drawString("Actualizacion disponible", tft.width() / 2, tft.height() / 2 - 55, 2);
  tft.setTextColor(RETRO_SIGN, RETRO_BG);
  tft.drawString(newVersion, tft.width() / 2, tft.height() / 2 - 22, 4);
  tft.setTextColor(TFT_WHITE, RETRO_BG);
  tft.drawString("Version actual: " LAUNCHER_VERSION, tft.width() / 2, tft.height() / 2 + 4, 2);

  tft.fillRoundRect(yesX, btnY, BTN_W, BTN_H, 8, TFT_GREEN);
  tft.drawRoundRect(yesX, btnY, BTN_W, BTN_H, 8, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.drawString("Si", yesX + BTN_W / 2, btnY + BTN_H / 2, 4);

  tft.fillRoundRect(noX, btnY, BTN_W, BTN_H, 8, TFT_RED);
  tft.drawRoundRect(noX, btnY, BTN_W, BTN_H, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawString("No", noX + BTN_W / 2, btnY + BTN_H / 2, 4);

  while (ts.touched()) delay(10);
  delay(150);

  while (true) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int x = touchScreenX(p.x), y = touchScreenY(p.y);
      bool onYes = x >= yesX && x <= yesX + BTN_W && y >= btnY && y <= btnY + BTN_H;
      bool onNo  = x >= noX  && x <= noX  + BTN_W && y >= btnY && y <= btnY + BTN_H;
      if (onYes || onNo) {
        while (ts.touched()) delay(10);
        return onYes;
      }
    }
    delay(20);
  }
}

// Comprobación manual: conecta con el WiFi guardado, muestra la versión
// disponible y pide confirmación antes de descargar/flashear. Asume que
// el llamador ya comprobó que hay credenciales guardadas.
void runManualUpdateCheck() {
  String ssid = prefs.getString("staSsid", "");
  String pass = prefs.getString("staPass", "");

  char line2[48];
  snprintf(line2, sizeof(line2), "Red: %s", ssid.c_str());
  drawUpdateScreen("Conectando WiFi...", line2);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  wl_status_t status;
  while ((status = WiFi.status()) != WL_CONNECTED && millis() - start < 10000) {
    if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED) break;
    delay(150);
  }

  if (WiFi.status() != WL_CONNECTED) {
    drawUpdateScreen("No se pudo conectar", "Revisa que tu WiFi sea 2.4GHz");
    delay(3000);
    WiFi.mode(WIFI_OFF);
    return;
  }

  syncTimeViaNtp();

  drawUpdateScreen("Comprobando version...");
  String latestTag = fetchLatestTag();

  if (latestTag.length() == 0) {
    drawUpdateScreen("No se pudo comprobar version", "Revisa tu conexion a internet");
    delay(2500);
    WiFi.mode(WIFI_OFF);
    return;
  }

  if (latestTag == LAUNCHER_VERSION) {
    drawUpdateScreen("Ya tienes la ultima version", LAUNCHER_VERSION);
    delay(2000);
    WiFi.mode(WIFI_OFF);
    return;
  }

  if (!confirmUpdate(latestTag)) {
    WiFi.mode(WIFI_OFF);
    return;
  }

  UpdateResult result = checkAndDownload();
  bool flashed = (result == UpdateResult::Downloaded) && flashLauncherSelfUpdate();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  if (flashed) {
    delay(500);
    esp_restart();
  }
}
