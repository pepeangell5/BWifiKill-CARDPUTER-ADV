#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <Arduino.h>

// =============================================================
// Configuración central del firmware.
// Todo lo que antes estaba esparcido como #define en cada .cpp
// vive aquí. Para cambiar un pin, una versión, un texto común
// o un límite de scan, este es el único archivo a tocar.
// =============================================================

namespace AppConfig {

    // ---------- Botones ----------
    constexpr uint8_t  BTN_UP    = 26;
    constexpr uint8_t  BTN_DOWN  = 33;
    constexpr uint8_t  BTN_OK    = 32;
    constexpr uint8_t  BTN_BACK  = 25;
    constexpr uint8_t  BTN_AUX   = 27;   // El que llamabas BTN27

    // ---------- I2C / Display ----------
    constexpr uint8_t  I2C_SDA   = 21;
    constexpr uint8_t  I2C_SCL   = 22;
    constexpr uint8_t  SCREEN_W  = 128;
    constexpr uint8_t  SCREEN_H  = 64;

    // ---------- nRF24 ----------
    constexpr uint8_t  NRF1_CE   = 5;
    constexpr uint8_t  NRF1_CSN  = 17;
    constexpr uint8_t  NRF2_CE   = 16;
    constexpr uint8_t  NRF2_CSN  = 4;
    constexpr uint32_t NRF_SPI_HZ = 16000000;

    // ---------- Firmware / Identidad ----------
    constexpr const char* FIRMWARE_NAME    = "BWifiKill";
    constexpr const char* FIRMWARE_VERSION = "V4.0";
    constexpr const char* BUILD_LABEL      = "V4 Visual";
    constexpr const char* BOARD_NAME       = "ESP32 DevKit";
    constexpr const char* DEV_HANDLE       = "PepeAngell";

    // ---------- AP del Web Dashboard ----------
    constexpr const char* WEB_AP_SSID      = "BWifiKill_V4";
    constexpr const char* WEB_AP_PASSWORD  = "bwifikill40";
    constexpr const char* WEB_AP_IP        = "192.168.4.1";
    constexpr uint16_t    WEB_PORT         = 80;

    // ---------- Redes sociales (About) ----------
    constexpr const char* SOCIAL_INSTAGRAM = "pepeangelll";
    constexpr const char* SOCIAL_GITHUB    = "pepeangell5";
    constexpr const char* SOCIAL_FACEBOOK  = "esp32tools";

    // ---------- Input timings (ms) ----------
    // Debounce: tiempo que debe mantenerse estable un flanco
    constexpr uint16_t INPUT_DEBOUNCE_MS     = 30;
    // Cuánto hay que sostener antes de empezar a auto-repetir
    constexpr uint16_t INPUT_REPEAT_DELAY_MS = 350;
    // Cada cuánto dispara el auto-repeat una vez activo
    constexpr uint16_t INPUT_REPEAT_RATE_MS  = 90;
    // Sostén para considerar long-press (dispara una sola vez)
    constexpr uint16_t INPUT_LONG_PRESS_MS   = 600;

    // ---------- Límites de scanners pasivos ----------
    constexpr uint8_t  WIFI_SCAN_MAX_NETWORKS = 30;
    constexpr uint16_t WIFI_SCAN_TIMEOUT_MS   = 3000;
    constexpr uint8_t  BT_SCAN_DURATION_S     = 5;
    constexpr uint8_t  BT_SCAN_MAX_DEVICES    = 25;
    constexpr uint8_t  IP_SCAN_BATCH_SIZE     = 8;   // hosts en paralelo
    constexpr uint16_t IP_SCAN_PING_TIMEOUT_MS = 250;

    // ---------- Log viewer ----------
    constexpr const char* LOG_FILE_PATH = "/logins.txt";
    constexpr uint8_t     LOG_LINES_PER_PAGE = 6;

    // ---------- Textos comunes de UI ----------
    constexpr const char* TXT_RUNNING   = "RUNNING";
    constexpr const char* TXT_STOPPED   = "STOPPED";
    constexpr const char* TXT_READY     = "READY";
    constexpr const char* TXT_BACK_EXIT = "BACK = SALIR";
    constexpr const char* TXT_OK_START  = "OK = INICIAR";

} // namespace AppConfig

#endif // APP_CONFIG_H
