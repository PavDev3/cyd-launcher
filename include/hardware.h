/*
 * hardware.h — control de bajo nivel: bus SPI compartido (SD/táctil),
 * LED RGB de estado, altavoz (beep), calibración táctil y apagado.
 */
#pragma once

#include "config.h"

// ---- Bus VSPI compartido en el tiempo entre SD y táctil ----
void busToSD();
void busToTouch();

// Aplica una nueva rotación a TFT + táctil y la persiste en NVS
void applyRotation(int rot);

// ---- LED RGB de estado ----
void ledSetup();
void ledSet(bool r, bool g, bool b);
void ledOff();
void ledReady();  // verde
void ledBusy();   // azul
void ledError();  // rojo

// ---- Beep (altavoz piezo/DAC en GPIO26) ----
void beep(int freqHz, int ms);
void beepOk();
void beepError();
void beepTick();

// ---- Coordenadas táctiles calibradas ----
int touchScreenX(int rawX);
int touchScreenY(int rawY);

// Calibración de 2 puntos (guarda en NVS). showIntro: mostrar pantalla
// explicativa antes de empezar (solo la primera vez).
void calibrateTouch(bool showIntro);

// ---- Apagado (deep sleep, sin fuente de wake) ----
void powerOff();

// Sincroniza la hora del sistema por NTP (necesita WiFi ya conectado).
// Best-effort con timeout corto: si falla, los archivos nuevos en la SD
// simplemente seguirán con fecha "desconocida" como hasta ahora.
void syncTimeViaNtp();
