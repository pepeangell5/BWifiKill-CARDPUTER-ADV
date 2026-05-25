#include "wifi_radar.h"
#include "ui_theme.h"
#include "input_manager.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static const int MAX_RADAR_NETWORKS = 18;
static const uint32_t TRACK_SCAN_INTERVAL_MS = 1200;

struct RadarNetwork {
    char ssid[18];
    char bssid[18];
    uint8_t bssidBytes[6];
    int32_t rssi;
    int32_t channel;
    wifi_auth_mode_t auth;
};

enum RadarState {
    RADAR_SCANNING_LIST,
    RADAR_LIST,
    RADAR_TRACK
};

static RadarNetwork networks[MAX_RADAR_NETWORKS];
static RadarNetwork target;
static RadarState radarState = RADAR_SCANNING_LIST;
static int networkCount = 0;
static int selectedIndex = 0;
static bool trackScanRunning = false;
static uint32_t scanStartMs = 0;
static uint32_t lastTrackScanMs = 0;
static int lastListScanResult = 0;
static int currentRssi = -100;
static int previousRssi = -100;
static int missedScans = 0;

static uint8_t signalPercent(int rssi) {
    return constrain(map(rssi, -95, -35, 0, 100), 0, 100);
}

static const char* distanceLabel(int rssi) {
    if (rssi > -50) return "MUY CERCA";
    if (rssi > -65) return "CERCA";
    if (rssi > -78) return "MEDIA";
    return "LEJOS";
}

static const char* trendLabel() {
    int delta = currentRssi - previousRssi;
    if (missedScans > 1) return "SIN SENAL";
    if (delta >= 4) return "ACERCANDO";
    if (delta <= -4) return "ALEJANDO";
    return "ESTABLE";
}

static void startListScan() {
    WiFi.scanDelete();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(80);
    networkCount = 0;
    selectedIndex = 0;
    lastListScanResult = 0;
    radarState = RADAR_SCANNING_LIST;
    scanStartMs = millis();
    int started = WiFi.scanNetworks(true, true, false, 500);
    if (started == WIFI_SCAN_FAILED) {
        delay(100);
        WiFi.scanNetworks(true, true, false, 500);
    }
}

static void copyBssid(uint8_t* src, uint8_t* dst) {
    if (!src) {
        memset(dst, 0, 6);
        return;
    }
    memcpy(dst, src, 6);
}

static bool sameBssid(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6) == 0;
}

static bool alreadyStored(uint8_t* bssid) {
    for (int i = 0; i < networkCount; i++) {
        if (sameBssid(networks[i].bssidBytes, bssid)) return true;
    }
    return false;
}

static void insertNetworkSorted(const RadarNetwork& net) {
    if (networkCount >= MAX_RADAR_NETWORKS) return;
    int pos = networkCount;
    while (pos > 0 && networks[pos - 1].rssi < net.rssi) {
        networks[pos] = networks[pos - 1];
        pos--;
    }
    networks[pos] = net;
    networkCount++;
}

static void buildNetworkList(int result) {
    if (result < 0) result = 0;
    lastListScanResult = result;

    networkCount = 0;
    for (int i = 0; i < result && networkCount < MAX_RADAR_NETWORKS; i++) {
        String ssid;
        uint8_t enc;
        int32_t rssi;
        uint8_t* bssid;
        int32_t channel;
        WiFi.getNetworkInfo(i, ssid, enc, rssi, bssid, channel);
        if (!bssid || alreadyStored(bssid)) continue;

        RadarNetwork net;
        String shown = ssid.length() == 0 ? String("<hidden>") : ssid;
        shown.substring(0, 17).toCharArray(net.ssid, sizeof(net.ssid));
        WiFi.BSSIDstr(i).substring(0, 17).toCharArray(net.bssid, sizeof(net.bssid));
        copyBssid(bssid, net.bssidBytes);
        net.rssi = rssi;
        net.channel = channel;
        net.auth = (wifi_auth_mode_t)enc;
        insertNetworkSorted(net);
    }

    WiFi.scanDelete();
    selectedIndex = 0;
    radarState = RADAR_LIST;
}

static void drawScanningList() {
    u8g2.clearBuffer();
    char status[8];
    snprintf(status, sizeof(status), "%02ds", (int)((millis() - scanStartMs) / 1000));
    UiTheme::drawHeader(u8g2, "WIFI RADAR", status);
    UiTheme::drawSpinner(u8g2, 64, 35, (millis() / 90) & 0xFF);
    UiTheme::drawCenteredText(u8g2, 56, "ESCANEANDO AP");
    u8g2.sendBuffer();
}

static void drawNetworkRow(int idx, int y) {
    bool selected = idx == selectedIndex;
    if (selected) {
        u8g2.drawBox(0, y - 8, 128, 10);
        u8g2.setDrawColor(0);
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, y, selected ? ">" : " ");
    u8g2.drawStr(10, y, networks[idx].ssid);

    u8g2.setCursor(91, y);
    u8g2.print(networks[idx].rssi);
    UiTheme::drawSignalBars(u8g2, 112, y, networks[idx].rssi > -55 ? 4 : networks[idx].rssi > -70 ? 3 : networks[idx].rssi > -85 ? 2 : 1);

    if (selected) u8g2.setDrawColor(1);
}

static void drawNetworkList() {
    u8g2.clearBuffer();
    char status[8];
    snprintf(status, sizeof(status), "%02d/%02d", networkCount == 0 ? 0 : selectedIndex + 1, networkCount);
    UiTheme::drawHeader(u8g2, "WIFI RADAR", status);

    if (networkCount == 0) {
        char msg[18];
        snprintf(msg, sizeof(msg), "RAW %02d OK=RESCAN", lastListScanResult);
        UiTheme::drawToast(u8g2, "SIN REDES", msg);
        u8g2.sendBuffer();
        return;
    }

    int first = (selectedIndex / 5) * 5;
    for (int row = 0; row < 5; row++) {
        int idx = first + row;
        if (idx < networkCount) drawNetworkRow(idx, 24 + row * 9);
    }
    u8g2.sendBuffer();
}

static void beginTargetTrack() {
    target = networks[selectedIndex];
    currentRssi = target.rssi;
    previousRssi = target.rssi;
    missedScans = 0;
    trackScanRunning = false;
    lastTrackScanMs = 0;
    radarState = RADAR_TRACK;
}

static void startTrackScan() {
    WiFi.scanDelete();
    trackScanRunning = true;
    lastTrackScanMs = millis();
    WiFi.scanNetworks(true, true, false, 140, target.channel, nullptr, target.bssidBytes);
}

static void updateTrackScan() {
    if (!trackScanRunning && millis() - lastTrackScanMs >= TRACK_SCAN_INTERVAL_MS) {
        startTrackScan();
        return;
    }

    if (!trackScanRunning) return;
    int result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) return;
    if (result < 0) {
        trackScanRunning = false;
        return;
    }

    bool found = false;
    int bestRssi = currentRssi;
    for (int i = 0; i < result; i++) {
        uint8_t* bssid = WiFi.BSSID(i);
        if (bssid && sameBssid(bssid, target.bssidBytes)) {
            bestRssi = WiFi.RSSI(i);
            found = true;
            break;
        }
    }

    previousRssi = currentRssi;
    if (found) {
        currentRssi = bestRssi;
        missedScans = 0;
    } else if (missedScans < 99) {
        missedScans++;
    }

    WiFi.scanDelete();
    trackScanRunning = false;
}

static void drawRadarRings(int percent) {
    const int cx = 64;
    const int cy = 51;
    u8g2.drawCircle(cx, cy, 8, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
    u8g2.drawCircle(cx, cy, 16, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
    u8g2.drawCircle(cx, cy, 24, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
    u8g2.drawHLine(cx - 26, cy, 52);
    u8g2.drawVLine(cx, cy - 24, 24);

    int dotR = map(percent, 0, 100, 24, 4);
    if (missedScans > 1 && (millis() / 200) % 2 == 0) return;
    u8g2.drawDisc(cx, cy - dotR, 3);
    u8g2.drawLine(cx, cy, cx, cy - dotR);
}

static void drawTrackView() {
    u8g2.clearBuffer();
    char status[8];
    snprintf(status, sizeof(status), "%ddB", currentRssi);
    UiTheme::drawHeader(u8g2, "WIFI RADAR", status);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 22);
    u8g2.print(target.ssid);
    u8g2.setCursor(92, 22);
    u8g2.print("CH"); u8g2.print(target.channel);

    uint8_t pct = signalPercent(currentRssi);
    drawRadarRings(pct);
    UiTheme::drawProgressBar(u8g2, 8, 56, 78, 6, pct);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(91, 55, distanceLabel(currentRssi));
    u8g2.drawStr(91, 63, trendLabel());
    u8g2.sendBuffer();
}

void wifiRadarEnter() {
    startListScan();
    Input.resetAll();
}

void wifiRadarExit() {
    esp_wifi_scan_stop();
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    radarState = RADAR_SCANNING_LIST;
    trackScanRunning = false;
}

void wifiRadarLoop() {
    if (radarState == RADAR_SCANNING_LIST) {
        int result = WiFi.scanComplete();
        if (result == WIFI_SCAN_RUNNING) {
            drawScanningList();
            return;
        }
        if (result < 0 && millis() - scanStartMs < 10000) {
            drawScanningList();
            return;
        }
        buildNetworkList(result);
    }

    if (radarState == RADAR_LIST) {
        if (networkCount > 0) {
            if (Input.repeating(BTN_ID_DOWN)) selectedIndex = (selectedIndex + 1) % networkCount;
            if (Input.repeating(BTN_ID_UP)) selectedIndex = (selectedIndex - 1 + networkCount) % networkCount;
        }
        if (Input.pressed(BTN_ID_OK)) {
            if (networkCount > 0) beginTargetTrack();
            else startListScan();
        }
        if (Input.pressed(BTN_ID_AUX)) startListScan();
        drawNetworkList();
        return;
    }

    if (radarState == RADAR_TRACK) {
        if (Input.pressed(BTN_ID_BACK)) {
            radarState = RADAR_LIST;
            Input.consume(BTN_ID_BACK);
            return;
        }
        if (Input.pressed(BTN_ID_AUX)) {
            startListScan();
            return;
        }
        updateTrackScan();
        drawTrackView();
    }
}
