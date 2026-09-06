/*
 * wifi_upload.h — servidor WiFi (AP + WebServer) para subir .bin a
 * /firmware/ en la SD sin sacar la tarjeta del lector.
 */
#pragma once

#include "config.h"

// Levanta el AP, sirve la página de subida, y bloquea hasta que el
// usuario toca "Salir" en pantalla. Al volver, ya redibuja el menú
// principal (con el listado de /firmware/ actualizado).
void runWifiUploadMode();
