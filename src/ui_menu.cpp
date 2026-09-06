#include "ui_menu.h"

// ---------- Identicon: icono pixel-art derivado del nombre ----------
static const uint16_t ICON_PALETTE[6] = {
  TFT_CYAN, TFT_MAGENTA, TFT_YELLOW, TFT_GREEN, TFT_ORANGE, TFT_RED
};

static uint32_t simpleHash(const char* s) {
  uint32_t h = 5381;
  while (*s) h = ((h << 5) + h) + (uint8_t)(*s++);
  return h;
}

// Dibuja un icono 5x5 simétrico (estilo identicon) en un cuadro size x size
void drawIdenticon(int x, int y, int size, const char* label) {
  uint32_t h = simpleHash(label);
  uint16_t color = ICON_PALETTE[h % 6];
  int cell = size / 5;

  tft.fillRect(x, y, size, size, RETRO_BG);
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) { // solo mitad izquierda, se espeja
      bool on = (h >> (row * 3 + col)) & 1;
      if (!on) continue;
      int px = x + col * cell;
      int mirrorPx = x + (4 - col) * cell;
      int py = y + row * cell;
      tft.fillRect(px, py, cell, cell, color);
      tft.fillRect(mirrorPx, py, cell, cell, color);
    }
  }
}

// ---------- Splash de arranque (pixel art, marco + firma) ----------
void drawSplash() {
  tft.fillScreen(RETRO_BG);

  static const uint16_t palette[4] = { TFT_RED, TFT_YELLOW, TFT_CYAN, TFT_MAGENTA };
  const int BLOCK = 8;
  int idx = 0;
  for (int x = 0; x < tft.width(); x += BLOCK) {
    tft.fillRect(x, 0, BLOCK, BLOCK, palette[idx++ % 4]);
    tft.fillRect(x, tft.height() - BLOCK, BLOCK, BLOCK, palette[idx++ % 4]);
  }
  for (int y = 0; y < tft.height(); y += BLOCK) {
    tft.fillRect(0, y, BLOCK, BLOCK, palette[idx++ % 4]);
    tft.fillRect(tft.width() - BLOCK, y, BLOCK, BLOCK, palette[idx++ % 4]);
  }

  int cx = tft.width() / 2;
  int cy = tft.height() / 2 - 30;

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(RETRO_SHADOW, RETRO_BG);
  tft.drawString("CYD LAUNCHER", cx + 3, cy + 3, 4);
  tft.setTextColor(RETRO_TITLE, RETRO_BG);
  tft.drawString("CYD LAUNCHER", cx, cy, 4);

  tft.setTextColor(RETRO_SIGN, RETRO_BG);
  tft.drawString("By HeraDev", cx, cy + 42, 4);

  const int totalBlocks = 12;
  const int blockW = 12, blockH = 14, gap = 3;
  int barW = totalBlocks * (blockW + gap) - gap;
  int barX = cx - barW / 2;
  int barY = cy + 78;

  for (int i = 0; i < totalBlocks; i++) {
    tft.drawRect(barX + i * (blockW + gap), barY, blockW, blockH, RETRO_SELECT);
  }
  for (int i = 0; i < totalBlocks; i++) {
    tft.fillRect(barX + i * (blockW + gap) + 1, barY + 1, blockW - 2, blockH - 2, RETRO_SELECT);
    delay(60);
  }
  delay(350);
}

// Trunca `text` para que quepa en maxW píxeles con la fuente `font`, añadiendo "..."
String truncateToWidth(const String& text, int maxW, int font) {
  if (tft.textWidth(text, font) <= maxW) return text;
  String out = text;
  while (out.length() > 1 && tft.textWidth(out + "...", font) > maxW) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

int itemY(int i) { return LIST_TOP + i * (ITEM_H + ITEM_GAP); }

// Interpola entre dos colores RGB565 (t: 0.0 a 1.0)
uint16_t lerpColor565(uint16_t c1, uint16_t c2, float t) {
  t = constrain(t, 0.0f, 1.0f);
  uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
  uint8_t r = r1 + (int)((r2 - r1) * t);
  uint8_t g = g1 + (int)((g2 - g1) * t);
  uint8_t b = b1 + (int)((b2 - b1) * t);
  return (r << 11) | (g << 5) | b;
}

// Dibuja UNA fila del menú principal con el fondo `bg` dado (se reusa para
// el estado normal y para la animación de carga con color interpolado).
void drawMenuItem(int i, uint16_t bg) {
  int x = MARGIN, y = itemY(i), w = tft.width() - 2 * MARGIN;
  bool textBlack = bg != RETRO_ITEM_BG; // fondos claros (seleccion/carga/ultima) -> texto negro

  tft.fillRoundRect(x, y, w, ITEM_H, 6, bg);
  tft.drawRoundRect(x, y, w, ITEM_H, 6, RETRO_BORDER);

  drawIdenticon(x + 6, y + (ITEM_H - ICON_SIZE) / 2, ICON_SIZE, apps[i].label);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(textBlack ? TFT_BLACK : TFT_WHITE, bg);
  String shown = truncateToWidth(apps[i].label, MAX_LABEL_W, 4);
  tft.drawString(shown, x + ICON_SIZE + 16, y + ITEM_H / 2, 4);

  char sizeStr[16];
  snprintf(sizeStr, sizeof(sizeStr), "%.1fMB", apps[i].size / 1024.0 / 1024.0);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(sizeStr, x + w - 10, y + ITEM_H / 2, 2);
}

// Color base de una fila (sin animación en curso)
uint16_t menuItemBaseColor(int i) {
  bool isLast = strcmp(apps[i].label, lastAppLabel) == 0;
  return isLast ? RETRO_LAST : RETRO_ITEM_BG;
}

// ---- Barra de carga (mantener presionado) ----
// Se dibuja UNA vez el marco vacío al empezar a tocar, y luego solo se
// actualiza el relleno cada fotograma — así no hay que redibujar todo el
// item (icono/texto) en cada frame, que era lo que causaba el parpadeo.
void chargeBarRect(int i, int& x, int& y, int& w) {
  x = MARGIN + BAR_MARGIN;
  y = itemY(i) + ITEM_H - BAR_H - 5;
  w = (tft.width() - 2 * MARGIN) - 2 * BAR_MARGIN;
}

void drawChargeBarFrame(int i) {
  int x, y, w;
  chargeBarRect(i, x, y, w);
  tft.fillRect(x, y, w, BAR_H, TFT_BLACK);
  tft.drawRect(x, y, w, BAR_H, TFT_WHITE);
}

// progress: 0.0 a 1.0. Solo pinta el relleno, no toca el resto del item.
void updateChargeBar(int i, float progress) {
  int x, y, w;
  chargeBarRect(i, x, y, w);
  progress = constrain(progress, 0.0f, 1.0f);
  int fillW = (int)((w - 2) * progress);
  uint16_t barColor = lerpColor565(TFT_YELLOW, TFT_PURPLE, progress);
  tft.fillRect(x + 1, y + 1, fillW, BAR_H - 2, barColor);
}

int menuBtnX() { return tft.width() - MENU_BTN_W - 6; } // depende del ancho actual (rotación)

void drawMenu(int selected) {
  tft.fillScreen(RETRO_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(RETRO_TITLE, RETRO_BG);
  tft.drawString("CYD LAUNCHER", 10, TITLE_Y, 4);

  // Botón único que abre la pantalla de opciones (Calibrar/WiFi/Info/Apagar/Rotar)
  int menuX = menuBtnX();
  tft.fillRoundRect(menuX, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H, 4, RETRO_ITEM_BG);
  tft.drawRoundRect(menuX, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H, 4, RETRO_SIGN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(RETRO_SIGN, RETRO_ITEM_BG);
  tft.drawString("Menu", menuX + MENU_BTN_W / 2, MENU_BTN_Y + MENU_BTN_H / 2, 2);

  if (appCount == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(RETRO_SHADOW, RETRO_BG);
    tft.drawString("Sin .bin en /firmware", tft.width() / 2, tft.height() / 2, 2);
    return;
  }

  for (int i = 0; i < appCount; i++) {
    uint16_t bg = (i == selected) ? TFT_YELLOW : menuItemBaseColor(i);
    drawMenuItem(i, bg);
  }

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(RETRO_FOOTER, RETRO_BG);
  tft.drawString("By HeraDev", tft.width() - 6, tft.height() - 6, 2);

  tft.setTextDatum(BL_DATUM);
  tft.setTextColor(TFT_DARKGREY, RETRO_BG); // negro puro sería invisible sobre fondo negro
  tft.drawString("Toque=info  Mantener 1s=flash", 6, tft.height() - 6, 1);
}

// Devuelve índice de app (0..appCount-1) o -1 (nada)
int hitTest(int touchX, int touchY) {
  int y = LIST_TOP;
  for (int i = 0; i < appCount; i++) {
    int x = MARGIN;
    int w = tft.width() - 2 * MARGIN;
    if (touchX >= x && touchX <= x + w && touchY >= y && touchY <= y + ITEM_H) return i;
    y += ITEM_H + ITEM_GAP;
  }
  return -1;
}

bool hitMenuButton(int touchX, int touchY) {
  int menuX = menuBtnX();
  return touchX >= menuX && touchX <= menuX + MENU_BTN_W &&
         touchY >= MENU_BTN_Y && touchY <= MENU_BTN_Y + MENU_BTN_H;
}
