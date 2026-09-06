# CYD Multi-Boot Launcher (carga firmwares desde microSD)

Launcher para ESP32-2432S028R ("CYD"). Escanea `/firmware/*.bin` en la microSD, muestra un menú
táctil, flashea el elegido al slot OTA activo y arranca.

**Estado: funcionando end-to-end** (launcher ↔ HaleHound-CYD, ida y vuelta).

## Tu placa

- Chip: ESP32-D0WD-V3 (rev 3.1), WiFi+BT
- Flash: 4MB
- Puerto: COM3 (CH340)

## Layout de particiones (`partitions.csv`)

Dos slots OTA **reales** (`ota_0` / `ota_1`), sin `factory`. Esto es
importante: el propio "Tools > Update Firmware" de HaleHound-CYD (y
cualquier firmware que use `Update.h` estándar de Arduino) resuelve el
destino de escritura con `esp_ota_get_next_update_partition()`, que
**ignora las particiones tipo `factory`**. Con `factory` en la tabla,
HaleHound crasheaba al 1% al intentar escribir. Con dos slots `ota_x`
reales, siempre encuentra "el otro slot" sin problema.

| Partición | Subtipo | Offset    | Tamaño   | Uso                          |
|-----------|---------|-----------|----------|-------------------------------|
| nvs       | nvs     | 0x9000    | 0x5000   | config interna                |
| otadata   | ota     | 0xe000    | 0x2000   | puntero de boot activo        |
| launcher  | ota_1   | 0x10000   | 0x90000  | **este launcher** (576KB)     |
| app0      | ota_0   | 0xa0000   | 0x360000 | firmware activo (3.375MB)     |

La SD es la biblioteca de firmwares; el flash interno solo guarda los
dos que están "en juego" (el launcher y el activo).

## 1. SD

Formateada en **FAT32 + MBR** (no GPT — el core Arduino SD/SdFat lo
espera así). Contiene:

```
/firmware/HaleHound.bin       (firmware "hijo", ~2.45MB)
/firmware/CYD-Launcher.bin    (este mismo launcher, para volver)
```

`CYD-Launcher.bin` es exactamente `.pio/build/cyd_launcher/firmware.bin`
— cada vez que recompiles el launcher, cópialo también a la SD con ese
nombre para que "Update Firmware" desde HaleHound siga funcionando.

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
`partitions.csv`. Si solo tocas `main.cpp`, basta con re-escribir
`firmware.bin` en `0x10000`.

## Flujo de uso

1. Enciende → splash "CYD LAUNCHER / By HeraDev" → menú con los `.bin`
   de `/firmware/`
2. Tocas **HaleHound** → barra de progreso → arranca HaleHound
3. Para volver: dentro de HaleHound, **Tools > Update Firmware** →
   eliges **CYD-Launcher.bin** → vuelve al menú
4. Repetir

## Limitación conocida

El slot `launcher` (ota_1) es de 576KB. Si algún día quieres usar
`Tools > Update Firmware` de HaleHound para **auto-actualizarse a una
versión nueva de sí mismo** (no para volver al launcher), esa nueva
versión de HaleHound también apuntaría a `ota_1` (el "otro" slot) y
576KB es insuficiente para un HaleHound completo (~2.45MB). Si eso pasa,
avísame y ajustamos tamaños en `partitions.csv` (agrandar `launcher`
implica achicar `app0`, manteniendo alineación a 0x10000 y total exacto
en 0x400000).

## Ajustar tamaños / añadir más firmwares

Solo puedes tener **un** firmware "hijo" activo a la vez en `app0`
(3.375MB de margen). Para tener más de dos opciones en el menú, deja
varios `.bin` en `/firmware/` — el launcher los lista todos, pero cada
selección sobrescribe `app0` con el elegido. No hay límite de cuántos
`.bin` guardes en la SD, solo de cuántos "quepan a la vez" en el chip
(siempre 1).

## Personalización (splash / tema)

Todo en `src/main.cpp`:
- `drawSplash()` — splash de arranque (marco pixel art, título, firma)
- Constantes `RETRO_*` al inicio del archivo — paleta de colores del
  tema, tanto del splash como del menú
- Texto "By HeraDev" aparece en el splash y en la esquina del menú —
  búscalo y cámbialo si quieres otra firma
