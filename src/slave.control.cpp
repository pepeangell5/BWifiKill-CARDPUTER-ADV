#include "slave_control.h"
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include "input_manager.h"
#include "app_config.h"
#include "ui_theme.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

static uint8_t macEsclavo[] = {0x00, 0x70, 0x07, 0x26, 0x6E, 0xE8};
static uint8_t macBroadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static esp_now_peer_info_t peerInfo;
static bool estadoEsclavoActual = false;
static Preferences prefsMaestro;
static bool initialized = false;
static unsigned long lastSentMs = 0;

static void addSlavePeer(const uint8_t* mac) {
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 1;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = false;

    if (esp_now_is_peer_exist(mac)) {
        esp_now_del_peer(mac);
    }
    esp_now_add_peer(&peerInfo);
}

static bool ensureSlaveRadio() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (!initialized) {
        esp_err_t result = esp_now_init();
        if (result != ESP_OK) {
            esp_now_deinit();
            delay(20);
            result = esp_now_init();
        }
        initialized = (result == ESP_OK);
    }

    if (!initialized) return false;

    addSlavePeer(macEsclavo);
    addSlavePeer(macBroadcast);
    return true;
}

static void sendSlaveState(bool state) {
    if (!ensureSlaveRadio()) return;

    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    struct_comando cmd;
    cmd.estado = state;

    int attempts = state ? 18 : 120;
    int waitMs = state ? 25 : 20;
    for (int i = 0; i < attempts; i++) {
        esp_now_send(macEsclavo, (uint8_t*)&cmd, sizeof(cmd));
        esp_now_send(macBroadcast, (uint8_t*)&cmd, sizeof(cmd));
        delay(waitMs);
    }
    lastSentMs = millis();
}

void slaveControlEnter() {
    prefsMaestro.begin("master_cfg", false);
    estadoEsclavoActual = prefsMaestro.getBool("esc_state", false);
    prefsMaestro.end();

    ensureSlaveRadio();

    Input.resetAll();
    lastSentMs = 0;
}

void slaveControlExit() {
    if (initialized) {
        esp_now_del_peer(macEsclavo);
        esp_now_deinit();
        initialized = false;
    }
    WiFi.mode(WIFI_OFF);
}

void slaveControlSetup() {
    slaveControlEnter();
}

static void drawMacShort(int x, int y) {
    char macStr[12];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X",
             macEsclavo[3], macEsclavo[4], macEsclavo[5]);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(x, y, macStr);
}

static bool drawSendingPulse(uint32_t ms) {
    uint32_t since = ms - lastSentMs;
    if (since > 900) return false;

    int r = 5 + (int)((since * 20) / 900);
    if (r < 22) {
        u8g2.drawCircle(64, 42, r);
    }
    return true;
}

static void drawNode(int x, int y, bool active) {
    u8g2.drawRFrame(x - 8, y - 5, 16, 12, 2);
    u8g2.drawVLine(x, y - 12, 7);
    u8g2.drawHLine(x - 3, y - 12, 7);
    if (active) {
        u8g2.drawDisc(x - 4, y, 1);
        u8g2.drawDisc(x + 4, y, 1);
    } else {
        u8g2.drawPixel(x - 4, y);
        u8g2.drawPixel(x + 4, y);
    }
}

static void drawLinkScene(uint32_t ms) {
    bool pulse = drawSendingPulse(ms);
    bool active = estadoEsclavoActual || pulse;

    drawNode(28, 42, true);
    drawNode(100, 42, estadoEsclavoActual);

    u8g2.drawHLine(40, 42, 48);
    u8g2.drawHLine(40, 43, 48);

    if (active) {
        int packetX = 42 + ((ms / 55) % 42);
        u8g2.drawBox(packetX, 39, 4, 4);
        u8g2.drawPixel(packetX + 6, 41);
    } else {
        for (int x = 42; x < 88; x += 8) {
            u8g2.drawPixel(x, 42);
        }
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(7, 59, "MASTER");
    u8g2.drawStr(88, 59, "SLAVE");
}

void slaveControlLoop() {
    uint32_t ms = millis();

    char status[8];
    snprintf(status, sizeof(status), "%s", estadoEsclavoActual ? "ON" : "OFF");

    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "CONTROL ESCLAVO", status);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(5, 25, "PEER");
    drawMacShort(35, 25);

    drawLinkScene(ms);

    u8g2.sendBuffer();

    if (Input.pressed(BTN_ID_OK)) {
        estadoEsclavoActual = !estadoEsclavoActual;

        prefsMaestro.begin("master_cfg", false);
        prefsMaestro.putBool("esc_state", estadoEsclavoActual);
        prefsMaestro.end();

        sendSlaveState(estadoEsclavoActual);
    }

    if (Input.pressed(BTN_ID_AUX)) {
        estadoEsclavoActual = false;
        prefsMaestro.begin("master_cfg", false);
        prefsMaestro.putBool("esc_state", false);
        prefsMaestro.end();
        sendSlaveState(false);
    }

    if (Input.pressed(BTN_ID_BACK)) {
        runningApp = false;
    }
}
