/*
 * ui_screens.h — pantallas secundarias: info por app (con borrar),
 * info del dispositivo, confirmación de flasheo/borrado, y los
 * sub-menús de lista "Opciones" y "Pantalla".
 */
#pragma once

#include "config.h"

// ---- Sub-menú genérico de lista compacta ----
struct OptionRow { char label[26]; uint16_t color; };
void drawRowMenu(const char* title, OptionRow* rows, int count, int selected);
int hitRowMenu(int touchX, int touchY, int count); // -1 = fuera (volver)

// Submenú "Pantalla": Calibrar / Rotar
void runScreenMenu();

// Menú principal de Opciones: Pantalla / WiFi / Info / Apagar
void runOptionsMenu();

// ---- Pantalla de info por app (toque corto) ----
int deleteBtnX();
int deleteBtnY();
bool hitDeleteButton(int touchX, int touchY);
void drawAppInfo(AppEntry& app);
bool confirmDelete(AppEntry& app); // true = confirmado

// ---- Info del dispositivo ----
void drawDeviceInfo();

// ---- Confirmación de flasheo ----
void drawConfirm(AppEntry& app, int yesX, int yesY, int noX, int noY);
bool confirmFlash(AppEntry& app); // true = confirmado
