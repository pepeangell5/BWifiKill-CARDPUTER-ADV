# Binarios BWifiKill Cardputer ADV

Imágenes compiladas para **M5Stack Cardputer ADV / ESP32-S3 con flash de 8 MB**.

## Instalación recomendada

El Web Flasher utiliza un único archivo:

| Archivo | Offset | Uso |
| --- | ---: | --- |
| `BWifiKill-CARDPUTER-ADV-full.bin` | `0x0000` | Imagen completa para Web Flasher o restauración manual. |

```bash
esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 binarios/BWifiKill-CARDPUTER-ADV-full.bin
```

## Imágenes separadas

| Archivo | Offset |
| --- | ---: |
| `bootloader.bin` | `0x0000` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xE000` |
| `firmware.bin` | `0x10000` |

```bash
esptool.py --chip esp32s3 --baud 921600 write_flash \
  0x0000 binarios/bootloader.bin \
  0x8000 binarios/partitions.bin \
  0xE000 binarios/boot_app0.bin \
  0x10000 binarios/firmware.bin
```

> `BWifiKill-ESP32-V4.0-full.bin` pertenece al ESP32 DevKit original y no es compatible con Cardputer ADV.

Las sumas de verificación están disponibles en `checksums-sha256.txt`.
