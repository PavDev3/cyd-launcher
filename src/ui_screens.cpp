#include "ui_screens.h"
#include "hardware.h"
#include "firmware_manager.h"
#include "wifi_upload.h"

// ---------- Sub-menú genérico de lista compacta ----------
void drawRowMenu(const char* title, OptionRow* rows, int count, int selected) {
  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(RETRO_TITLE, RETRO_BG);
  tft.drawString(title, tft.width() / 2, 4, 2);

  int y = OPT_TOP;
  for (int i = 0; i < count; i++) {
    int x = MARGIN, w = tft.width() - 2 * MARGIN;
    uint16_t bg = (i == selected) ? RETRO_SELECT : RETRO_ITEM_BG;
    tft.fillRoundRect(x, y, w, OPT_ITEM_H, 6, bg);
    tft.drawRoundRect(x, y, w, OPT_ITEM_H, 6, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor((i == selected) ? TFT_BLACK : TFT_WHITE, bg);
    tft.drawString(rows[i].label, tft.width() / 2, y + OPT_ITEM_H / 2, 2);
    y += OPT_ITEM_H + OPT_ITEM_GAP;
  }

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(RETRO_FOOTER, RETRO_BG);
  tft.drawString("Toca fuera de la lista para volver", tft.width() / 2, tft.height() - 6, 1);
}

// -1 = fuera de todas las filas (volver), 0..count-1 = fila tocada
int hitRowMenu(int touchX, int touchY, int count) {
  int y = OPT_TOP;
  for (int i = 0; i < count; i++) {
    int x = MARGIN, w = tft.width() - 2 * MARGIN;
    if (touchX >= x && touchX <= x + w && touchY >= y && touchY <= y + OPT_ITEM_H) return i;
    y += OPT_ITEM_H + OPT_ITEM_GAP;
  }
  return -1;
}

// ---------- Submenú "Pantalla": Calibrar / Rotar ----------
void runScreenMenu() {
  OptionRow rows[2];
  strncpy(rows[0].label, "Calibrar tactil", sizeof(rows[0].label) - 1);
  rows[0].color = TFT_YELLOW;
  rows[1].color = TFT_GREEN;

  while (ts.touched()) delay(10);
  int selected = -1;

  while (true) {
    snprintf(rows[1].label, sizeof(rows[1].label), "Rotar (%d grados)", screenRotation * 90);
    drawRowMenu("Pantalla", rows, 2, selected);

    // Espera un toque
    while (!ts.touched()) delay(20);
    TS_Point p = ts.getPoint();
    int x = touchScreenX(p.x), y = touchScreenY(p.y);
    int row = hitRowMenu(x, y, 2);
    selected = row;
    if (row >= 0) beepTick();
    while (ts.touched()) delay(10);
    delay(120);

    if (row == -1) return; // volver a Opciones
    if (row == 0) { calibrateTouch(false); selected = -1; continue; }
    if (row == 1) {
      // Rotar y recalibrar (los ejes crudos del táctil cambian con la rotación)
      applyRotation(screenRotation + 1);
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("Pantalla rotada", tft.width() / 2, tft.height() / 2 - 15, 2);
      tft.drawString("Recalibrando tactil...", tft.width() / 2, tft.height() / 2 + 15, 2);
      delay(1200);
      calibrateTouch(false);
      selected = -1;
      continue;
    }
  }
}

// ---------- Submenú "Backups": lista /backups/, Restaurar / Borrar ----------
static void refreshBackupRows(OptionRow* rows) {
  for (int i = 0; i < backupCount; i++) {
    char buf[26];
    snprintf(buf, sizeof(buf), "%s (%.1fMB)", backupApps[i].label, backupApps[i].size / 1024.0 / 1024.0);
    strncpy(rows[i].label, buf, sizeof(rows[i].label) - 1);
    rows[i].label[sizeof(rows[i].label) - 1] = '\0';
    rows[i].color = TFT_CYAN;
  }
}

// Bloquea hasta que se toque una fila (0..count-1) o se toque fuera (-1)
static int waitRowTap(const char* title, OptionRow* rows, int count) {
  int sel = -1;
  drawRowMenu(title, rows, count, sel);
  while (true) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int x = touchScreenX(p.x), y = touchScreenY(p.y);
      int row = hitRowMenu(x, y, count);
      if (row != sel) {
        sel = row;
        drawRowMenu(title, rows, count, sel);
        if (row >= 0) beepTick();
      }
      while (ts.touched()) delay(10);
      delay(120);
      return row;
    }
    delay(20);
  }
}

void runBackupsMenu() {
  while (ts.touched()) delay(10);
  scanBackupsDir();

  if (backupCount == 0) {
    tft.fillScreen(RETRO_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(RETRO_SHADOW, RETRO_BG);
    tft.drawString("Sin backups guardados", tft.width() / 2, tft.height() / 2, 2);
    while (!ts.touched()) delay(20);
    while (ts.touched()) delay(10);
    return;
  }

  OptionRow rows[MAX_BACKUPS];
  refreshBackupRows(rows);

  while (true) {
    int row = waitRowTap("Backups", rows, backupCount);
    if (row == -1) return; // volver a Opciones

    AppEntry chosen = backupApps[row]; // copia: scanBackupsDir() reescribe el array al borrar

    OptionRow actionRows[2];
    strncpy(actionRows[0].label, "Restaurar", sizeof(actionRows[0].label) - 1); actionRows[0].color = TFT_GREEN;
    strncpy(actionRows[1].label, "Borrar",    sizeof(actionRows[1].label) - 1); actionRows[1].color = TFT_RED;

    while (ts.touched()) delay(10);
    int action = waitRowTap(chosen.label, actionRows, 2);

    if (action == 0) { // Restaurar
      if (confirmFlash(chosen)) {
        if (flashFromSD(chosen)) {
          ledReady();
          beepOk();
          tft.fillScreen(TFT_BLACK);
          tft.setTextDatum(MC_DATUM);
          tft.setTextColor(TFT_GREEN, TFT_BLACK);
          tft.drawString("Restaurado, arrancando...", tft.width() / 2, tft.height() / 2, 4);
          delay(500);
          esp_restart();
        }
      }
    } else if (action == 1) { // Borrar
      deleteBackupFromSD(chosen);
      scanBackupsDir();
      if (backupCount == 0) return;
      refreshBackupRows(rows);
    }
  }
}

// ---------- Menú principal de Opciones: Pantalla / WiFi / Backups / Info / Apagar ----------
void runOptionsMenu() {
  OptionRow rows[5];
  strncpy(rows[0].label, "Pantalla",         sizeof(rows[0].label) - 1); rows[0].color = TFT_YELLOW;
  strncpy(rows[1].label, "WiFi",             sizeof(rows[1].label) - 1); rows[1].color = TFT_CYAN;
  strncpy(rows[2].label, "Backups",          sizeof(rows[2].label) - 1); rows[2].color = TFT_ORANGE;
  strncpy(rows[3].label, "Info del sistema", sizeof(rows[3].label) - 1); rows[3].color = RETRO_SIGN;
  strncpy(rows[4].label, "Apagar",           sizeof(rows[4].label) - 1); rows[4].color = TFT_RED;

  while (ts.touched()) delay(10);

  int selected = -1;
  drawRowMenu("Opciones", rows, 5, selected);

  while (true) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int x = touchScreenX(p.x), y = touchScreenY(p.y);
      int row = hitRowMenu(x, y, 5);

      if (row != selected) {
        selected = row;
        drawRowMenu("Opciones", rows, 5, selected);
        if (row >= 0) beepTick();
      }

      while (ts.touched()) delay(10);
      delay(120);

      if (row == -1) return; // tocó fuera -> volver al menú principal
      if (row == 0) { runScreenMenu(); return; }
      if (row == 1) { runWifiMenu(); return; }
      if (row == 2) { runBackupsMenu(); return; }
      if (row == 3) {
        drawDeviceInfo();
        while (!ts.touched()) delay(20);
        while (ts.touched()) delay(10);
        return;
      }
      if (row == 4) { powerOff(); } // no vuelve
    }
    delay(20);
  }
}

// ---------- Pantalla de info por app (toque corto) ----------
int deleteBtnX() { return tft.width() / 2 - 65; }
int deleteBtnY() { return tft.height() - 46; }

bool hitDeleteButton(int touchX, int touchY) {
  int x = deleteBtnX(), y = deleteBtnY();
  return touchX >= x && touchX <= x + DELETE_BTN_W && touchY >= y && touchY <= y + DELETE_BTN_H;
}

void drawAppInfo(AppEntry& app) {
  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(RETRO_TITLE, RETRO_BG);
  tft.drawString(app.label, tft.width() / 2, 12, 4);

  int y = 56;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, RETRO_BG);

  char line[64];
  snprintf(line, sizeof(line), "Tamano: %.2f MB (%lu bytes)", app.size / 1024.0 / 1024.0, (unsigned long)app.size);
  tft.drawString(line, 16, y, 2); y += 24;

  snprintf(line, sizeof(line), "Modificado: %s", app.dateStr);
  tft.drawString(line, 16, y, 2); y += 24;

  snprintf(line, sizeof(line), "Ruta: %s", app.path);
  tft.drawString(line, 16, y, 2); y += 24;

  if (app.desc[0]) {
    tft.setTextColor(RETRO_SIGN, RETRO_BG);
    tft.drawString(app.desc, 16, y, 2);
    y += 24;
  }

  int bx = deleteBtnX(), by = deleteBtnY();
  tft.fillRoundRect(bx, by, DELETE_BTN_W, DELETE_BTN_H, 6, TFT_RED);
  tft.drawRoundRect(bx, by, DELETE_BTN_W, DELETE_BTN_H, 6, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawString("Borrar .bin", bx + DELETE_BTN_W / 2, by + DELETE_BTN_H / 2, 2);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(RETRO_FOOTER, RETRO_BG);
  tft.drawString("Toca fuera para volver", tft.width() / 2, tft.height() - 6, 1);
}

// true = confirmado; bloquea hasta tocar Si/No
bool confirmDelete(AppEntry& app) {
  int yesX = tft.width() / 2 - BTN_W - 10, btnY = tft.height() / 2 + 30;
  int noX = tft.width() / 2 + 10;

  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_RED, RETRO_BG);
  tft.drawString("Borrar de la SD:", tft.width() / 2, tft.height() / 2 - 40, 4);
  tft.setTextColor(RETRO_SIGN, RETRO_BG);
  tft.drawString(app.label, tft.width() / 2, tft.height() / 2 - 5, 4);

  tft.fillRoundRect(yesX, btnY, BTN_W, BTN_H, 8, TFT_RED);
  tft.drawRoundRect(yesX, btnY, BTN_W, BTN_H, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawString("Si", yesX + BTN_W / 2, btnY + BTN_H / 2, 4);

  tft.fillRoundRect(noX, btnY, BTN_W, BTN_H, 8, TFT_GREEN);
  tft.drawRoundRect(noX, btnY, BTN_W, BTN_H, 8, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
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
        beepTick();
        while (ts.touched()) delay(10);
        return onYes;
      }
    }
    delay(20);
  }
}

// ---------- Pantalla de info del dispositivo ----------
void drawDeviceInfo() {
  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(RETRO_TITLE, RETRO_BG);
  tft.drawString("Device Info", tft.width() / 2, 12, 4);

  int y = 56;
  const int LINE_H = 22;
  tft.setTextDatum(TL_DATUM);
  char line[64];

  tft.setTextColor(RETRO_SIGN, RETRO_BG);
  snprintf(line, sizeof(line), "Version launcher: %s", LAUNCHER_VERSION);
  tft.drawString(line, 16, y, 2); y += LINE_H;

  tft.setTextColor(TFT_WHITE, RETRO_BG);
  uint64_t mac = ESP.getEfuseMac();
  snprintf(line, sizeof(line), "MAC: %04X%08X",
           (uint16_t)(mac >> 32), (uint32_t)mac);
  tft.drawString(line, 16, y, 2); y += LINE_H;

  snprintf(line, sizeof(line), "Chip: %s rev %d", ESP.getChipModel(), ESP.getChipRevision());
  tft.drawString(line, 16, y, 2); y += LINE_H;

  snprintf(line, sizeof(line), "Flash: %.1f MB", ESP.getFlashChipSize() / 1024.0 / 1024.0);
  tft.drawString(line, 16, y, 2); y += LINE_H;

  snprintf(line, sizeof(line), "Heap libre: %lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
  tft.drawString(line, 16, y, 2); y += LINE_H;

  busToSD();
  if (SD.begin(SD_CS, sharedSPI, 20000000)) {
    uint64_t total = SD.totalBytes();
    uint64_t used = SD.usedBytes();
    snprintf(line, sizeof(line), "SD: %.1f / %.1f GB usados",
             used / 1024.0 / 1024.0 / 1024.0, total / 1024.0 / 1024.0 / 1024.0);
    tft.drawString(line, 16, y, 2); y += LINE_H;
  }
  busToTouch();

  snprintf(line, sizeof(line), "Ultima app: %s", lastAppLabel[0] ? lastAppLabel : "(ninguna)");
  tft.drawString(line, 16, y, 2); y += LINE_H;

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(RETRO_FOOTER, RETRO_BG);
  tft.drawString("Toca en cualquier lado para volver", tft.width() / 2, tft.height() - 10, 2);
}

// ---------- Confirmación táctil de flasheo ----------
void drawConfirm(AppEntry& app, int yesX, int yesY, int noX, int noY) {
  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(RETRO_TITLE, RETRO_BG);
  tft.drawString("Flashear:", tft.width() / 2, 60, 4);
  tft.setTextColor(RETRO_SIGN, RETRO_BG);
  tft.drawString(app.label, tft.width() / 2, 95, 4);

  char warn[48];
  snprintf(warn, sizeof(warn), "Se reemplazara la app activa (%.1fMB)", app.size / 1024.0 / 1024.0);
  tft.setTextColor(TFT_WHITE, RETRO_BG);
  tft.drawString(warn, tft.width() / 2, 130, 2);
  tft.drawString("(se hace backup automatico antes)", tft.width() / 2, 150, 2);

  tft.fillRoundRect(yesX, yesY, BTN_W, BTN_H, 8, TFT_GREEN);
  tft.drawRoundRect(yesX, yesY, BTN_W, BTN_H, 8, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.drawString("Si", yesX + BTN_W / 2, yesY + BTN_H / 2, 4);

  tft.fillRoundRect(noX, noY, BTN_W, BTN_H, 8, TFT_RED);
  tft.drawRoundRect(noX, noY, BTN_W, BTN_H, 8, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_RED);
  tft.drawString("No", noX + BTN_W / 2, noY + BTN_H / 2, 4);
}

// true = confirmado, false = cancelado; bloquea hasta que se toque un botón
bool confirmFlash(AppEntry& app) {
  int yesX = tft.width() / 2 - BTN_W - 10, btnY = 190;
  int noX = tft.width() / 2 + 10;

  drawConfirm(app, yesX, btnY, noX, btnY);

  // Esperar a soltar el dedo del tap que abrió esta pantalla
  while (ts.touched()) delay(10);
  delay(150);

  while (true) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int x = touchScreenX(p.x);
      int y = touchScreenY(p.y);

      bool onYes = x >= yesX && x <= yesX + BTN_W && y >= btnY && y <= btnY + BTN_H;
      bool onNo  = x >= noX  && x <= noX  + BTN_W && y >= btnY && y <= btnY + BTN_H;

      if (onYes || onNo) {
        beepTick();
        while (ts.touched()) delay(10);
        return onYes;
      }
    }
    delay(20);
  }
}
