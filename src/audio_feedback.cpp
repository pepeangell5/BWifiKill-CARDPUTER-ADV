#include "audio_feedback.h"
#include "app_config.h"

#ifdef BWK_CARDPUTER_ADV
#ifdef digitalRead
#undef digitalRead
#endif
#ifdef pinMode
#undef pinMode
#endif
#include <M5Cardputer.h>
#endif

namespace {
constexpr uint8_t AUDIO_VOLUME = 42;

uint32_t lastMoveMs = 0;
uint32_t lastActivityMs = 0;
uint32_t lastAlertMs = 0;
uint8_t lastStartupStep = 255;
bool ready = false;

uint16_t clampPercent(uint16_t value) {
    return value > 100 ? 100 : value;
}

void playTone(uint16_t freq, uint16_t durationMs) {
#ifdef BWK_CARDPUTER_ADV
    if (!ready) return;
    M5Cardputer.Speaker.tone(freq, durationMs);
#else
    (void)freq;
    (void)durationMs;
#endif
}

void playToneThrottled(uint16_t freq, uint16_t durationMs, uint32_t& lastMs,
                       uint16_t minGapMs) {
    uint32_t now = millis();
    if (now - lastMs < minGapMs) return;
    lastMs = now;
    playTone(freq, durationMs);
}

uint16_t activityFreq(AudioActivityKind kind, uint16_t strength) {
    uint16_t s = clampPercent(strength);
    switch (kind) {
        case AUDIO_ACTIVITY_RF:
            return 700 + s * 18;
        case AUDIO_ACTIVITY_WIFI:
            return 1200 + s * 20;
        case AUDIO_ACTIVITY_BLE:
            return 1600 + s * 17;
        case AUDIO_ACTIVITY_PACKET:
        default:
            return 1900 + s * 14;
    }
}
}  // namespace

namespace AudioFeedback {

void begin() {
#ifdef BWK_CARDPUTER_ADV
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(AUDIO_VOLUME);
    ready = M5Cardputer.Speaker.isEnabled();
#else
    ready = false;
#endif
}

void startupStep(uint8_t progress) {
    const uint8_t step =
        progress >= 100 ? 3 :
        progress >= 70  ? 2 :
        progress >= 35  ? 1 : 0;

    if (step == lastStartupStep) return;
    lastStartupStep = step;

    static const uint16_t notes[] = {880, 1175, 1568, 2093};
    playTone(notes[step], step == 3 ? 90 : 45);
}

void menuMove() {
    playToneThrottled(4200, 8, lastMoveMs, 45);
}

void select() {
    playTone(3200, 35);
}

void back() {
    playTone(1800, 35);
}

void launch() {
    playTone(2600, 45);
}

void exitApp() {
    playTone(1400, 45);
}

void activity(AudioActivityKind kind, uint16_t strength) {
    if (strength == 0) return;

    uint16_t level = clampPercent(strength);
    uint16_t gap = level > 75 ? 95 : level > 35 ? 150 : 230;
    uint16_t duration = level > 75 ? 18 : 12;

    playToneThrottled(activityFreq(kind, level), duration, lastActivityMs, gap);
}

void alert() {
    playToneThrottled(5200, 40, lastAlertMs, 350);
}

}  // namespace AudioFeedback
