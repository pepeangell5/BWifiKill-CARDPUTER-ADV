#include "channel_advisor.h"
#include "ui_theme.h"
#include <RF24.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <stdlib.h>
#include <string.h>

extern RF24 jam1;
extern RF24 jam2;
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static const uint8_t RF_CHANNELS = 125;
static const uint8_t WIFI_CHANNELS = 13;
static const uint8_t SAMPLES_PER_CHANNEL = 24;

static uint8_t profile[RF_CHANNELS];
static uint16_t sweeps = 0;
static uint8_t frameTick = 0;

static uint8_t bestWifi[3] = {1, 6, 11};
static uint16_t bestWifiScore[3] = {0, 0, 0};
static uint8_t bestNrf[3] = {2, 40, 80};
static uint16_t bestNrfScore[3] = {0, 0, 0};

static uint8_t sampleRadio(RF24& radio, uint8_t channel) {
    radio.setChannel(channel);
    delayMicroseconds(90);

    uint8_t hits = 0;
    for (uint8_t i = 0; i < SAMPLES_PER_CHANNEL; i++) {
        if (radio.testCarrier()) hits++;
    }
    return hits;
}

static void insertWifiCandidate(uint8_t channel, uint16_t score) {
    for (uint8_t i = 0; i < 3; i++) {
        if (score >= bestWifiScore[i]) continue;
        for (int8_t j = 2; j > i; j--) {
            bestWifi[j] = bestWifi[j - 1];
            bestWifiScore[j] = bestWifiScore[j - 1];
        }
        bestWifi[i] = channel;
        bestWifiScore[i] = score;
        break;
    }
}

static void insertNrfCandidate(uint8_t channel, uint16_t score) {
    for (uint8_t i = 0; i < 3; i++) {
        if (score >= bestNrfScore[i]) continue;
        for (int8_t j = 2; j > i; j--) {
            bestNrf[j] = bestNrf[j - 1];
            bestNrfScore[j] = bestNrfScore[j - 1];
        }
        bestNrf[i] = channel;
        bestNrfScore[i] = score;
        break;
    }
}

static uint16_t wifiScore(uint8_t wifiChannel) {
    int center = 12 + ((int)wifiChannel - 1) * 5;
    uint16_t score = 0;
    for (int ch = center - 11; ch <= center + 11; ch++) {
        if (ch < 0 || ch >= RF_CHANNELS) continue;
        uint8_t distance = abs(ch - center);
        uint8_t weight = distance < 4 ? 3 : (distance < 8 ? 2 : 1);
        score += (uint16_t)profile[ch] * weight;
    }
    return score;
}

static uint16_t nrfScore(uint8_t channel) {
    uint16_t score = 0;
    for (int ch = (int)channel - 2; ch <= (int)channel + 2; ch++) {
        if (ch < 0 || ch >= RF_CHANNELS) continue;
        score += profile[ch];
    }
    return score;
}

static void analyzeProfile() {
    for (uint8_t i = 0; i < 3; i++) {
        bestWifi[i] = 0;
        bestWifiScore[i] = 0xFFFF;
        bestNrf[i] = 0;
        bestNrfScore[i] = 0xFFFF;
    }

    for (uint8_t ch = 1; ch <= WIFI_CHANNELS; ch++) {
        insertWifiCandidate(ch, wifiScore(ch));
    }

    for (uint8_t ch = 2; ch < RF_CHANNELS - 2; ch++) {
        insertNrfCandidate(ch, nrfScore(ch));
    }
}

static void scanProfile() {
    for (uint8_t i = 0; i < 63; i++) {
        uint8_t ch1 = i;
        uint8_t s1 = sampleRadio(jam1, ch1);
        profile[ch1] = (profile[ch1] * 3 + s1) / 4;

        uint8_t ch2 = i + 63;
        if (ch2 < RF_CHANNELS) {
            uint8_t s2 = sampleRadio(jam2, ch2);
            profile[ch2] = (profile[ch2] * 3 + s2) / 4;
        }
    }
    sweeps++;
    analyzeProfile();
}

static const char* qualityLabel() {
    uint16_t s = bestWifiScore[0];
    if (s < 80) return "LIMPIO";
    if (s < 220) return "MEDIO";
    return "RUIDO";
}

static void drawProfileBar() {
    const uint8_t baseY = 62;
    for (uint8_t x = 0; x < 125; x++) {
        uint8_t h = map(constrain(profile[x], 0, SAMPLES_PER_CHANNEL),
                        0, SAMPLES_PER_CHANNEL,
                        0, 12);
        if (h > 0) {
            u8g2.drawVLine(x + 1, baseY - h, h);
        } else if ((x & 3) == 0) {
            u8g2.drawPixel(x + 1, baseY);
        }
    }

    const uint8_t wifiCenters[] = {12, 37, 62};
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t x = wifiCenters[i] + 1;
        u8g2.drawVLine(x, 49, 2);
    }
}

static void drawAdvisor() {
    char status[10];
    snprintf(status, sizeof(status), "S%03u", sweeps);
    UiTheme::drawHeader(u8g2, "CH ADVISOR", status);

    u8g2.setFont(u8g2_font_5x7_tr);

    char line[28];
    snprintf(line, sizeof(line), "WIFI %02u %02u %02u",
             bestWifi[0], bestWifi[1], bestWifi[2]);
    u8g2.drawStr(3, 24, line);

    snprintf(line, sizeof(line), "NRF  %03u %03u %03u",
             bestNrf[0], bestNrf[1], bestNrf[2]);
    u8g2.drawStr(3, 34, line);

    snprintf(line, sizeof(line), "CALIDAD %s", qualityLabel());
    u8g2.drawStr(3, 44, line);

    UiTheme::drawMiniWave(u8g2, 106, 43, frameTick);
    drawProfileBar();
}

void channelAdvisorEnter() {
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
    frameTick = 0;
    analyzeProfile();
}

void channelAdvisorExit() {
    jam1.stopListening();
    jam2.stopListening();
    memset(profile, 0, sizeof(profile));
}

void channelAdvisorLoop() {
    scanProfile();
    frameTick++;

    u8g2.clearBuffer();
    drawAdvisor();
    u8g2.sendBuffer();
}
