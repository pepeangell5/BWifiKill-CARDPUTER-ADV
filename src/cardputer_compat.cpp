#include "cardputer_compat.h"

#ifdef BWK_CARDPUTER_ADV

#undef digitalRead
#undef pinMode
#include <M5Cardputer.h>
#include <Preferences.h>

#include "app_config.h"

namespace {
constexpr int SRC_W = 128;
constexpr int SRC_H = 64;
constexpr int DST_W = 240;
constexpr int DST_H = 120;
constexpr int DST_Y = 7;

uint16_t frame[DST_W * DST_H];
bool cardputerReady = false;
uint8_t brightness = 180;

bool keyInWord(char key) {
    const auto& state = M5Cardputer.Keyboard.keysState();
    for (char pressed : state.word) {
        if (pressed == key || pressed == (char)toupper(key)) return true;
    }
    return false;
}

bool buttonForLegacyPin(uint8_t pin) {
    const auto& state = M5Cardputer.Keyboard.keysState();
    if (pin == AppConfig::BTN_UP) return keyInWord(';');
    if (pin == AppConfig::BTN_DOWN) return keyInWord('.');
    if (pin == AppConfig::BTN_OK) return state.enter;
    if (pin == AppConfig::BTN_BACK) return state.del;
    if (pin == AppConfig::BTN_AUX) return state.space;
    return false;
}

bool isLegacyButtonPin(uint8_t pin) {
    return pin == AppConfig::BTN_UP || pin == AppConfig::BTN_DOWN ||
           pin == AppConfig::BTN_OK || pin == AppConfig::BTN_BACK ||
           pin == AppConfig::BTN_AUX;
}
}  // namespace

void cardputerBegin() {
    if (cardputerReady) return;

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Preferences prefs;
    if (prefs.begin("bwkcfg", true)) {
        brightness = prefs.getUChar("bright", brightness);
        prefs.end();
    }
    if (brightness < 20) brightness = 20;
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(brightness);
    M5Cardputer.Display.fillScreen(0x0000);
    cardputerReady = true;

    Serial.println("[BWK] M5Cardputer display and keyboard initialized");
}

void cardputerUpdate() {
    if (cardputerReady) M5Cardputer.update();
}

void cardputerWaitForKeysReleased() {
    if (!cardputerReady) return;
    do {
        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isPressed()) break;
        delay(5);
    } while (true);
}

void cardputerPresent(U8G2& source) {
    if (!cardputerReady) return;

    const uint8_t* buffer = source.getBufferPtr();
    if (!buffer) return;

    for (int y = 0; y < DST_H; ++y) {
        const int srcY = (y * SRC_H) / DST_H;
        const int row = (srcY >> 3) * SRC_W;
        const uint8_t mask = 1U << (srcY & 7);
        for (int x = 0; x < DST_W; ++x) {
            const int srcX = (x * SRC_W) / DST_W;
            frame[y * DST_W + x] = (buffer[row + srcX] & mask)
                                         ? 0xFFFF
                                         : 0x0000;
        }
    }

    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.pushImage(0, DST_Y, DST_W, DST_H, frame);
    M5Cardputer.Display.endWrite();
}

int cardputerDigitalRead(uint8_t pin) {
    if (!isLegacyButtonPin(pin)) return ::digitalRead(pin);
    if (cardputerReady) M5Cardputer.update();
    return buttonForLegacyPin(pin) ? LOW : HIGH;
}

void cardputerPinMode(uint8_t pin, uint8_t mode) {
    if (isLegacyButtonPin(pin)) return;
    ::pinMode(pin, mode);
}

void cardputerSetBrightness(uint8_t value) {
    brightness = value < 20 ? 20 : value;
    if (cardputerReady) M5Cardputer.Display.setBrightness(brightness);

    Preferences prefs;
    if (!prefs.begin("bwkcfg", false)) return;
    prefs.putUChar("bright", brightness);
    prefs.end();
}

uint8_t cardputerBrightness() {
    return brightness;
}

#endif
