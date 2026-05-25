#include "wifi_channel_scan.h"
#include "ui_theme.h"
#include "input_manager.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static const int MAX_APS = 40;
static const int MAX_CHANNELS = 14;

struct ChannelAp {
    char ssid[18];
    char bssid[18];
    int32_t rssi;
    int32_t channel;
    wifi_auth_mode_t auth;
    bool hidden;
};

enum ChannelScanState {
    CH_SCAN_SCANNING,
    CH_SCAN_CHANNELS,
    CH_SCAN_APS,
    CH_SCAN_DETAIL
};

static ChannelAp aps[MAX_APS];
static uint8_t channelCounts[MAX_CHANNELS + 1];
static ChannelScanState scanState = CH_SCAN_SCANNING;
static uint32_t scanStartMs = 0;
static int apCount = 0;
static int selectedChannel = 1;
static int selectedApInChannel = 0;
static int detailApIndex = -1;
static int lastRawScanCount = 0;

static const char* authLabel(wifi_auth_mode_t authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN:           return "OPEN";
        case WIFI_AUTH_WEP:            return "WEP";
        case WIFI_AUTH_WPA_PSK:        return "WPA";
        case WIFI_AUTH_WPA2_PSK:       return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:   return "WPA/WPA2";
        default:                       return "LOCKED";
    }
}

static uint8_t signalBars(int rssi) {
    if (rssi > -55) return 4;
    if (rssi > -70) return 3;
    if (rssi > -85) return 2;
    return 1;
}

static void resetScanData() {
    apCount = 0;
    selectedChannel = 1;
    selectedApInChannel = 0;
    detailApIndex = -1;
    lastRawScanCount = 0;
    memset(channelCounts, 0, sizeof(channelCounts));
    for (int i = 0; i < MAX_APS; i++) {
        aps[i].ssid[0] = '\0';
        aps[i].bssid[0] = '\0';
        aps[i].rssi = -100;
        aps[i].channel = 0;
        aps[i].auth = WIFI_AUTH_OPEN;
        aps[i].hidden = false;
    }
}

static void startChannelScan() {
    WiFi.scanDelete();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(80);
    resetScanData();
    scanState = CH_SCAN_SCANNING;
    scanStartMs = millis();
    int started = WiFi.scanNetworks(true, true, false, 500);
    if (started == WIFI_SCAN_FAILED) {
        delay(100);
        WiFi.scanNetworks(true, true, false, 500);
    }
}

static void insertApSorted(const ChannelAp& ap) {
    if (apCount >= MAX_APS) return;
    int pos = apCount;
    while (pos > 0 && aps[pos - 1].rssi < ap.rssi) {
        aps[pos] = aps[pos - 1];
        pos--;
    }
    aps[pos] = ap;
    apCount++;
}

static void buildScanResults(int result) {
    if (result < 0) result = 0;
    lastRawScanCount = result;
    apCount = 0;
    memset(channelCounts, 0, sizeof(channelCounts));

    for (int i = 0; i < result && apCount < MAX_APS; i++) {
        String ssid;
        uint8_t enc;
        int32_t rssi;
        uint8_t* bssid;
        int32_t channel;
        if (!WiFi.getNetworkInfo(i, ssid, enc, rssi, bssid, channel)) continue;
        if (channel < 1 || channel > MAX_CHANNELS) continue;

        ChannelAp ap;
        ap.hidden = ssid.length() == 0;
        String shown = ap.hidden ? String("<hidden>") : ssid;
        shown.substring(0, 17).toCharArray(ap.ssid, sizeof(ap.ssid));
        WiFi.BSSIDstr(i).substring(0, 17).toCharArray(ap.bssid, sizeof(ap.bssid));
        ap.rssi = rssi;
        ap.channel = channel;
        ap.auth = (wifi_auth_mode_t)enc;
        insertApSorted(ap);
        if (channelCounts[channel] < 255) channelCounts[channel]++;
    }

    WiFi.scanDelete();
    selectedChannel = 1;
    for (int ch = 1; ch <= MAX_CHANNELS; ch++) {
        if (channelCounts[ch] > 0) {
            selectedChannel = ch;
            break;
        }
    }
    selectedApInChannel = 0;
    scanState = CH_SCAN_CHANNELS;
}

static int apsInChannel(int channel) {
    int count = 0;
    for (int i = 0; i < apCount; i++) {
        if (aps[i].channel == channel) count++;
    }
    return count;
}

static int apIndexForChannelRow(int channel, int row) {
    int seen = 0;
    for (int i = 0; i < apCount; i++) {
        if (aps[i].channel != channel) continue;
        if (seen == row) return i;
        seen++;
    }
    return -1;
}

static void drawScanning() {
    u8g2.clearBuffer();
    char status[8];
    snprintf(status, sizeof(status), "%02ds", (int)((millis() - scanStartMs) / 1000));
    UiTheme::drawHeader(u8g2, "CHANNEL SCAN", status);
    UiTheme::drawSpinner(u8g2, 64, 34, (millis() / 90) & 0xFF);
    UiTheme::drawCenteredText(u8g2, 56, "ESCANEANDO CANALES");
    u8g2.sendBuffer();
}

static void drawChannelRow(int channel, int y) {
    bool selected = channel == selectedChannel;
    if (selected) {
        u8g2.drawBox(0, y - 8, 128, 10);
        u8g2.setDrawColor(0);
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    char row[18];
    snprintf(row, sizeof(row), "CH %02d", channel);
    u8g2.drawStr(6, y, row);

    int count = channelCounts[channel];
    u8g2.setCursor(42, y);
    u8g2.print(count);
    u8g2.print(" AP");

    int pct = constrain(count * 14, 0, 100);
    UiTheme::drawProgressBar(u8g2, 80, y - 7, 42, 6, pct);

    if (selected) u8g2.setDrawColor(1);
}

static void drawChannelList() {
    u8g2.clearBuffer();
    char status[12];
    snprintf(status, sizeof(status), "%02d AP", apCount);
    UiTheme::drawHeader(u8g2, "CHANNEL SCAN", status);

    if (apCount == 0) {
        char msg[18];
        snprintf(msg, sizeof(msg), "RAW %02d OK=SCAN", lastRawScanCount);
        UiTheme::drawToast(u8g2, "SIN REDES", msg);
        u8g2.sendBuffer();
        return;
    }

    int first = selectedChannel - 2;
    if (first < 1) first = 1;
    if (first > MAX_CHANNELS - 4) first = MAX_CHANNELS - 4;
    for (int i = 0; i < 5; i++) {
        int ch = first + i;
        if (ch <= MAX_CHANNELS) drawChannelRow(ch, 24 + i * 9);
    }
    u8g2.sendBuffer();
}

static void drawApRow(int apIndex, int rowIndex, int y) {
    bool selected = rowIndex == selectedApInChannel;
    if (selected) {
        u8g2.drawBox(0, y - 8, 128, 10);
        u8g2.setDrawColor(0);
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, y, selected ? ">" : " ");
    u8g2.drawStr(10, y, aps[apIndex].ssid);
    u8g2.setCursor(91, y);
    u8g2.print(aps[apIndex].rssi);
    UiTheme::drawSignalBars(u8g2, 112, y, signalBars(aps[apIndex].rssi));

    if (selected) u8g2.setDrawColor(1);
}

static void drawApList() {
    u8g2.clearBuffer();
    int count = apsInChannel(selectedChannel);
    char status[12];
    snprintf(status, sizeof(status), "CH%02d %02d", selectedChannel, count);
    UiTheme::drawHeader(u8g2, "AP DEL CANAL", status);

    if (count == 0) {
        UiTheme::drawToast(u8g2, "CANAL VACIO", "BACK = CANALES");
        u8g2.sendBuffer();
        return;
    }

    int first = (selectedApInChannel / 5) * 5;
    for (int i = 0; i < 5; i++) {
        int row = first + i;
        int apIndex = apIndexForChannelRow(selectedChannel, row);
        if (apIndex >= 0) drawApRow(apIndex, row, 24 + i * 9);
    }
    u8g2.sendBuffer();
}

static void drawApDetail() {
    u8g2.clearBuffer();
    if (detailApIndex < 0 || detailApIndex >= apCount) {
        UiTheme::drawToast(u8g2, "SIN DETALLE", "BACK");
        u8g2.sendBuffer();
        return;
    }

    const ChannelAp& ap = aps[detailApIndex];
    char status[8];
    snprintf(status, sizeof(status), "CH%02d", (int)ap.channel);
    UiTheme::drawHeader(u8g2, "AP DETALLE", status);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 21);
    u8g2.print("SSID "); u8g2.print(ap.ssid);
    u8g2.setCursor(2, 31);
    u8g2.print("BSSID "); u8g2.print(ap.bssid);
    u8g2.setCursor(2, 41);
    u8g2.print("RSSI "); u8g2.print(ap.rssi); u8g2.print(" dBm");
    UiTheme::drawSignalBars(u8g2, 112, 41, signalBars(ap.rssi));
    u8g2.setCursor(2, 51);
    u8g2.print("SEG "); u8g2.print(authLabel(ap.auth));
    u8g2.setCursor(2, 61);
    u8g2.print(ap.hidden ? "HIDDEN" : "VISIBLE");
    u8g2.sendBuffer();
}

void wifiChannelScanEnter() {
    startChannelScan();
    Input.resetAll();
}

void wifiChannelScanExit() {
    esp_wifi_scan_stop();
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    scanState = CH_SCAN_SCANNING;
}

void wifiChannelScanLoop() {
    if (scanState == CH_SCAN_SCANNING) {
        int result = WiFi.scanComplete();
        if (result == WIFI_SCAN_RUNNING) {
            drawScanning();
            return;
        }
        if (result < 0 && millis() - scanStartMs < 10000) {
            drawScanning();
            return;
        }
        buildScanResults(result);
    }

    if (scanState == CH_SCAN_CHANNELS) {
        if (apCount > 0) {
            if (Input.repeating(BTN_ID_DOWN)) {
                selectedChannel++;
                if (selectedChannel > MAX_CHANNELS) selectedChannel = 1;
                selectedApInChannel = 0;
            }
            if (Input.repeating(BTN_ID_UP)) {
                selectedChannel--;
                if (selectedChannel < 1) selectedChannel = MAX_CHANNELS;
                selectedApInChannel = 0;
            }
        }
        if (Input.pressed(BTN_ID_OK)) {
            if (apCount == 0) startChannelScan();
            else {
                selectedApInChannel = 0;
                scanState = CH_SCAN_APS;
            }
        }
        if (Input.pressed(BTN_ID_AUX)) startChannelScan();
        drawChannelList();
        return;
    }

    if (scanState == CH_SCAN_APS) {
        int count = apsInChannel(selectedChannel);
        if (Input.pressed(BTN_ID_BACK)) {
            scanState = CH_SCAN_CHANNELS;
            Input.consume(BTN_ID_BACK);
            return;
        }
        if (count > 0) {
            if (Input.repeating(BTN_ID_DOWN)) selectedApInChannel = (selectedApInChannel + 1) % count;
            if (Input.repeating(BTN_ID_UP)) selectedApInChannel = (selectedApInChannel - 1 + count) % count;
        }
        if (Input.pressed(BTN_ID_OK) && count > 0) {
            detailApIndex = apIndexForChannelRow(selectedChannel, selectedApInChannel);
            scanState = CH_SCAN_DETAIL;
        }
        drawApList();
        return;
    }

    if (scanState == CH_SCAN_DETAIL) {
        if (Input.pressed(BTN_ID_BACK) || Input.pressed(BTN_ID_OK)) {
            scanState = CH_SCAN_APS;
            Input.consume(BTN_ID_BACK);
            return;
        }
        drawApDetail();
    }
}
