#include "wifi_upload.h"
#include "hardware.h"
#include "firmware_manager.h"
#include "ui_menu.h"
#include <WiFi.h>
#include <WebServer.h>

// ================== Subida por WiFi (WebUI) ==================
static const char* WIFI_AP_SSID = "CYD-Launcher";
static const char* WIFI_AP_PASS = "cydlauncher"; // min 8 caracteres para WPA2

static WebServer webServer(80);
static File wifiUploadFile;

static const char* UPLOAD_PAGE_HTML =
  "<!DOCTYPE html><html><head><meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>CYD Launcher</title>"
  "<style>body{font-family:monospace;background:#111;color:#0f0;padding:20px}"
  "input,button{font-size:1.1em;padding:8px;margin:6px 0}"
  "button{background:#0f0;color:#000;border:none;cursor:pointer}</style></head>"
  "<body><h2>CYD Launcher &mdash; Subir firmware</h2>"
  "<p>El archivo se guarda en /firmware/ en la SD.</p>"
  "<form method='POST' action='/upload' enctype='multipart/form-data'>"
  "<input type='file' name='file' accept='.bin'><br>"
  "<button type='submit'>Subir</button></form></body></html>";

static void handleWifiUploadPage() {
  webServer.send(200, "text/html", UPLOAD_PAGE_HTML);
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
  IPAddress ip = WiFi.softAPIP();

  webServer.on("/", HTTP_GET, handleWifiUploadPage);
  webServer.on("/upload", HTTP_POST, handleWifiUploadResult, handleWifiUploadData);
  webServer.begin();

  drawWifiScreen(ip);

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
