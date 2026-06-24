#include "system_settings.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include "audio_feedback.h"
#include "cardputer_compat.h"
#include "input_manager.h"
#include "ui_theme.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

namespace {
enum SettingItem : uint8_t {
    SET_MUTE = 0,
    SET_VOLUME,
    SET_BRIGHTNESS,
    SET_MENU_SOUNDS,
    SET_MONITOR_SOUNDS,
    SET_RESET,
    SET_COUNT
};

uint8_t selected = 0;
bool editing = false;
uint32_t toastUntil = 0;

uint8_t brightnessPercent() {
#ifdef BWK_CARDPUTER_ADV
    uint8_t raw = cardputerBrightness();
    if (raw <= 20) return 0;
    return (uint16_t)(raw - 20) * 100 / 235;
#else
    return 0;
#endif
}

void setBrightnessPercent(uint8_t percent) {
#ifdef BWK_CARDPUTER_ADV
    percent = percent > 100 ? 100 : percent;
    uint8_t raw = 20 + ((uint16_t)percent * 235 / 100);
    cardputerSetBrightness(raw);
#else
    (void)percent;
#endif
}

void adjustNumeric(int8_t delta) {
    if (selected == SET_VOLUME) {
        int v = (int)AudioFeedback::volume() + delta;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        AudioFeedback::setVolume((uint8_t)v);
        AudioFeedback::select();
    } else if (selected == SET_BRIGHTNESS) {
        int b = (int)brightnessPercent() + delta;
        if (b < 0) b = 0;
        if (b > 100) b = 100;
        setBrightnessPercent((uint8_t)b);
        AudioFeedback::select();
    }
}

void resetDefaults() {
    AudioFeedback::setMuted(false);
    AudioFeedback::setVolume(16);
    AudioFeedback::setMenuSounds(true);
    AudioFeedback::setMonitorSounds(true);
    setBrightnessPercent(68);
    editing = false;
    toastUntil = millis() + 1200;
    AudioFeedback::select();
}

const char* onOff(bool enabled) {
    return enabled ? "ON" : "OFF";
}

void valueText(uint8_t item, char* out, size_t len) {
    switch (item) {
        case SET_MUTE:
            snprintf(out, len, "%s", AudioFeedback::muted() ? "ON" : "OFF");
            break;
        case SET_VOLUME:
            snprintf(out, len, "%u%%", AudioFeedback::volume());
            break;
        case SET_BRIGHTNESS:
            snprintf(out, len, "%u%%", brightnessPercent());
            break;
        case SET_MENU_SOUNDS:
            snprintf(out, len, "%s", onOff(AudioFeedback::menuSounds()));
            break;
        case SET_MONITOR_SOUNDS:
            snprintf(out, len, "%s", onOff(AudioFeedback::monitorSounds()));
            break;
        case SET_RESET:
        default:
            snprintf(out, len, "OK");
            break;
    }
}

const char* labelFor(uint8_t item) {
    switch (item) {
        case SET_MUTE: return "SILENCIO";
        case SET_VOLUME: return "VOLUMEN";
        case SET_BRIGHTNESS: return "BRILLO";
        case SET_MENU_SOUNDS: return "CLICK MENU";
        case SET_MONITOR_SOUNDS: return "BEEP MON";
        case SET_RESET: return "RESET CFG";
        default: return "";
    }
}

void drawRow(uint8_t item, int y) {
    bool active = item == selected;
    if (active) {
        u8g2.drawBox(0, y - 8, 128, 10);
        u8g2.setDrawColor(0);
    }

    char value[12];
    valueText(item, value, sizeof(value));

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(3, y, labelFor(item));
    int valueX = 125 - u8g2.getStrWidth(value);
    u8g2.drawStr(valueX, y, value);

    if (active && editing && (item == SET_VOLUME || item == SET_BRIGHTNESS)) {
        u8g2.drawFrame(valueX - 2, y - 8, u8g2.getStrWidth(value) + 4, 10);
    }

    if (active) u8g2.setDrawColor(1);
}

void drawSettings() {
    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "CONFIG", editing ? "EDIT" : "SIST");

    for (uint8_t i = 0; i < SET_COUNT; i++) {
        drawRow(i, 20 + i * 7);
    }

    u8g2.setFont(u8g2_font_4x6_tf);
    if (editing) {
        u8g2.drawStr(2, 63, "UP/DN cambia  OK guarda");
    } else {
        u8g2.drawStr(2, 63, "OK editar/toggle  BACK sale");
    }

    if (millis() < toastUntil) {
        UiTheme::drawToast(u8g2, "CONFIG", "RESTAURADA");
    }

    u8g2.sendBuffer();
}
}  // namespace

void systemSettingsEnter() {
    selected = 0;
    editing = false;
    toastUntil = 0;
    Input.resetAll();
}

void systemSettingsLoop() {
    if (editing) {
        if (Input.repeating(BTN_ID_UP)) adjustNumeric(5);
        if (Input.repeating(BTN_ID_DOWN)) adjustNumeric(-5);
        if (Input.pressed(BTN_ID_OK)) {
            editing = false;
            AudioFeedback::select();
        }
        if (Input.pressed(BTN_ID_BACK)) {
            editing = false;
            Input.consume(BTN_ID_BACK);
            AudioFeedback::back();
        }
        drawSettings();
        return;
    }

    if (Input.repeating(BTN_ID_UP)) {
        selected = selected == 0 ? SET_COUNT - 1 : selected - 1;
        AudioFeedback::menuMove();
    }
    if (Input.repeating(BTN_ID_DOWN)) {
        selected++;
        if (selected >= SET_COUNT) selected = 0;
        AudioFeedback::menuMove();
    }

    if (Input.pressed(BTN_ID_OK)) {
        switch (selected) {
            case SET_MUTE:
                AudioFeedback::setMuted(!AudioFeedback::muted());
                AudioFeedback::select();
                break;
            case SET_VOLUME:
            case SET_BRIGHTNESS:
                editing = true;
                AudioFeedback::select();
                break;
            case SET_MENU_SOUNDS:
                AudioFeedback::setMenuSounds(!AudioFeedback::menuSounds());
                AudioFeedback::select();
                break;
            case SET_MONITOR_SOUNDS:
                AudioFeedback::setMonitorSounds(!AudioFeedback::monitorSounds());
                AudioFeedback::select();
                break;
            case SET_RESET:
                resetDefaults();
                break;
        }
        Input.consume(BTN_ID_OK);
    }

    drawSettings();
}

void systemSettingsExit() {
    editing = false;
}
