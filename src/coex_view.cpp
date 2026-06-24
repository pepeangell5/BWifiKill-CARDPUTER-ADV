#include "coex_view.h"
#include "input_manager.h"
#include "ui_theme.h"
#include "audio_feedback.h"
#include <RF24.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <string.h>

extern RF24 jam1;
extern RF24 jam2;
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static const uint16_t FIRST_MHZ = 2400;
static const uint16_t LAST_MHZ = 2483;
static const uint8_t RF_BINS = LAST_MHZ - FIRST_MHZ + 1;
static const uint8_t SAMPLES_PER_BIN = 18;

static uint8_t profile[RF_BINS];
static uint16_t sweeps = 0;
static uint16_t hotMhz = 2400;
static uint8_t hotLevel = 0;
static uint8_t frameTick = 0;

static uint8_t xForMhz(uint16_t mhz) {
    mhz = constrain(mhz, FIRST_MHZ, LAST_MHZ);
    return 2 + ((uint32_t)(mhz - FIRST_MHZ) * 124) / (LAST_MHZ - FIRST_MHZ);
}

static uint8_t sampleRadio(RF24& radio, uint8_t channel) {
    radio.setChannel(channel);
    delayMicroseconds(90);

    uint8_t hits = 0;
    for (uint8_t i = 0; i < SAMPLES_PER_BIN; i++) {
        if (radio.testCarrier()) hits++;
    }
    return hits;
}

static uint8_t bandAverage(uint16_t startMhz, uint16_t endMhz) {
    startMhz = constrain(startMhz, FIRST_MHZ, LAST_MHZ);
    endMhz = constrain(endMhz, FIRST_MHZ, LAST_MHZ);
    if (endMhz < startMhz) return 0;

    uint16_t total = 0;
    uint8_t count = 0;
    for (uint16_t mhz = startMhz; mhz <= endMhz; mhz++) {
        total += profile[mhz - FIRST_MHZ];
        count++;
    }
    return count ? total / count : 0;
}

static void scanProfile() {
    hotLevel = 0;
    hotMhz = FIRST_MHZ;

    for (uint8_t i = 0; i < 42; i++) {
        uint8_t s1 = sampleRadio(jam1, i);
        profile[i] = (profile[i] * 3 + s1) / 4;
        if (profile[i] >= hotLevel) {
            hotLevel = profile[i];
            hotMhz = FIRST_MHZ + i;
        }

        uint8_t ch2 = i + 42;
        if (ch2 < RF_BINS) {
            uint8_t s2 = sampleRadio(jam2, ch2);
            profile[ch2] = (profile[ch2] * 3 + s2) / 4;
            if (profile[ch2] >= hotLevel) {
                hotLevel = profile[ch2];
                hotMhz = FIRST_MHZ + ch2;
            }
        }
    }

    sweeps++;
}

static void drawWifiBand(uint8_t channel) {
    uint16_t center = 2412 + (uint16_t)(channel - 1) * 5;
    uint16_t startMhz = center > 11 ? center - 11 : FIRST_MHZ;
    uint16_t endMhz = center + 11;
    uint8_t x0 = xForMhz(startMhz);
    uint8_t x1 = xForMhz(endMhz);
    uint8_t w = max<uint8_t>(3, x1 - x0);
    uint8_t energy = bandAverage(startMhz, endMhz);

    u8g2.drawFrame(x0, 19, w, 11);
    if (energy > 2) {
        uint8_t fill = map(constrain(energy, 0, SAMPLES_PER_BIN), 0, SAMPLES_PER_BIN, 0, w - 2);
        if (fill > 0) u8g2.drawBox(x0 + 1, 27, fill, 2);
    }

    for (uint8_t x = x0 + ((frameTick + channel) & 3); x < x1; x += 5) {
        u8g2.drawPixel(x, 21);
    }

    char label[3];
    snprintf(label, sizeof(label), "%u", channel);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(x0 + (w - u8g2.getStrWidth(label)) / 2, 27, label);
}

static void drawBleMarker(uint16_t mhz, const char* label, bool fresh) {
    uint8_t x = xForMhz(mhz);
    for (uint8_t y = 34; y <= 44; y += 2) {
        u8g2.drawPixel(x, y);
    }
    u8g2.drawDisc(x, 39, fresh ? 2 : 1);

    u8g2.setFont(u8g2_font_4x6_tf);
    int labelX = constrain((int)x - 4, 0, 120);
    u8g2.drawStr(labelX, 50, label);
}

static void drawBleLane() {
    for (uint8_t x = xForMhz(2402); x <= xForMhz(2480); x += 3) {
        u8g2.drawPixel(x, 39);
    }

    const bool pulse = ((frameTick / 4) & 1) == 0;
    drawBleMarker(2402, "37", pulse);
    drawBleMarker(2426, "38", !pulse);
    drawBleMarker(2480, "39", pulse);

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(55, 36, "BLE");
}

static void drawLiveBars() {
    const uint8_t baseY = 63;
    for (uint8_t i = 0; i < RF_BINS; i++) {
        uint8_t h = map(constrain(profile[i], 0, SAMPLES_PER_BIN),
                        0, SAMPLES_PER_BIN,
                        0, 10);
        uint8_t x = xForMhz(FIRST_MHZ + i);
        if (h > 0) {
            u8g2.drawVLine(x, baseY - h, h);
        } else if ((i & 3) == 0) {
            u8g2.drawPixel(x, baseY);
        }
    }

    u8g2.setFont(u8g2_font_4x6_tf);
    char meta[18];
    snprintf(meta, sizeof(meta), "RF S%03u L%02u", sweeps, hotLevel);
    u8g2.drawStr(2, 57, meta);
}

static void drawCoexView() {
    char status[10];
    snprintf(status, sizeof(status), "H%04u", hotMhz);
    UiTheme::drawHeader(u8g2, "COEX VIEW", status);

    drawWifiBand(1);
    drawWifiBand(6);
    drawWifiBand(11);
    drawBleLane();
    drawLiveBars();
}

void coexViewEnter() {
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

    memset(profile, 0, sizeof(profile));
    sweeps = 0;
    hotMhz = FIRST_MHZ;
    hotLevel = 0;
    frameTick = 0;
    Input.resetAll();
}

void coexViewExit() {
    jam1.stopListening();
    jam2.stopListening();
    memset(profile, 0, sizeof(profile));
}

void coexViewLoop() {
    if (Input.pressed(BTN_ID_OK)) {
        memset(profile, 0, sizeof(profile));
        sweeps = 0;
        Input.consume(BTN_ID_OK);
    }

    scanProfile();
    AudioFeedback::activity(AUDIO_ACTIVITY_RF, min<uint16_t>(100, hotLevel * 5));
    frameTick++;

    u8g2.clearBuffer();
    drawCoexView();
    u8g2.sendBuffer();
}
