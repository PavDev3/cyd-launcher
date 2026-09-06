/*
 * ui_menu.h — pantalla de splash y menú principal (lista de firmwares
 * de la SD): identicons, truncado de texto, barra de carga al mantener
 * presionado, hit-testing.
 */
#pragma once

#include "config.h"

void drawIdenticon(int x, int y, int size, const char* label);
void drawSplash();

String truncateToWidth(const String& text, int maxW, int font);
int itemY(int i);
uint16_t lerpColor565(uint16_t c1, uint16_t c2, float t);

// Dibuja una fila del menú (icono + label + tamaño) con el fondo dado
void drawMenuItem(int i, uint16_t bg);
uint16_t menuItemBaseColor(int i);

// ---- Barra de carga (mantener presionado 1s) ----
void chargeBarRect(int i, int& x, int& y, int& w);
void drawChargeBarFrame(int i); // marco vacío, se llama una vez al empezar a tocar
void updateChargeBar(int i, float progress); // solo actualiza el relleno

int menuBtnX(); // posición X del botón "Menu" (depende del ancho, por rotación)

void drawMenu(int selected = -1);
int hitTest(int touchX, int touchY);       // -1 = nada, 0..appCount-1 = item
bool hitMenuButton(int touchX, int touchY);
