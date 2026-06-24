#include "nrf_diagnostic.h"

#include "app_config.h"
#include "input_manager.h"
#include "ui_theme.h"

#include <RF24.h>
#include <U8g2lib.h>
#include <WiFi.h>

extern RF24 jam1;
extern RF24 jam2;
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

namespace {

struct RadioDiag {
    bool beginOk = false;
    bool chipOk = false;
    bool rateOk = false;
    uint8_t channel = 0;
    rf24_datarate_e rate = RF24_1MBPS;
    uint8_t pa = 0;
    bool failure = false;
};

RadioDiag radio1;
#ifndef BWK_CARDPUTER_ADV
RadioDiag radio2;
#endif
uint32_t lastDrawMs = 0;

const char* rateName(rf24_datarate_e rate) {
    switch (rate) {
        case RF24_250KBPS: return "250K";
        case RF24_2MBPS: return "2M";
        case RF24_1MBPS:
        default: return "1M";
    }
}

const char* paName(uint8_t pa) {
    switch (pa) {
        case RF24_PA_MIN: return "MIN";
        case RF24_PA_LOW: return "LOW";
        case RF24_PA_HIGH: return "HIGH";
        case RF24_PA_MAX: return "MAX";
        default: return "?";
    }
}

RadioDiag testRadio(RF24& radio) {
    RadioDiag diag;
    diag.beginOk = radio.begin();
    if (diag.beginOk) {
        radio.stopConstCarrier();
        radio.stopListening();
        radio.powerUp();
        radio.setChannel(76);
        diag.rateOk = radio.setDataRate(RF24_2MBPS);
        radio.setPALevel(RF24_PA_MAX);
        radio.setAutoAck(false);
        radio.setCRCLength(RF24_CRC_DISABLED);
        radio.flush_rx();
        radio.flush_tx();
        diag.chipOk = radio.isChipConnected();
        diag.channel = radio.getChannel();
        diag.rate = radio.getDataRate();
        diag.pa = radio.getPALevel();
        diag.failure = radio.failureDetected;
    }
    return diag;
}

void runDiagnostics() {
    WiFi.mode(WIFI_OFF);
    jam1.stopConstCarrier();
    jam1.stopListening();
#ifndef BWK_CARDPUTER_ADV
    jam2.stopConstCarrier();
    jam2.stopListening();
#endif

    radio1 = testRadio(jam1);
#ifndef BWK_CARDPUTER_ADV
    radio2 = testRadio(jam2);
#endif
    lastDrawMs = 0;
}

void drawStatusLine(int y, const char* label, const RadioDiag& diag) {
    char line[32];
    snprintf(line, sizeof(line), "%s %s CH%03u %s",
             label,
             diag.chipOk ? "OK" : (diag.beginOk ? "NOCHIP" : "BEGIN!"),
             diag.channel,
             rateName(diag.rate));
    u8g2.drawStr(4, y, line);
}

void drawPins() {
    char line[32];
    snprintf(line, sizeof(line), "CE%u CS%u SCK%u",
             AppConfig::NRF1_CE, AppConfig::NRF1_CSN, AppConfig::NRF_SPI_SCK);
    u8g2.drawStr(4, 42, line);
    snprintf(line, sizeof(line), "MISO%u MOSI%u PA %s",
             AppConfig::NRF_SPI_MISO, AppConfig::NRF_SPI_MOSI, paName(radio1.pa));
    u8g2.drawStr(4, 50, line);
}

void drawDiagnostic() {
    bool ok = radio1.beginOk && radio1.chipOk;
    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "NRF DIAG", ok ? "OK" : "ERROR");

    u8g2.setFont(u8g2_font_5x7_tr);
    drawStatusLine(25, "NRF1", radio1);
#ifdef BWK_CARDPUTER_ADV
    u8g2.drawStr(4, 34, "NRF2 N/A en Cardputer");
#else
    drawStatusLine(34, "NRF2", radio2);
#endif
    drawPins();
    u8g2.drawStr(4, 61, "OK retest  BACK salir");
    u8g2.sendBuffer();
}

}  // namespace

void nrfDiagnosticEnter() {
    Input.resetAll();
    runDiagnostics();
    drawDiagnostic();
}

void nrfDiagnosticLoop() {
    if (Input.pressed(BTN_ID_OK)) {
        runDiagnostics();
        drawDiagnostic();
        Input.consume(BTN_ID_OK);
    }

    if (Input.pressed(BTN_ID_BACK)) {
        runningApp = false;
        Input.consume(BTN_ID_BACK);
        return;
    }

    uint32_t now = millis();
    if (lastDrawMs == 0 || now - lastDrawMs > 700) {
        lastDrawMs = now;
        drawDiagnostic();
    }
}

void nrfDiagnosticExit() {
    jam1.stopConstCarrier();
    jam1.stopListening();
    jam1.powerDown();
#ifndef BWK_CARDPUTER_ADV
    jam2.stopConstCarrier();
    jam2.stopListening();
    jam2.powerDown();
#endif
}
