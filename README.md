# CYD Multi-Boot Launcher

Launcher para ESP32-2432S028R ("Cheap Yellow Display"). Escanea
`/firmware/*.bin` en la microSD, muestra un menú táctil, flashea el
firmware elegido al slot OTA activo y arranca.

**Estado: funcionando end-to-end** — ida y vuelta entre el launcher y
cualquier firmware de terceros compatible con OTA estándar de Arduino.

## Funciones

- Menú táctil con la lista de `.bin` encontrados en la SD
- Iconos "identicon" pixel-art derivados del nombre de cada firmware
- Backup automático del firmware activo a `/backups/` antes de sobrescribirlo
- Verificación básica de imagen (magic byte + tamaño) antes de flashear
- Confirmación mediante gesto: mantener presionado 1s (animación de
  carga amarillo→morado) para flashear; toque corto = info del archivo
  (tamaño, fecha, descripción, botón Borrar)
- Recuerda el último firmware usado (NVS) y lo resalta en el menú
- Beep + LED RGB de estado (listo/flasheando/error)
- Subida de `.bin` por WiFi (AP propio + página web) sin sacar la SD
- Calibración táctil guiada + rotación de pantalla (0°/90°/180°/270°)
  con recalibración automática al rotar
- Apagado (deep sleep)

## Layout de particiones (`partitions.csv`)

Dos slots OTA **reales** (`ota_0` / `ota_1`), sin `factory`. Esto es
importante: cualquier firmware que use `Update.h` estándar de Arduino
(incluyendo su propio "actualizar por SD" si lo trae) resuelve el
destino de escritura con `esp_ota_get_next_update_partition()`, que
**ignora las particiones tipo `factory`**. Con dos slots `ota_x` reales,
siempre encuentra "el otro slot" sin problema — con `factory` en la
tabla, en cambio, ese lookup falla y el firmware crashea al iniciar la
escritura.

| Partición | Subtipo | Offset    | Tamaño   | Uso                          |
|-----------|---------|-----------|----------|-------------------------------|
| nvs       | nvs     | 0x9000    | 0x5000   | config interna                |
| otadata   | ota     | 0xe000    | 0x2000   | puntero de boot activo        |
| launcher  | ota_1   | 0x10000   | 0x110000 | **este launcher** (~1.06MB)   |
| app0      | ota_0   | 0x120000  | 0x2e0000 | firmware activo (~2.8MB)      |

La SD es la biblioteca de firmwares; el flash interno solo guarda los
dos que están "en juego" (el launcher y el activo).

## Estructura del código

```
include/
  config.h            pines, tema visual, structs y estado global (extern)
  hardware.h          buses SPI, LED, beep, calibración táctil, apagado
  firmware_manager.h  escaneo SD, backup, validación, flasheo, borrado
  ui_menu.h           splash + menú principal (identicons, barra de carga)
  ui_screens.h        info de app/dispositivo, confirmaciones, submenús
  wifi_upload.h       servidor WiFi de subida de .bin
src/
  main.cpp            setup()/loop() + máquina de estados del gesto táctil
  hardware.cpp
  firmware_manager.cpp
  ui_menu.cpp
  ui_screens.cpp
  wifi_upload.cpp
```

## 1. SD

Formateada en **FAT32 + MBR** (no GPT — el core Arduino SD/SdFat lo
espera así). Estructura:

```
/firmware/MiFirmware.bin        (cualquier firmware "hijo" compatible)
/firmware/MiFirmware.txt        (opcional: primera línea = descripción)
/firmware/CYD-Launcher.bin      (este mismo launcher, para volver)
```

`CYD-Launcher.bin` es exactamente `.pio/build/cyd_launcher/firmware.bin`
— cada vez que recompiles el launcher, cópialo también a la SD con ese
nombre para poder volver a él desde un firmware hijo que tenga su
propia opción de "actualizar por SD".

## 2. Compilar

```bash
cd D:/cyd-launcher
python -m platformio run -e cyd_launcher
```

Genera en `.pio/build/cyd_launcher/`:
- `bootloader.bin`  → offset `0x1000`
- `partitions.bin`  → offset `0x8000`
- `firmware.bin`    → offset `0x10000` (esto es lo mismo que
  `/firmware/CYD-Launcher.bin` en la SD)

## 3. Flashear (placa en modo bootloader: BOOT + RESET)

```bash
python -m esptool --port COM3 --baud 921600 write-flash \
  0x1000  .pio/build/cyd_launcher/bootloader.bin \
  0x8000  .pio/build/cyd_launcher/partitions.bin \
  0x10000 .pio/build/cyd_launcher/firmware.bin
```

Solo hace falta re-flashear bootloader+partitions si cambias
`partitions.csv`. Si solo tocas el código fuente, basta con re-escribir
`firmware.bin` en `0x10000`.

## Flujo de uso

1. Enciende → splash → menú con los `.bin` de `/firmware/`
2. Mantén presionado un firmware 1s (barra de carga) → confirmar →
   barra de progreso → arranca ese firmware
3. Para volver: si el firmware activo tiene su propia opción de
   actualizar/flashear desde SD, úsala eligiendo `CYD-Launcher.bin`
4. Repetir

## Limitación conocida

El slot `launcher` (ota_1) es de ~1.06MB. Si algún firmware usa esa
misma partición para auto-actualizarse a una versión de sí mismo más
grande que eso, fallará. Si eso pasa, se puede ajustar `partitions.csv`
(agrandar `launcher` implica achicar `app0`, manteniendo alineación a
0x10000 y total exacto en 0x400000) — pero eso siempre requiere
reubicar físicamente lo que haya en `app0` en el mismo proceso.

## Ajustar tamaños / añadir más firmwares

Solo puedes tener **un** firmware "hijo" activo a la vez en `app0`
(~2.8MB de margen). Para tener más de dos opciones en el menú, deja
varios `.bin` en `/firmware/` — el launcher los lista todos, pero cada
selección sobrescribe `app0` con el elegido. No hay límite de cuántos
`.bin` guardes en la SD, solo de cuántos "quepan a la vez" en el chip
(siempre 1).

## Personalización (splash / tema)

- `ui_menu.cpp` → `drawSplash()` — splash de arranque (marco pixel art,
  título, firma)
- `config.h` → constantes `RETRO_*` — paleta de colores del tema, tanto
  del splash como del menú
