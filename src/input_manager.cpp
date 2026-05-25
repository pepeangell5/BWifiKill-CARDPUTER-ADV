#include "input_manager.h"

InputManager Input;

const uint8_t InputManager::PINS[BTN_COUNT] = {
    AppConfig::BTN_UP,
    AppConfig::BTN_DOWN,
    AppConfig::BTN_OK,
    AppConfig::BTN_BACK,
    AppConfig::BTN_AUX
};

void InputManager::begin() {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        pinMode(PINS[i], INPUT_PULLUP);
        stableState[i]  = false;
        rawPrev[i]      = false;
        lastEdgeMs[i]   = 0;
        pressStartMs[i] = 0;
        lastRepeatMs[i] = 0;
        longFired[i]    = false;
        events[i]       = EVT_NONE;
    }
}

void InputManager::update() {
    const uint32_t now = millis();

    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        events[i] = EVT_NONE;

        const bool raw = (digitalRead(PINS[i]) == LOW);

        if (raw != rawPrev[i]) {
            lastEdgeMs[i] = now;
            rawPrev[i]    = raw;
        }

        const bool stableElapsed =
            (now - lastEdgeMs[i]) >= AppConfig::INPUT_DEBOUNCE_MS;

        if (stableElapsed && raw != stableState[i]) {
            stableState[i] = raw;

            if (raw) {
                events[i]       = EVT_PRESSED;
                pressStartMs[i] = now;
                lastRepeatMs[i] = now;
                longFired[i]    = false;
            } else {
                events[i] = EVT_RELEASED;
            }
            continue;
        }

        if (stableState[i]) {
            const uint32_t holdMs = now - pressStartMs[i];

            if (!longFired[i] && holdMs >= AppConfig::INPUT_LONG_PRESS_MS) {
                events[i]    = EVT_LONG_PRESS;
                longFired[i] = true;
                continue;
            }

            if (holdMs >= AppConfig::INPUT_REPEAT_DELAY_MS &&
                (now - lastRepeatMs[i]) >= AppConfig::INPUT_REPEAT_RATE_MS) {
                events[i]       = EVT_REPEAT;
                lastRepeatMs[i] = now;
            }
        }
    }
}

uint32_t InputManager::heldMs(ButtonId id) const {
    if (!stableState[id]) return 0;
    return millis() - pressStartMs[id];
}

void InputManager::consume(ButtonId id) {
    if (id >= BTN_COUNT) return;
    events[id] = EVT_NONE;
}

void InputManager::resetAll() {
    const uint32_t now = millis();
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        stableState[i]  = false;
        rawPrev[i]      = false;
        lastEdgeMs[i]   = now;
        pressStartMs[i] = 0;
        lastRepeatMs[i] = 0;
        longFired[i]    = false;
        events[i]       = EVT_NONE;
    }
}