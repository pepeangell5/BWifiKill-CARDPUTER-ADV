#ifndef UI_THEME_H
#define UI_THEME_H

#include <Arduino.h>
#include <U8g2lib.h>

namespace UiTheme {

static const char* const FIRMWARE_NAME = "BWifiKill";
static const char* const FIRMWARE_VERSION = "v4.0";

void drawHeader(U8G2& display, const char* title, const char* status = nullptr);
void drawFooter(U8G2& display, const char* leftHint, const char* rightHint = nullptr);
void drawCenteredText(U8G2& display, int y, const char* text);
void drawProgressBar(U8G2& display, int x, int y, int width, int height, int percent);
void drawToast(U8G2& display, const char* title, const char* message = nullptr);
void drawConfirmBox(U8G2& display, const char* title, const char* hint);
void drawSpinner(U8G2& display, int x, int y, uint8_t frame);
void drawSignalBars(U8G2& display, int x, int y, uint8_t bars);
void drawMiniWave(U8G2& display, int x, int y, uint8_t frame);
void drawSplashFrame(U8G2& display, int progress, uint8_t frame);
void drawBattery(U8G2& display, int x, int y);

}

#endif
