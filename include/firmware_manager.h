/*
 * firmware_manager.h — todo lo relacionado con los .bin en la SD:
 * escaneo del directorio /firmware, backup de app0, validación de imagen,
 * flasheo vía Update.h y borrado.
 */
#pragma once

#include "config.h"

// Lee el primer renglón de <label>.txt (si existe) como descripción
void loadDescription(AppEntry& app);

// Escanea /firmware/*.bin en la SD y rellena apps[]/appCount
bool scanFirmwareDir();

// Vuelca el contenido actual de la partición app0 a /backups/<lastAppLabel>.bin
bool backupCurrentApp0();

// Verifica magic byte (0xE9) y tamaño antes de flashear
bool validateImage(File& f, size_t maxPartitionSize, String& errMsg);

// Pantallas de progreso / error genéricas usadas durante el flasheo
void drawProgress(const char* label, size_t done, size_t total);
void showError(const char* line1, const char* line2 = nullptr);

// Flashea `app` desde la SD hacia app0 (con backup y validación previos)
bool flashFromSD(AppEntry& app);

// Borra el .bin (y su .txt asociado, si existe) de la SD
bool deleteAppFromSD(AppEntry& app);

// ---------- Backups (/backups/, uno por cada nombre de firmware distinto) ----------
#define MAX_BACKUPS 8
extern AppEntry backupApps[MAX_BACKUPS];
extern int backupCount;

// Escanea /backups/*.bin y rellena backupApps[]/backupCount
bool scanBackupsDir();

// Borra un backup de la SD
bool deleteBackupFromSD(AppEntry& backup);
