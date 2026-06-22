#include "dual_nrf_scope.h"
#include "input_manager.h"
#include "ui_theme.h"
#include <RF24.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <string.h>

extern RF24 jam1;
extern RF24 jam2;
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static const uint8_t LOW_START = 0;
static const uint8_t LOW_COUNT = 62;   // 2400-2461 MHz
static const uint8_t HIGH_START = 62;
static const uint8_t HIGH_COUNT = 63;  // 2462-2524 MHz
static const uint8_t SAMPLES_PER_POINT = 12;
static const uint8_t POINTS_PER_FRAME = 6;

static uint8_t lowTrace[LOW_COUNT];
static uint8_t highTrace[HIGH_COUNT];
static uint8_t lowCursor = 0;
static uint8_t highCursor = 0;
static uint16_t frames = 0;
static uint8_t lowPeak = 0;
static uint8_t highPeak = 0;
static uint8_t frameTick = 0;

static uint8_t sampleRadio(RF24& radio, uint8_t channel) {
    radio.setChannel(channel);
    delayMicroseconds(75);

    uint8_t hits = 0;
    for (uint8_t i = 0; i < SAMPLES_PER_POINT; i++) {
        if (radio.testCarrier()) hits++;
    }
    return hits;
}

static uint8_t pointX(uint8_t index, uint8_t count) {
    return 14 + ((uint16_t)index * 112) / max<uint8_t>(1, count - 1);
}

static uint8_t pointY(uint8_t sample, uint8_t top, uint8_t bottom) {
    uint8_t h = map(constrain(sample, 0, SAMPLES_PER_POINT),
                    0, SAMPLES_PER_POINT,
                    0, bottom - top - 2);
    return bottom - 1 - h;
}

static void scanStep() {
    for (uint8_t i = 0; i < POINTS_PER_FRAME; i++) {
        uint8_t lowSample = sampleRadio(jam1, LOW_START + lowCursor);
        lowTrace[lowCursor] = (lowTrace[lowCursor] * 2 + lowSample) / 3;
        if (lowTrace[lowCursor] > lowPeak) lowPeak = lowTrace[lowCursor];
        lowCursor++;
        if (lowCursor >= LOW_COUNT) lowCursor = 0;

        uint8_t highSample = sampleRadio(jam2, HIGH_START + highCursor);
        highTrace[highCursor] = (highTrace[highCursor] * 2 + highSample) / 3;
        if (highTrace[highCursor] > highPeak) highPeak = highTrace[highCursor];
        highCursor++;
        if (highCursor >= HIGH_COUNT) highCursor = 0;
    }

    if ((frames & 0x001F) == 0) {
        if (lowPeak > 0) lowPeak--;
        if (highPeak > 0) highPeak--;
    }
    frames++;
}

static void drawGrid(uint8_t top, uint8_t bottom) {
    uint8_t mid = top + (bottom - top) / 2;
    for (uint8_t x = 14; x <= 126; x += 8) {
        u8g2.drawPixel(x, mid);
    }
    u8g2.drawHLine(14, bottom, 112);
}

static void drawTrace(const uint8_t* trace,
                      uint8_t count,
                      uint8_t cursor,
                      uint8_t top,
                      uint8_t bottom,
                      const char* label,
                      uint8_t peak) {
    drawGrid(top, bottom);

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(1, top + 8, label);

    char peakText[4];
    snprintf(peakText, sizeof(peakText), "%02u", peak);
    u8g2.drawStr(1, bottom - 1, peakText);

    uint8_t prevX = pointX(0, count);
    uint8_t prevY = pointY(trace[0], top, bottom);
    for (uint8_t i = 1; i < count; i++) {
        uint8_t x = pointX(i, count);
        uint8_t y = pointY(trace[i], top, bottom);
        u8g2.drawLine(prevX, prevY, x, y);
        prevX = x;
        prevY = y;
    }

    uint8_t cx = pointX(cursor, count);
    u8g2.drawVLine(cx, top + 1, bottom - top - 1);
    if (((frameTick / 3) & 1) == 0) {
        u8g2.drawPixel(cx, top);
        u8g2.drawPixel(cx, bottom);
    }
}

static void drawScope() {
    char status[10];
    snprintf(status, sizeof(status), "F%04u", frames);
#ifdef BWK_CARDPUTER_ADV
    UiTheme::drawHeader(u8g2, "NRF SCOPE", status);
#else
    UiTheme::drawHeader(u8g2, "DUAL NRF", status);
#endif

    drawTrace(lowTrace, LOW_COUNT, lowCursor, 18, 38, "L", lowPeak);
    drawTrace(highTrace, HIGH_COUNT, highCursor, 42, 62, "H", highPeak);

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(32, 41, "2400-2461");
    u8g2.drawStr(76, 41, "2462-2524");
}

void dualNrfScopeEnter() {
    WiFi.mode(WIFI_OFF);

    jam1.begin();
    jam1.setAutoAck(false);
    jam1.setDataRate(RF24_2MBPS);
    jam1.setPALevel(RF24_PA_MAX);
    jam1.startListening();

    jam2.begin();
    jam2.setAutoAck(false);
    jam2.setDataRate(RF24_2MBPS);
    jam2.setPALevel(RF24_PA_MAX);
    jam2.startListening();

    memset(lowTrace, 0, sizeof(lowTrace));
    memset(highTrace, 0, sizeof(highTrace));
    lowCursor = 0;
    highCursor = 0;
    frames = 0;
    lowPeak = 0;
    highPeak = 0;
    frameTick = 0;
    Input.resetAll();
}

void dualNrfScopeExit() {
    jam1.stopListening();
    jam2.stopListening();
    memset(lowTrace, 0, sizeof(lowTrace));
    memset(highTrace, 0, sizeof(highTrace));
}

void dualNrfScopeLoop() {
    if (Input.pressed(BTN_ID_OK)) {
        memset(lowTrace, 0, sizeof(lowTrace));
        memset(highTrace, 0, sizeof(highTrace));
        frames = 0;
        lowPeak = 0;
        highPeak = 0;
        Input.consume(BTN_ID_OK);
    }

    scanStep();
    frameTick++;

    u8g2.clearBuffer();
    drawScope();
    u8g2.sendBuffer();
}
