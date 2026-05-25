# Binarios BWifiKill ESP32 V4.0

Esta carpeta contiene los binarios listos para release.

| Archivo | Uso |
| --- | --- |
| `BWifiKill-ESP32-V4.0-full.bin` | Binario completo para Web Flasher o `esptool.py` en offset `0x0`. |
| `firmware.bin` | Aplicacion principal generada por PlatformIO. |
| `bootloader.bin` | Bootloader generado por PlatformIO. |
| `partitions.bin` | Tabla de particiones usada por el build. |
| `boot_app0.bin` | Imagen auxiliar del framework Arduino ESP32 incluida en el binario completo. |

Flasheo manual recomendado:

```bash
esptool.py --chip esp32 --baud 921600 write_flash 0x0 binarios/BWifiKill-ESP32-V4.0-full.bin
```
