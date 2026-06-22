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

## Artefactos separados

| Archivo | Offset |
| --- | ---: |
| `bootloader.bin` | `0x0000` |
| `partitions.bin` | `0x8000` |
| `firmware.bin` | `0x10000` |

Estos archivos se conservan como referencia del build. Para instalaciones y
restauraciones utiliza siempre `BWifiKill-CARDPUTER-ADV-full.bin`.

Las sumas de verificación están disponibles en `checksums-sha256.txt`.
