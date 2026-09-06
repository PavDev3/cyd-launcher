#include "wifi_upload.h"
#include "hardware.h"
#include "firmware_manager.h"
#include "ui_menu.h"
#include "ui_screens.h"
#include "ota_update.h"
#include <WiFi.h>
#include <WebServer.h>

// ================== AP propio de la CYD para subir/configurar ==================
static const char* WIFI_AP_SSID = "CYD-Launcher";
static const char* WIFI_AP_PASS = "cydlauncher"; // min 8 caracteres para WPA2

static WebServer webServer(80);
static File wifiUploadFile;
static IPAddress apIp;

enum class ApPurpose { Upload, ConfigWifi };
static ApPurpose currentPurpose = ApPurpose::Upload;

// ---------- Página: subir .bin ----------
static void handleUploadPage() {
  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>CYD Launcher</title>"
    "<style>body{font-family:monospace;background:#111;color:#0f0;padding:20px}"
    "input,button{font-size:1.1em;padding:8px;margin:6px 0;width:100%;box-sizing:border-box}"
    "button{background:#0f0;color:#000;border:none;cursor:pointer}"
    "a.del{color:#f55}"
    "li{margin:6px 0}"
    "hr{border-color:#333;margin:20px 0}</style></head>"
    "<body><h2>Subir firmware</h2>"
    "<p>El archivo se guarda en /firmware/ en la SD.</p>"
    "<form method='POST' action='/upload' enctype='multipart/form-data'>"
    "<input type='file' name='file' accept='.bin'>"
    "<button type='submit'>Subir</button></form>"
    "<hr><h3>Archivos en /firmware/</h3><ul>";

  busToSD();
  File dir = SD.open("/firmware");
  if (dir && dir.isDirectory()) {
    File entry = dir.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        String name = entry.name();
        html += "<li>" + name + " (" + String(entry.size() / 1024) + "KB) "
                "<a class='del' href='/delete?file=" + name + "'>[borrar]</a></li>";
      }
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
  }
  html += "</ul></body></html>";

  webServer.send(200, "text/html", html);
}

static void handleUploadResult() {
  webServer.send(200, "text/html",
    "<html><body style='font-family:monospace;background:#111;color:#0f0;padding:20px'>"
    "Subido OK. <a href='/' style='color:#0ff'>Volver</a></body></html>");
}

static void handleUploadData() {
  HTTPUpload& upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String name = upload.filename;
    if (!name.endsWith(".bin") && !name.endsWith(".BIN")) name += ".bin";
    wifiUploadFile = SD.open("/firmware/" + name, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (wifiUploadFile) wifiUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (wifiUploadFile) wifiUploadFile.close();
  }
}

static void handleDeleteFile() {
  String name = webServer.arg("file");
  if (name.length() > 0 && name.indexOf("..") < 0) { // evita salir del directorio
    busToSD();
    SD.remove("/firmware/" + name);
    String txt = name;
    txt.replace(".bin", ".txt");
    txt.replace(".BIN", ".txt");
    SD.remove("/firmware/" + txt);
  }
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

// ---------- Página: configurar WiFi (SSID/clave para actualizaciones) ----------
static void handleWifiConfigPage() {
  String savedSsid = prefs.getString("staSsid", "");

  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>CYD Launcher</title>"
    "<style>body{font-family:monospace;background:#111;color:#0f0;padding:20px}"
    "input,button{font-size:1.1em;padding:8px;margin:6px 0;width:100%;box-sizing:border-box}"
    "button{background:#0f0;color:#000;border:none;cursor:pointer}</style></head>"
    "<body><h2>Conectar WiFi</h2>"
    "<p>Esta red se usa solo para buscar actualizaciones del launcher "
    "en GitHub. Debe ser tu WiFi normal, de <b style='color:#fa0'>2.4GHz</b> "
    "(el ESP32 no soporta 5GHz).</p>"
    "<form method='POST' action='/savewifi'>"
    "<input type='text' name='ssid' placeholder='SSID' value='" + savedSsid + "'>"
    "<input type='password' name='pass' placeholder='Clave'>"
    "<button type='submit'>Guardar</button></form>"
    "</body></html>";

  webServer.send(200, "text/html", html);
}

static void handleSaveWifi() {
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");

  if (ssid.length() == 0) {
    webServer.send(400, "text/html",
      "<html><body style='font-family:monospace;background:#111;color:#f55;padding:20px'>"
      "Falta el SSID. <a href='/' style='color:#0ff'>Volver</a></body></html>");
    return;
  }

  prefs.putString("staSsid", ssid);
  if (pass.length() > 0) prefs.putString("staPass", pass);

  webServer.send(200, "text/html",
    "<html><body style='font-family:monospace;background:#111;color:#0f0;padding:20px'>"
    "Guardado. Ya puedes usar 'Actualizar launcher' desde el menu del dispositivo."
    " <a href='/' style='color:#0ff'>Volver</a></body></html>");
}

// ---------- Pantalla física común (AP activo) ----------
static void drawApScreen(IPAddress ip, const char* title) {
  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(RETRO_TITLE, RETRO_BG);
  tft.drawString(title, tft.width() / 2, 10, 4);

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

static void runApSession(ApPurpose purpose) {
  currentPurpose = purpose;

  busToSD();
  if (!SD.exists("/firmware")) SD.mkdir("/firmware");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  apIp = WiFi.softAPIP();

  if (purpose == ApPurpose::Upload) {
    webServer.on("/", HTTP_GET, handleUploadPage);
    webServer.on("/upload", HTTP_POST, handleUploadResult, handleUploadData);
    webServer.on("/delete", HTTP_GET, handleDeleteFile);
  } else {
    webServer.on("/", HTTP_GET, handleWifiConfigPage);
    webServer.on("/savewifi", HTTP_POST, handleSaveWifi);
  }
  webServer.begin();

  const char* title = (purpose == ApPurpose::Upload) ? "Subir .bin" : "Conectar WiFi";
  drawApScreen(apIp, title);

  int exitX = tft.width() / 2 - 60, exitY = tft.height() - 50, exitW = 120, exitH = 40;

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
  webServer.close();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  if (purpose == ApPurpose::Upload) scanFirmwareDir(); // pudo haber archivos nuevos/borrados
  busToTouch();
  beepOk();
}

// ================== Sub-menú "WiFi" ==================
void runWifiMenu() {
  OptionRow rows[3];
  strncpy(rows[0].label, "Subir .bin",         sizeof(rows[0].label) - 1); rows[0].color = TFT_CYAN;
  strncpy(rows[1].label, "Actualizar launcher", sizeof(rows[1].label) - 1); rows[1].color = TFT_GREEN;
  strncpy(rows[2].label, "Conectar WiFi",       sizeof(rows[2].label) - 1); rows[2].color = TFT_YELLOW;

  while (ts.touched()) delay(10);
  int selected = -1;
  drawRowMenu("WiFi", rows, 3, selected);

  while (true) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int x = touchScreenX(p.x), y = touchScreenY(p.y);
      int row = hitRowMenu(x, y, 3);

      if (row != selected) {
        selected = row;
        drawRowMenu("WiFi", rows, 3, selected);
        if (row >= 0) beepTick();
      }

      while (ts.touched()) delay(10);
      delay(120);

      if (row == -1) return; // tocó fuera -> volver a Opciones
      if (row == 0) { runApSession(ApPurpose::Upload); return; }
      if (row == 1) {
        // "Actualizar launcher" necesita WiFi guardado; si no hay, te manda
        // directo a configurarlo primero.
        if (prefs.getString("staSsid", "").length() == 0) {
          tft.fillScreen(RETRO_BG);
          tft.setTextDatum(MC_DATUM);
          tft.setTextColor(TFT_YELLOW, RETRO_BG);
          tft.drawString("No hay WiFi configurado", tft.width() / 2, tft.height() / 2 - 15, 2);
          tft.drawString("Configuralo primero...", tft.width() / 2, tft.height() / 2 + 15, 2);
          delay(1800);
          runApSession(ApPurpose::ConfigWifi);
          return;
        }
        runManualUpdateCheck();
        return;
      }
      if (row == 2) { runApSession(ApPurpose::ConfigWifi); return; }
    }
    delay(20);
  }
}
