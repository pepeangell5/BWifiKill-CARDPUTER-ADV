#include "audio_feedback.h"
#include "app_config.h"
#include <Preferences.h>

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
constexpr uint8_t DEFAULT_VOLUME = 16;
constexpr const char* PREF_NS = "bwkcfg";

uint32_t lastMoveMs = 0;
uint32_t lastActivityMs = 0;
uint32_t lastAlertMs = 0;
uint8_t lastStartupStep = 255;
bool ready = false;
bool mutedState = false;
bool menuSoundsState = true;
bool monitorSoundsState = true;
uint8_t volumeState = DEFAULT_VOLUME;

uint16_t clampPercent(uint16_t value) {
    return value > 100 ? 100 : value;
}

void playTone(uint16_t freq, uint16_t durationMs) {
    if (mutedState || volumeState == 0) return;
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
    Preferences prefs;
    if (prefs.begin(PREF_NS, true)) {
        mutedState = prefs.getBool("audMute", false);
        volumeState = prefs.getUChar("audVol", DEFAULT_VOLUME);
        menuSoundsState = prefs.getBool("audMenu", true);
        monitorSoundsState = prefs.getBool("audMon", true);
        prefs.end();
    }
    volumeState = clampPercent(volumeState);

#ifdef BWK_CARDPUTER_ADV
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume((uint8_t)map(volumeState, 0, 100, 0, 255));
    ready = M5Cardputer.Speaker.isEnabled();
#else
    ready = false;
#endif
}

void saveSettings() {
    Preferences prefs;
    if (!prefs.begin(PREF_NS, false)) return;
    prefs.putBool("audMute", mutedState);
    prefs.putUChar("audVol", volumeState);
    prefs.putBool("audMenu", menuSoundsState);
    prefs.putBool("audMon", monitorSoundsState);
    prefs.end();
}

void setMuted(bool enabled) {
    mutedState = enabled;
    saveSettings();
}

void setVolume(uint8_t percent) {
    volumeState = clampPercent(percent);
#ifdef BWK_CARDPUTER_ADV
    if (ready) M5Cardputer.Speaker.setVolume((uint8_t)map(volumeState, 0, 100, 0, 255));
#endif
    saveSettings();
}

void setMenuSounds(bool enabled) {
    menuSoundsState = enabled;
    saveSettings();
}

void setMonitorSounds(bool enabled) {
    monitorSoundsState = enabled;
    saveSettings();
}

bool muted() { return mutedState; }
uint8_t volume() { return volumeState; }
bool menuSounds() { return menuSoundsState; }
bool monitorSounds() { return monitorSoundsState; }

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
    if (!menuSoundsState) return;
    playToneThrottled(4200, 8, lastMoveMs, 45);
}

void select() {
    if (!menuSoundsState) return;
    playTone(3200, 35);
}

void back() {
    if (!menuSoundsState) return;
    playTone(1800, 35);
}

void launch() {
    if (!menuSoundsState) return;
    playTone(2600, 45);
}

void exitApp() {
    if (!menuSoundsState) return;
    playTone(1400, 45);
}

void activity(AudioActivityKind kind, uint16_t strength) {
    if (!monitorSoundsState) return;
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
