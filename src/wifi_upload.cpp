#include "wifi_upload.h"
#include "hardware.h"
#include "firmware_manager.h"
#include "ui_menu.h"
#include "ota_update.h"
#include <WiFi.h>
#include <WebServer.h>

// ================== Subida por WiFi (WebUI) ==================
static const char* WIFI_AP_SSID = "CYD-Launcher";
static const char* WIFI_AP_PASS = "cydlauncher"; // min 8 caracteres para WPA2

static WebServer webServer(80);
static File wifiUploadFile;
static IPAddress apIp; // se usa para redibujar la pantalla tras un intento de update

static void drawWifiScreen(IPAddress ip);

static void handleWifiUploadPage() {
  String savedSsid = prefs.getString("staSsid", "");

  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>CYD Launcher</title>"
    "<style>body{font-family:monospace;background:#111;color:#0f0;padding:20px}"
    "input,button{font-size:1.1em;padding:8px;margin:6px 0;width:100%;box-sizing:border-box}"
    "button{background:#0f0;color:#000;border:none;cursor:pointer}"
    "hr{border-color:#333;margin:24px 0}"
    "h3{color:#0ff}</style></head>"
    "<body><h2>CYD Launcher</h2>"
    "<h3>Subir firmware</h3>"
    "<p>El archivo se guarda en /firmware/ en la SD.</p>"
    "<form method='POST' action='/upload' enctype='multipart/form-data'>"
    "<input type='file' name='file' accept='.bin'>"
    "<button type='submit'>Subir</button></form>"
    "<hr>"
    "<h3>Actualizar launcher (Internet)</h3>"
    "<p>Descarga la ultima version publicada de "
    OTA_GITHUB_OWNER "/" OTA_GITHUB_REPO
    " en GitHub, la flashea y reinicia el dispositivo automaticamente.</p>"
    "<p style='color:#fa0'>Importante: tu WiFi debe ser de <b>2.4GHz</b> "
    "(el ESP32 no puede conectarse a redes 5GHz). Es tu red normal con "
    "internet, no esta red \"" + String(WIFI_AP_SSID) + "\".</p>"
    "<form method='POST' action='/update'>"
    "<input type='text' name='ssid' placeholder='SSID de tu WiFi' value='" + savedSsid + "'>"
    "<input type='password' name='pass' placeholder='Clave (dejar vacio para reusar la guardada)'>"
    "<button type='submit'>Actualizar ahora</button></form>"
    "</body></html>";

  webServer.send(200, "text/html", html);
}

static void handleWifiUpdate() {
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");

  if (ssid.length() > 0) {
    prefs.putString("staSsid", ssid);
    if (pass.length() > 0) prefs.putString("staPass", pass);
  } else {
    ssid = prefs.getString("staSsid", "");
  }
  if (pass.length() == 0) {
    pass = prefs.getString("staPass", "");
  }

  if (ssid.length() == 0) {
    webServer.send(400, "text/html",
      "<html><body style='font-family:monospace;background:#111;color:#f55;padding:20px'>"
      "Falta el SSID de tu WiFi. <a href='/' style='color:#0ff'>Volver</a></body></html>");
    return;
  }

  UpdateResult result = downloadLatestRelease(ssid, pass);
  bool flashed = (result == UpdateResult::Downloaded) && flashLauncherSelfUpdate();

  // La pantalla de resultado tapó el botón "Salir" — lo restauramos salvo
  // que vayamos a reiniciar de todas formas.
  if (!flashed) drawWifiScreen(apIp);

  String msg;
  if (flashed) {
    msg = "Actualizado! El dispositivo se va a reiniciar solo en unos segundos con la nueva version.";
  } else if (result == UpdateResult::AlreadyLatest) {
    msg = "Ya tienes la ultima version (" LAUNCHER_VERSION "). No hacia falta actualizar.";
  } else if (result == UpdateResult::Downloaded) {
    msg = "Se descargo pero no se pudo flashear automaticamente. "
          "Puedes elegir " OTA_ASSET_NAME " a mano en el menu principal.";
  } else {
    msg = "<span style='color:#f55'>Fallo la descarga. Revisa la pantalla del dispositivo.</span>";
  }

  webServer.send(200, "text/html",
    String("<html><body style='font-family:monospace;background:#111;color:#0f0;padding:20px'>") +
    msg + " <a href='/' style='color:#0ff'>Volver</a></body></html>");

  if (flashed) {
    delay(800); // dar tiempo a que la respuesta HTTP llegue al navegador
    esp_restart();
  }
}

static void handleWifiUploadResult() {
  webServer.send(200, "text/html",
    "<html><body style='font-family:monospace;background:#111;color:#0f0;padding:20px'>"
    "Subido OK. <a href='/' style='color:#0ff'>Subir otro</a></body></html>");
}

static void handleWifiUploadData() {
  HTTPUpload& upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String name = upload.filename;
    if (!name.endsWith(".bin") && !name.endsWith(".BIN")) name += ".bin";
    String path = "/firmware/" + name;
    wifiUploadFile = SD.open(path, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (wifiUploadFile) wifiUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (wifiUploadFile) wifiUploadFile.close();
  }
}

static void drawWifiScreen(IPAddress ip) {
  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(RETRO_TITLE, RETRO_BG);
  tft.drawString("Subida por WiFi", tft.width() / 2, 10, 4);

  int y = 60;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, RETRO_BG);
  char line[64];
  snprintf(line, sizeof(line), "Red: %s", WIFI_AP_SSID);
  tft.drawString(line, 20, y, 2); y += 24;
  snprintf(line, sizeof(line), "Clave: %s", WIFI_AP_PASS);
  tft.drawString(line, 20, y, 2); y += 24;
  tft.setTextColor(RETRO_SIGN, RETRO_BG);
  snprintf(line, sizeof(line), "Abrir: http://%s", ip.toString().c_str());
  tft.drawString(line, 20, y, 2); y += 34;

  tft.setTextColor(TFT_WHITE, RETRO_BG);
  tft.drawString("Conectate a esa red y abre esa", 20, y, 2); y += 20;
  tft.drawString("direccion en el navegador.", 20, y, 2);

  tft.fillRoundRect(tft.width() / 2 - 60, tft.height() - 50, 120, 40, 8, TFT_RED);
  tft.drawRoundRect(tft.width() / 2 - 60, tft.height() - 50, 120, 40, 8, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_RED);
  tft.drawString("Salir", tft.width() / 2, tft.height() - 30, 4);
}

void runWifiUploadMode() {
  busToSD();
  if (!SD.exists("/firmware")) SD.mkdir("/firmware");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  apIp = WiFi.softAPIP();

  webServer.on("/", HTTP_GET, handleWifiUploadPage);
  webServer.on("/upload", HTTP_POST, handleWifiUploadResult, handleWifiUploadData);
  webServer.on("/update", HTTP_POST, handleWifiUpdate);
  webServer.begin();

  drawWifiScreen(apIp);

  int exitX = tft.width() / 2 - 60, exitY = tft.height() - 50, exitW = 120, exitH = 40;

  // Esperar a que suelten el dedo del tap que abrió esta pantalla
  busToTouch();
  while (ts.touched()) delay(10);
  busToSD();

  unsigned long lastTouchCheck = 0;
  bool exit = false;
  while (!exit) {
    webServer.handleClient();

    if (millis() - lastTouchCheck > 150) {
      lastTouchCheck = millis();
      busToTouch();
      if (ts.touched()) {
        TS_Point p = ts.getPoint();
        int x = touchScreenX(p.x), y = touchScreenY(p.y);
        if (x >= exitX && x <= exitX + exitW && y >= exitY && y <= exitY + exitH) {
          exit = true;
        }
        while (ts.touched()) delay(10);
      }
      busToSD();
    }
  }

  webServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  scanFirmwareDir(); // puede haber archivos nuevos
  busToTouch();
  beepOk();
  drawMenu();
}
