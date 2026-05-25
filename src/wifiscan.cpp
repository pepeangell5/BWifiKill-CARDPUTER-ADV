#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "ui_theme.h"
#include "wifiscan.h"
#include "app_config.h"
#include "input_manager.h"

extern String target_ssid;
extern int    target_channel;
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static const int MAX_NETWORKS = AppConfig::WIFI_SCAN_MAX_NETWORKS;

// --- Estado del módulo ---
static int      sortedIdx[MAX_NETWORKS];
static uint8_t  dupCounts[MAX_NETWORKS];
static int      totalNetworks = 0;
static int      rawTotal       = 0;

static int      selectedNetwork = 0;
static bool     viewingDetails  = false;
static int      detailScrollY   = 0;
static int      maxScrollDetails = 0;
static int      marqueeOffset   = 0;
static unsigned long lastMarqueeUpdate = 0;

// --- Helpers de presentación ---

static uint8_t getSignalBars(int rssi) {
    if (rssi > -55) return 4;
    if (rssi > -70) return 3;
    if (rssi > -85) return 2;
    return 1;
}

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

static String fitSsidForList(const String& ssid, bool selected) {
    if (ssid.length() == 0) return "<hidden>";
    if (!selected && ssid.length() > 15) return ssid.substring(0, 13) + "..";
    if (!selected || ssid.length() <= 15) return ssid;

    int shift = marqueeOffset % (ssid.length() + 5);
    return (ssid + "     " + ssid).substring(shift, shift + 15);
}

// =============================================================
// Animación de scan (router + arcos WiFi)
// =============================================================
static void drawScanAnimation(uint32_t elapsedMs) {
    u8g2.clearBuffer();

    char hdr[8];
    snprintf(hdr, sizeof(hdr), "%02ds", (int)(elapsedMs / 1000));
    UiTheme::drawHeader(u8g2, "WIFI SCANNER", hdr);

    const int rx = 64, ry = 50;
    u8g2.drawFrame(rx - 7, ry, 14, 5);
    u8g2.drawPixel(rx - 4, ry + 2);
    u8g2.drawPixel(rx + 4, ry + 2);
    u8g2.drawVLine(rx - 4, ry - 3, 3);
    u8g2.drawVLine(rx + 4, ry - 3, 3);

    const int ax = rx, ay = ry - 3;
    const uint32_t cycleMs = 1500;
    for (int i = 0; i < 3; i++) {
        uint32_t phase = (elapsedMs + i * (cycleMs / 3)) % cycleMs;
        int r = 4 + (int)(((uint32_t)28 * phase) / cycleMs);
        if (r > 5 && r < 30) {
            u8g2.drawCircle(ax, ay, r,
                            U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        }
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    const char* dotPatterns[] = {"BUSCANDO", "BUSCANDO.",
                                 "BUSCANDO..", "BUSCANDO..."};
    const char* txt = dotPatterns[(elapsedMs / 250) % 4];
    int strW = u8g2.getStrWidth(txt);
    u8g2.drawStr((128 - strW) / 2, 62, txt);

    u8g2.sendBuffer();
}

// --- Sort + dedupe ---
static void buildSortedDedupedList() {
    rawTotal = WiFi.scanComplete();
    if (rawTotal < 0) rawTotal = 0;

    const int WORK_MAX = 40;
    int allIdx[WORK_MAX];
    int allN = (rawTotal > WORK_MAX) ? WORK_MAX : rawTotal;
    for (int i = 0; i < allN; i++) allIdx[i] = i;

    for (int i = 1; i < allN; i++) {
        int key  = allIdx[i];
        int keyR = WiFi.RSSI(key);
        int j    = i - 1;
        while (j >= 0 && WiFi.RSSI(allIdx[j]) < keyR) {
            allIdx[j + 1] = allIdx[j];
            j--;
        }
        allIdx[j + 1] = key;
    }

    totalNetworks = 0;
    for (int i = 0; i < allN && totalNetworks < MAX_NETWORKS; i++) {
        const int candIdx = allIdx[i];
        const String ssid = WiFi.SSID(candIdx);

        bool isDup = false;
        if (ssid.length() > 0) {
            for (int j = 0; j < totalNetworks; j++) {
                if (WiFi.SSID(sortedIdx[j]) == ssid) {
                    if (dupCounts[j] < 99) dupCounts[j]++;
                    isDup = true;
                    break;
                }
            }
        }
        if (!isDup) {
            sortedIdx[totalNetworks] = candIdx;
            dupCounts[totalNetworks] = 1;
            totalNetworks++;
        }
    }
}

static void runScan() {
    WiFi.scanDelete();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(50);

    WiFi.scanNetworks(true, true, false, 500);

    const uint32_t startMs   = millis();
    const uint32_t timeoutMs = 9000;

    while (true) {
        Input.update();

        int16_t result = WiFi.scanComplete();
        if (result != WIFI_SCAN_RUNNING) break;
        if (millis() - startMs > timeoutMs) break;

        if (Input.pressed(BTN_ID_BACK)) {
            esp_wifi_scan_stop();
            WiFi.scanDelete();
            rawTotal      = 0;
            totalNetworks = 0;
            return;
        }

        drawScanAnimation(millis() - startMs);
        delay(40);
    }

    buildSortedDedupedList();

    selectedNetwork    = 0;
    viewingDetails     = false;
    detailScrollY      = 0;
    marqueeOffset      = 0;
    lastMarqueeUpdate  = millis();
}

// =============================================================
// Conexión a red abierta (sin cambios funcionales)
// =============================================================
static void attemptConnection(const String& ssid) {
    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "WIFI CONNECT", "OPEN");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(8, 28, "SSID:");
    u8g2.setCursor(40, 28);
    u8g2.print(ssid.substring(0, 14));
    UiTheme::drawProgressBar(u8g2, 14, 48, 100, 7, 0);
    u8g2.sendBuffer();

    WiFi.begin(ssid.c_str());

    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        UiTheme::drawProgressBar(u8g2, 14, 48, 100, 7, (timeout * 100) / 20);
        u8g2.sendBuffer();
        timeout++;
    }

    u8g2.clearBuffer();
    if (WiFi.status() == WL_CONNECTED) {
        target_ssid = ssid;   // auto-fijamos como target si se conectó
        UiTheme::drawToast(u8g2, "CONECTADO", WiFi.localIP().toString().c_str());
    } else {
        UiTheme::drawToast(u8g2, "TIMEOUT", "NO CONECTO");
    }
    u8g2.sendBuffer();
    delay(2000);
}

static void setAsTarget(int row) {
    const int idx = sortedIdx[row];
    target_ssid    = WiFi.SSID(idx);
    target_channel = WiFi.channel(idx);

    u8g2.clearBuffer();
    UiTheme::drawToast(u8g2, "TARGET FIJADO",
                       target_ssid.length() > 0 ? target_ssid.c_str() : "<hidden>");
    u8g2.sendBuffer();
    delay(1100);
}

// =============================================================
// Drawing — fila simplificada
// =============================================================

static void drawListHeader() {
    char status[12];
    if (totalNetworks == 0) {
        snprintf(status, sizeof(status), "0/0");
    } else if (rawTotal > totalNetworks) {
        snprintf(status, sizeof(status), "%02d/%02d(%d)",
                 selectedNetwork + 1, totalNetworks, rawTotal);
    } else {
        snprintf(status, sizeof(status), "%02d/%02d",
                 selectedNetwork + 1, totalNetworks);
    }
    UiTheme::drawHeader(u8g2, "WIFI SCANNER", status);
}

static void drawNetworkRow(int row, int y) {
    const int idx = sortedIdx[row];
    const bool selected = (row == selectedNetwork);
    const int rssi = WiFi.RSSI(idx);

    if (selected) {
        u8g2.drawBox(0, y - 8, 128, 10);
        u8g2.setDrawColor(0);
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, y, selected ? ">" : " ");

    String ssid = fitSsidForList(WiFi.SSID(idx), selected);
    u8g2.setCursor(10, y);
    u8g2.print(ssid);

    u8g2.setCursor(96, y);
    u8g2.print(rssi);

    UiTheme::drawSignalBars(u8g2, 119, y, getSignalBars(rssi));

    if (selected) u8g2.setDrawColor(1);
}

static void drawNetworkList() {
    drawListHeader();

    if (totalNetworks == 0) {
        UiTheme::drawToast(u8g2, "SIN REDES", "AUX = RESCAN");
        return;
    }

    if (millis() - lastMarqueeUpdate > 150) {
        marqueeOffset++;
        lastMarqueeUpdate = millis();
    }

    int first = (selectedNetwork / 4) * 4;
    for (int row = 0; row < 5; row++) {
        int r = first + row;
        if (r < totalNetworks) {
            drawNetworkRow(r, 24 + (row * 9));
        }
    }
}

static void printDetailLine(int& yOff, const char* label, const String& value) {
    if (yOff > 16 && yOff < 63) {
        u8g2.setCursor(3, yOff);
        u8g2.print(label);
        u8g2.print(value);
    }
    yOff += 11;
}

static void drawNetworkDetails() {
    const int idx     = sortedIdx[selectedNetwork];
    const uint8_t dup = dupCounts[selectedNetwork];

    const int totalLines = (dup > 1) ? 7 : 6;
    maxScrollDetails = -((totalLines * 11) - 42);

    if (Input.held(BTN_ID_DOWN)) {
        detailScrollY -= 4;
        if (detailScrollY < maxScrollDetails) detailScrollY = maxScrollDetails;
    }
    if (Input.held(BTN_ID_UP)) {
        detailScrollY += 4;
        if (detailScrollY > 0) detailScrollY = 0;
    }

    wifi_auth_mode_t authMode = WiFi.encryptionType(idx);
    bool isOpen = (authMode == WIFI_AUTH_OPEN);

    char status[8];
    snprintf(status, sizeof(status), "%s", isOpen ? "OPEN" : "LOCK");
    UiTheme::drawHeader(u8g2, "RED DETALLE", status);

    int yOff = 27 + detailScrollY;
    u8g2.setFont(u8g2_font_5x7_tr);

    String ssid = WiFi.SSID(idx);

    printDetailLine(yOff, "SSID: ",   ssid.length() == 0 ? String("<hidden>") : ssid);
    printDetailLine(yOff, "BSSID: ",  WiFi.BSSIDstr(idx));
    printDetailLine(yOff, "RSSI: ",   String(WiFi.RSSI(idx)) + " dBm");
    printDetailLine(yOff, "SEG: ",    authLabel(authMode));
    printDetailLine(yOff, "CANAL: ",  String(WiFi.channel(idx)));
    if (dup > 1) {
        printDetailLine(yOff, "DUP:  ", String("x") + String((int)dup));
    }
    printDetailLine(yOff, "TARGET: ", target_ssid);

    // -------- Acciones --------
    //   OK en red OPEN    = intentar conexión (como era antes)
    //   OK en red LOCKED  = fijar como target (con toast)
    //   AUX               = fijar como target (cualquier red)
    if (Input.pressed(BTN_ID_OK)) {
        if (isOpen) {
            attemptConnection(ssid);
        } else {
            setAsTarget(selectedNetwork);
        }
        viewingDetails = false;
    } else if (Input.pressed(BTN_ID_AUX)) {
        setAsTarget(selectedNetwork);
        viewingDetails = false;
    }
}

// =============================================================
// Ciclo de vida
// =============================================================

void wifiscanEnter() {
    runScan();
    Input.resetAll();
}

void wifiscanExit() {
    WiFi.scanDelete();
    // OJO: NO hacemos WiFi.mode(WIFI_OFF) si estamos conectados a una red,
    // porque romperíamos la conexión que IP Scanner / dashboard necesitan.
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_OFF);
    }
    totalNetworks    = 0;
    rawTotal         = 0;
    selectedNetwork  = 0;
    viewingDetails   = false;
    detailScrollY    = 0;
    marqueeOffset    = 0;
}

void wifiscanSetup() { wifiscanEnter(); }

void wifiscanLoop() {
    u8g2.clearBuffer();

    if (!viewingDetails) {
        if (totalNetworks > 0) {
            if (Input.repeating(BTN_ID_DOWN)) {
                selectedNetwork = (selectedNetwork + 1) % totalNetworks;
                marqueeOffset   = 0;
            }
            if (Input.repeating(BTN_ID_UP)) {
                selectedNetwork = (selectedNetwork - 1 + totalNetworks) % totalNetworks;
                marqueeOffset   = 0;
            }
            if (Input.pressed(BTN_ID_OK)) {
                viewingDetails = true;
                detailScrollY  = 0;
            }
        }
        if (Input.pressed(BTN_ID_AUX)) {
            runScan();
        }

        drawNetworkList();
    } else {
        if (Input.pressed(BTN_ID_BACK)) {
            viewingDetails = false;
            detailScrollY  = 0;
            Input.consume(BTN_ID_BACK);
        } else {
            drawNetworkDetails();
        }
    }

    u8g2.sendBuffer();
}