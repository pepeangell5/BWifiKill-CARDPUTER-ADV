#include "jammer.h"
#include <RF24.h>
#include <U8g2lib.h>
#include "ui_theme.h"

extern RF24 jam1;
extern RF24 jam2;
extern U8G2 u8g2;

#define BTN_UP 26
#define BTN_DOWN 33
#define BTN_OK 32
#define BTN_BACK 25

int jamChannel = 1;
bool isAttacking = false;

const byte noise_payload[] = {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA};

static void drawChannelGauge(int channel, bool active) {
    char status[8];
    snprintf(status, sizeof(status), "CH:%02d", channel);
    UiTheme::drawHeader(u8g2, "JAMMER CANAL", status);

    u8g2.setFont(u8g2_font_6x12_tr);
    char chText[10];
    snprintf(chText, sizeof(chText), "%02d", channel);
    int tw = u8g2.getStrWidth(chText);
    u8g2.drawStr((128 - tw) / 2, 31, chText);

    int pct = 7 + ((channel - 1) * 93) / 13;
    UiTheme::drawProgressBar(u8g2, 16, 38, 96, 7, pct);

    u8g2.setFont(u8g2_font_5x7_tr);
    if (active) {
        uint8_t frame = (millis() / 80) & 0xFF;
        for (int i = 0; i < 13; i++) {
            uint8_t h = 3 + ((frame + i * 3) % 16);
            u8g2.drawBox(8 + i * 9, 63 - h, 5, h);
        }
        UiTheme::drawCenteredText(u8g2, 53, "ACTIVO");
    } else {
        u8g2.drawFrame(35, 50, 58, 10);
        UiTheme::drawCenteredText(u8g2, 58, "LISTO");
    }
}

void jammerSetup() {
    jam1.begin();
    jam2.begin();

    jam1.setAddressWidth(3);
    jam1.setRetries(0, 0);
    jam1.setDataRate(RF24_2MBPS);
    jam1.setAutoAck(false);
    jam1.stopListening();

    jam2.setAddressWidth(3);
    jam2.setRetries(0, 0);
    jam2.setDataRate(RF24_2MBPS);
    jam2.setAutoAck(false);
    jam2.stopListening();
}

void jammerLoop() {
    if (digitalRead(BTN_BACK) == LOW) {
        isAttacking = false;
        jam1.stopConstCarrier();
        jam2.stopConstCarrier();
        delay(200);
        return;
    }

    if (digitalRead(BTN_UP) == LOW) {
        if (jamChannel < 14) {
            jamChannel++;
            if (isAttacking) {
                int freq = (jamChannel * 5) + 2;
                jam1.startConstCarrier(RF24_PA_MAX, (uint8_t)freq);
            }
        }
        delay(200);
    }

    if (digitalRead(BTN_DOWN) == LOW) {
        if (jamChannel > 1) {
            jamChannel--;
            if (isAttacking) {
                int freq = (jamChannel * 5) + 2;
                jam1.startConstCarrier(RF24_PA_MAX, (uint8_t)freq);
            }
        }
        delay(200);
    }

    if (digitalRead(BTN_OK) == LOW) {
        isAttacking = !isAttacking;
        int freq = (jamChannel * 5) + 2;
        if (isAttacking) {
            jam1.startConstCarrier(RF24_PA_MAX, (uint8_t)freq);
        } else {
            jam1.stopConstCarrier();
            jam2.stopConstCarrier();
        }
        delay(400);
    }

    u8g2.clearBuffer();
    drawChannelGauge(jamChannel, isAttacking);
    u8g2.sendBuffer();

    if (isAttacking) {
        int freq = (jamChannel * 5) + 2;
        jam2.setChannel(freq);
        for (int i = 0; i < 20; i++) {
            jam2.startWrite(&noise_payload, sizeof(noise_payload), true);
        }
    }
}
