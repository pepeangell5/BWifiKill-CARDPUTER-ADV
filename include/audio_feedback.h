#ifndef AUDIO_FEEDBACK_H
#define AUDIO_FEEDBACK_H

#include <Arduino.h>

enum AudioActivityKind : uint8_t {
    AUDIO_ACTIVITY_RF = 0,
    AUDIO_ACTIVITY_WIFI,
    AUDIO_ACTIVITY_BLE,
    AUDIO_ACTIVITY_PACKET
};

namespace AudioFeedback {
    void begin();
    void saveSettings();
    void setMuted(bool enabled);
    void setVolume(uint8_t percent);
    void setMenuSounds(bool enabled);
    void setMonitorSounds(bool enabled);
    bool muted();
    uint8_t volume();
    bool menuSounds();
    bool monitorSounds();
    void startupStep(uint8_t progress);
    void menuMove();
    void select();
    void back();
    void launch();
    void exitApp();
    void activity(AudioActivityKind kind, uint16_t strength);
    void alert();
}

#endif // AUDIO_FEEDBACK_H
