#include "hardware.h"
#include <esp_sleep.h>

// ---------- Objetos compartidos (declarados extern en config.h) ----------
TFT_eSPI tft = TFT_eSPI();
SPIClass sharedSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
Preferences prefs;

// Valores de calibración táctil — se cargan desde NVS (ver calibrateTouch()).
// Estos son solo el fallback antes de la primera calibración.
int tsMinX = 300, tsMaxX = 3800, tsMinY = 300, tsMaxY = 3800;

// Rotación actual (0-3, mismo valor para tft y para el táctil). Al rotar,
// las coordenadas crudas del táctil cambian de eje -> conviene recalibrar
// (el menú de Opciones lo hace automáticamente tras rotar).
int screenRotation = 1;

int touchScreenX(int rawX) { return constrain((int)map(rawX, tsMinX, tsMaxX, 0, tft.width()), 0, tft.width() - 1); }
int touchScreenY(int rawY) { return constrain((int)map(rawY, tsMinY, tsMaxY, 0, tft.height()), 0, tft.height() - 1); }

void busToSD() {
  sharedSPI.end();
  sharedSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
}

void busToTouch() {
  sharedSPI.end();
  sharedSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(sharedSPI);
  ts.setRotation(screenRotation);
}

void applyRotation(int rot) {
  screenRotation = ((rot % 4) + 4) % 4;
  prefs.putInt("rotation", screenRotation);
  tft.setRotation(screenRotation);
  busToTouch();
}

// ================== LED RGB de estado ==================
void ledSetup() {
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
}

void ledSet(bool r, bool g, bool b) {
  if (LED_ACTIVE_LOW) { r = !r; g = !g; b = !b; }
  digitalWrite(LED_R, r);
  digitalWrite(LED_G, g);
  digitalWrite(LED_B, b);
}

void ledOff()    { ledSet(false, false, false); }
void ledReady()  { ledSet(false, true,  false); } // verde
void ledBusy()   { ledSet(false, false, true);  } // azul
void ledError()  { ledSet(true,  false, false); } // rojo

// ================== Beep (altavoz piezo/DAC en GPIO26) ==================
// Deshabilitado: esta placa no trae altavoz montado, así que era peso
// muerto (movía el pin sin efecto audible). Se deja la función como
// no-op en vez de borrar las llamadas, por si en el futuro se conecta uno.
void beep(int freqHz, int ms) {
  (void)freqHz;
  (void)ms;
}

void beepOk()    { beep(1200, 60); delay(30); beep(1800, 80); }
void beepError() { beep(300, 150); delay(50); beep(200, 200); }
void beepTick()  { beep(900, 25); }

// ================== Calibración táctil (2 puntos) ==================
// Promedia varias lecturas crudas mientras el usuario mantiene el dedo
// sobre una diana en cada esquina.
void calibrateTouch(bool showIntro) {
  tft.fillScreen(TFT_BLACK);
  if (showIntro) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Calibracion tactil", tft.width() / 2, tft.height() / 2 - 20, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Toca cada diana con precision", tft.width() / 2, tft.height() / 2 + 15, 2);
    delay(1800);
  }

  int targets[2][2] = {
    {14, 14},
    {tft.width() - 14, tft.height() - 14}
  };
  int rawPts[2][2];

  for (int t = 0; t < 2; t++) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(t == 0 ? "Esquina superior izq." : "Esquina inferior der.",
                    tft.width() / 2, 10, 2);

    int cx = targets[t][0], cy = targets[t][1];
    tft.drawLine(cx - 10, cy, cx + 10, cy, TFT_RED);
    tft.drawLine(cx, cy - 10, cx, cy + 10, TFT_RED);
    tft.drawCircle(cx, cy, 8, TFT_RED);

    while (!ts.touched()) delay(20);

    long sumX = 0, sumY = 0;
    int samples = 0;
    unsigned long start = millis();
    while (millis() - start < 400) { // promedia 400ms de lecturas
      if (ts.touched()) {
        TS_Point p = ts.getPoint();
        sumX += p.x; sumY += p.y;
        samples++;
      }
      delay(10);
    }
    rawPts[t][0] = samples ? (int)(sumX / samples) : 2048;
    rawPts[t][1] = samples ? (int)(sumY / samples) : 2048;

    beepTick();
    while (ts.touched()) delay(10);
    delay(200);
  }

  tsMinX = min(rawPts[0][0], rawPts[1][0]);
  tsMaxX = max(rawPts[0][0], rawPts[1][0]);
  tsMinY = min(rawPts[0][1], rawPts[1][1]);
  tsMaxY = max(rawPts[0][1], rawPts[1][1]);

  prefs.putInt("tsMinX", tsMinX);
  prefs.putInt("tsMaxX", tsMaxX);
  prefs.putInt("tsMinY", tsMinY);
  prefs.putInt("tsMaxY", tsMaxY);
  prefs.putBool("calibrated", true);

  beepOk();
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Calibracion guardada", tft.width() / 2, tft.height() / 2, 4);
  delay(1000);
}

// ================== Apagado (deep sleep) ==================
void powerOff() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Apagando...", tft.width() / 2, tft.height() / 2, 4);
  beepTick();
  delay(400);
  digitalWrite(TFT_BL, LOW);
  ledOff();
  esp_deep_sleep_start(); // sin fuente de wake: queda "apagado" hasta RESET/power cycle
}
