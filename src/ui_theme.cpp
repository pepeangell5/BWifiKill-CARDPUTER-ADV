#include "ui_theme.h"

#ifdef BWK_CARDPUTER_ADV
#include "cardputer_compat.h"
#endif

namespace UiTheme {

static int centeredX(U8G2& display, const char* text) {
    return (128 - display.getStrWidth(text)) / 2;
}

void drawHeader(U8G2& display, const char* title, const char* status) {
    display.setDrawColor(1);
    display.drawBox(0, 0, 128, 14);
    display.setDrawColor(0);

    int statusX = 127;
    if (status != nullptr) {
        display.setFont(u8g2_font_5x7_tr);
        statusX = 125 - display.getStrWidth(status);
        display.drawStr(statusX, 10, status);
    }

    display.setFont(u8g2_font_helvB08_tr);
    if (display.getStrWidth(title) > statusX - 7) {
        display.setFont(u8g2_font_5x7_tr);
    }
    display.drawStr(3, 10, title);

    display.setDrawColor(1);
    display.drawHLine(0, 15, 128);
}

void drawFooter(U8G2& display, const char* leftHint, const char* rightHint) {
    (void)display;
    (void)leftHint;
    (void)rightHint;
}

void drawCenteredText(U8G2& display, int y, const char* text) {
    display.drawStr(centeredX(display, text), y, text);
}

void drawProgressBar(U8G2& display, int x, int y, int width, int height, int percent) {
    int safePercent = constrain(percent, 0, 100);
    int innerWidth = width - 4;
    int fillWidth = (innerWidth * safePercent) / 100;

    display.drawFrame(x, y, width, height);
    if (fillWidth > 0) {
        display.drawBox(x + 2, y + 2, fillWidth, height - 4);
    }
}

void drawToast(U8G2& display, const char* title, const char* message) {
    display.drawRFrame(7, 18, 114, 35, 4);
    display.setFont(u8g2_font_6x12_tr);
    drawCenteredText(display, 32, title);
    if (message != nullptr) {
        display.setFont(u8g2_font_5x7_tr);
        drawCenteredText(display, 46, message);
    }
}

void drawConfirmBox(U8G2& display, const char* title, const char* hint) {
    display.setDrawColor(0);
    display.drawBox(4, 18, 120, 34);
    display.setDrawColor(1);
    display.drawFrame(4, 18, 120, 34);
    display.setFont(u8g2_font_6x10_tr);
    drawCenteredText(display, 32, title);
    display.setFont(u8g2_font_5x7_tr);
    drawCenteredText(display, 46, hint);
}

void drawSpinner(U8G2& display, int x, int y, uint8_t frame) {
    uint8_t phase = frame % 4;
    display.drawCircle(x, y, 6);
    if (phase == 0) display.drawDisc(x, y - 6, 2);
    else if (phase == 1) display.drawDisc(x + 6, y, 2);
    else if (phase == 2) display.drawDisc(x, y + 6, 2);
    else display.drawDisc(x - 6, y, 2);
}

void drawSignalBars(U8G2& display, int x, int y, uint8_t bars) {
    uint8_t safeBars = min<uint8_t>(bars, 4);
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t barHeight = (i + 1) * 2;
        if (i < safeBars) {
            display.drawBox(x + (i * 4), y - barHeight, 3, barHeight);
        } else {
            display.drawFrame(x + (i * 4), y - barHeight, 3, barHeight);
        }
    }
}

void drawMiniWave(U8G2& display, int x, int y, uint8_t frame) {
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t height = 3 + ((frame + i * 2) % 8);
        display.drawVLine(x + (i * 5), y - height, height);
    }
}

void drawSplashFrame(U8G2& display, int progress, uint8_t frame) {
    display.clearBuffer();
    display.drawRFrame(0, 0, 128, 64, 5);

    display.setFont(u8g2_font_10x20_tr);
    drawCenteredText(display, 25, FIRMWARE_NAME);

    display.setFont(u8g2_font_6x12_tr);
    drawCenteredText(display, 39, "PepeAngell");
    display.setFont(u8g2_font_5x7_tr);
    drawCenteredText(display, 49, FIRMWARE_VERSION);

    drawMiniWave(display, 8, 48, frame);
    drawMiniWave(display, 108, 48, frame + 4);
    drawProgressBar(display, 14, 56, 100, 6, progress);
}

void drawBattery(U8G2& display, int x, int y) {
#ifdef BWK_CARDPUTER_ADV
    int level = cardputerBatteryLevel();
    if (level < 0) return;

    int safeLevel = constrain(level, 0, 100);
    char label[6];
    snprintf(label, sizeof(label), "%d%%", safeLevel);

    display.setDrawColor(0);
    display.setFont(u8g2_font_5x7_tr);
    display.drawStr(x - display.getStrWidth(label), y + 7, label);
    display.setDrawColor(1);
#else
    (void)display;
    (void)x;
    (void)y;
#endif
}

}
