#include "ip_scanner.h"
#include "ui_theme.h"
#include "app_config.h"
#include "input_manager.h"
#include <U8g2lib.h>
#include <WiFi.h>
#include <ESP32Ping.h>
#include <AsyncUDP.h>
#include <esp_wifi.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern String target_ssid;
extern bool runningApp;

// --- Estado del módulo ---
static int    current_scan_ip = 1;
static int    found_ips = 0;
static bool   scanFinished = false;
static String foundList[20];
static int    list_selection = 0;
static int    scroll_offset = 0;
static bool   inSubMenu = false;
static int    subMenuIndex = 0;
static String selectedIP = "";
static const char* subMenuLabels[] = {
    "1. PING TEST", "2. UDP FLOOD", "3. PORT SCAN", "<- VOLVER"
};

static const int MAX_OPEN_NETWORKS = 12;
static int    openIdx[MAX_OPEN_NETWORKS];
static int    openCount = 0;
static int    openSelection = 0;
static String connectingSSID = "";
static uint32_t wifiScanStartMs = 0;
static uint32_t connectStartMs = 0;
static uint32_t connectFailMs = 0;

enum WifiJoinState : uint8_t {
    WIFI_READY = 0,
    WIFI_SCAN_OPEN,
    WIFI_OPEN_LIST,
    WIFI_CONNECTING,
    WIFI_CONNECT_FAILED
};

static WifiJoinState wifiJoinState = WIFI_READY;

// Bitmaps de 256 bits (32 bytes) para estado por host
static uint8_t hostScanned[32];
static uint8_t hostFound[32];

static inline void markScanned(int ip) { hostScanned[ip >> 3] |= (1 << (ip & 7)); }
static inline void markFound(int ip)   { hostFound[ip >> 3]   |= (1 << (ip & 7)); }
static inline bool isScanned(int ip)   { return hostScanned[ip >> 3] & (1 << (ip & 7)); }
static inline bool isFound(int ip)     { return hostFound[ip >> 3]   & (1 << (ip & 7)); }

static uint8_t wifiBars(int rssi) {
    if (rssi > -55) return 4;
    if (rssi > -70) return 3;
    if (rssi > -85) return 2;
    return 1;
}

static void resetHostScanState() {
    found_ips        = 0;
    current_scan_ip  = 1;
    scanFinished     = false;
    list_selection   = 0;
    scroll_offset    = 0;
    inSubMenu        = false;
    subMenuIndex     = 0;
    selectedIP       = "";
    for (int i = 0; i < 20; i++) foundList[i] = "";
    memset(hostScanned, 0, sizeof(hostScanned));
    memset(hostFound,   0, sizeof(hostFound));
}

static bool sameOpenSsidAlreadyAdded(const String& ssid) {
    for (int i = 0; i < openCount; i++) {
        if (WiFi.SSID(openIdx[i]) == ssid) return true;
    }
    return false;
}

static void buildOpenNetworkList() {
    openCount = 0;
    openSelection = 0;

    int raw = WiFi.scanComplete();
    if (raw <= 0) return;

    const int WORK_MAX = 40;
    int allIdx[WORK_MAX];
    int allN = min(raw, WORK_MAX);
    for (int i = 0; i < allN; i++) allIdx[i] = i;

    for (int i = 1; i < allN; i++) {
        int key = allIdx[i];
        int keyRssi = WiFi.RSSI(key);
        int j = i - 1;
        while (j >= 0 && WiFi.RSSI(allIdx[j]) < keyRssi) {
            allIdx[j + 1] = allIdx[j];
            j--;
        }
        allIdx[j + 1] = key;
    }

    for (int i = 0; i < allN && openCount < MAX_OPEN_NETWORKS; i++) {
        int idx = allIdx[i];
        String ssid = WiFi.SSID(idx);
        if (ssid.length() == 0) continue;
        if (WiFi.encryptionType(idx) != WIFI_AUTH_OPEN) continue;
        if (sameOpenSsidAlreadyAdded(ssid)) continue;
        openIdx[openCount++] = idx;
    }
}

static void startOpenNetworkScan() {
    openCount = 0;
    openSelection = 0;
    connectingSSID = "";
    wifiJoinState = WIFI_SCAN_OPEN;
    wifiScanStartMs = millis();

    WiFi.scanDelete();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(50);
    WiFi.scanNetworks(true, true, false, 500);
}

static void beginOpenConnection() {
    if (openCount <= 0) return;

    connectingSSID = WiFi.SSID(openIdx[openSelection]);
    if (connectingSSID.length() == 0) return;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(50);
    WiFi.begin(connectingSSID.c_str());

    connectStartMs = millis();
    wifiJoinState = WIFI_CONNECTING;
}

// =============================================================
// Acciones del sub-menú — NO se modifican funcionalmente
// =============================================================

void runPingAction(String ip) {
    u8g2.clearBuffer();
    u8g2.drawStr(10, 30, "PINGING...");
    u8g2.sendBuffer();
    bool success = Ping.ping(ip.c_str(), 3);
    u8g2.clearBuffer();
    u8g2.setCursor(10, 30);
    u8g2.print(success ? "RESPONDE: SI" : "RESPONDE: NO");
    u8g2.sendBuffer();
    while (digitalRead(25) == HIGH);
    delay(200);
}

void runFloodAction(String ip) {
    AsyncUDP udp;
    u8g2.clearBuffer();
    u8g2.drawStr(10, 20, "FLOODING...");
    u8g2.setCursor(10, 40);
    u8g2.print(ip);
    u8g2.sendBuffer();
    while (digitalRead(25) == HIGH) {
        udp.writeTo((uint8_t*)"SYSTEM_OVERLOAD", 15,
                    IPAddress().fromString(ip), 80);
        delay(1);
    }
    delay(200);
}

void runScanAction(String ip) {
    u8g2.clearBuffer();
    u8g2.drawStr(10, 15, "IDENTIFICANDO...");
    u8g2.sendBuffer();
    int ports[] = {80, 443, 135, 445, 62078, 8008, 7000, 5000};
    String info = "Desconocido";
    for (int p : ports) {
        WiFiClient c;
        if (c.connect(ip.c_str(), p, 300)) {
            if (p == 62078 || p == 7000) info = "Apple Device";
            else if (p == 135 || p == 445) info = "Windows PC";
            else if (p == 8008 || p == 8009) info = "Google/IoT";
            else if (p == 80 || p == 443) info = "Web/Server";
            else if (p == 5000) info = "Samsung/NAS";
            else info = "Puerto Activo";
            c.stop();
            break;
        }
    }
    u8g2.setCursor(10, 40);
    u8g2.print(info);
    u8g2.sendBuffer();
    while (digitalRead(25) == HIGH);
    delay(200);
}

// =============================================================
// Animación del scan: grilla de hosts iluminándose
// 32 cols × 8 rows × 4×4 px = barrido lineal a través del /24
// =============================================================
static void drawScanGrid() {
    char status[12];
    snprintf(status, sizeof(status), "%03d %02d",
             scanFinished ? 254 : current_scan_ip - 1, found_ips);
    UiTheme::drawHeader(u8g2, "IP SCANNER", status);

    const int cellSize = 4;
    const int gridX0   = 0;
    const int gridY0   = 19;

    for (int ip = 1; ip <= 254; ip++) {
        int col = (ip - 1) % 32;
        int row = (ip - 1) / 32;
        int x = gridX0 + col * cellSize;
        int y = gridY0 + row * cellSize;

        if (isFound(ip)) {
            // Host respondió: bloque 3×3 visible
            u8g2.drawBox(x, y, 3, 3);
        } else if (isScanned(ip)) {
            // Escaneado sin respuesta: punto central
            u8g2.drawPixel(x + 1, y + 1);
        }
        // No escaneado: vacío
    }

    // Cursor del IP actual: marco 4×4 parpadeante
    if (!scanFinished && current_scan_ip >= 1 && current_scan_ip <= 254) {
        int col = (current_scan_ip - 1) % 32;
        int row = (current_scan_ip - 1) / 32;
        int x = gridX0 + col * cellSize;
        int y = gridY0 + row * cellSize;
        if ((millis() / 250) % 2 == 0) {
            u8g2.drawFrame(x, y, cellSize, cellSize);
        }
    }

    // Info abajo
    u8g2.setFont(u8g2_font_5x7_tr);
    if (WiFi.status() == WL_CONNECTED && !scanFinished) {
        if (found_ips > 0 && ((millis() / 1200) % 2 == 0)) {
            UiTheme::drawCenteredText(u8g2, 62, "OK = VER LISTA");
            return;
        }

        char ipBuf[24];
        IPAddress baseIp = WiFi.localIP();
        snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d",
                 baseIp[0], baseIp[1], baseIp[2], current_scan_ip);
        int strW = u8g2.getStrWidth(ipBuf);
        u8g2.drawStr((128 - strW) / 2, 62, ipBuf);
    } else if (scanFinished) {
        UiTheme::drawCenteredText(u8g2, 62, "OK = VER LISTA");
    } else {
        UiTheme::drawCenteredText(u8g2, 62, "FALLO WIFI");
    }
}

static void drawWifiOpenScan(uint32_t elapsedMs) {
    char status[8];
    snprintf(status, sizeof(status), "%02ds", (int)(elapsedMs / 1000));
    UiTheme::drawHeader(u8g2, "IP SCANNER", status);

    const int cx = 64;
    const int cy = 39;
    u8g2.drawFrame(cx - 10, cy + 10, 20, 6);
    u8g2.drawVLine(cx, cy + 5, 5);

    const uint32_t cycleMs = 1400;
    for (int i = 0; i < 3; i++) {
        uint32_t phase = (elapsedMs + i * (cycleMs / 3)) % cycleMs;
        int radius = 5 + (int)(((uint32_t)26 * phase) / cycleMs);
        if (radius > 6 && radius < 30) {
            u8g2.drawCircle(cx, cy + 5, radius,
                            U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        }
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    const char* dots[] = { "BUSCANDO OPEN", "BUSCANDO OPEN.", "BUSCANDO OPEN..", "BUSCANDO OPEN..." };
    UiTheme::drawCenteredText(u8g2, 62, dots[(elapsedMs / 250) % 4]);
}

static void drawOpenNetworkList() {
    char status[8];
    snprintf(status, sizeof(status), "%02d/%02d",
             openCount == 0 ? 0 : openSelection + 1, openCount);
    UiTheme::drawHeader(u8g2, "CONECTAR WIFI", status);

    if (openCount == 0) {
        UiTheme::drawToast(u8g2, "SIN REDES OPEN", "AUX = RESCAN");
        return;
    }

    int first = (openSelection / 5) * 5;
    for (int row = 0; row < 5; row++) {
        int idx = first + row;
        if (idx >= openCount) break;

        int scanIdx = openIdx[idx];
        int y = 24 + row * 9;
        bool selected = idx == openSelection;

        if (selected) {
            u8g2.drawBox(0, y - 8, 128, 10);
            u8g2.setDrawColor(0);
        }

        String ssid = WiFi.SSID(scanIdx);
        if (ssid.length() > 15) ssid = ssid.substring(0, 13) + "..";

        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(2, y, selected ? ">" : " ");
        u8g2.setCursor(10, y);
        u8g2.print(ssid);
        u8g2.setCursor(92, y);
        u8g2.print(WiFi.RSSI(scanIdx));
        UiTheme::drawSignalBars(u8g2, 114, y, wifiBars(WiFi.RSSI(scanIdx)));

        if (selected) u8g2.setDrawColor(1);
    }
}

static void drawConnectingOpenNetwork() {
    UiTheme::drawHeader(u8g2, "WIFI CONNECT", "OPEN");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(8, 29, "SSID:");
    u8g2.setCursor(40, 29);
    u8g2.print(connectingSSID.substring(0, 14));

    uint32_t elapsed = millis() - connectStartMs;
    int progress = min<int>((elapsed * 100) / 12000, 100);
    UiTheme::drawProgressBar(u8g2, 14, 48, 100, 7, progress);
}

static void drawConnectFailed() {
    UiTheme::drawHeader(u8g2, "WIFI CONNECT", "FAIL");
    UiTheme::drawToast(u8g2, "TIMEOUT", "NO CONECTO");
}

static void drawResultList() {
    char status[8];
    snprintf(status, sizeof(status), "%02d/%02d",
             found_ips == 0 ? 0 : list_selection + 1, found_ips);
    UiTheme::drawHeader(u8g2, "IPS ENCONTRADAS", status);

    if (found_ips == 0) {
        UiTheme::drawToast(u8g2, "SIN RESULTADOS", "BACK = SALIR");
        return;
    }

    u8g2.setFont(u8g2_font_6x10_tr);
    for (int i = 0; i < 4; i++) {
        int idx = scroll_offset + i;
        if (idx >= found_ips) break;

        int y = 28 + i * 10;
        bool sel = (idx == list_selection);
        if (sel) {
            u8g2.drawBox(0, y - 9, 128, 11);
            u8g2.setDrawColor(0);
        }
        u8g2.setCursor(5, y);
        u8g2.print(sel ? "> " : "  ");
        u8g2.print(foundList[idx]);
        if (sel) u8g2.setDrawColor(1);
    }
}

static void drawIPSubMenu() {
    UiTheme::drawHeader(u8g2, "OBJETIVO", "ACCION");

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(5, 22);
    u8g2.print("IP: ");
    u8g2.print(selectedIP);
    u8g2.drawHLine(0, 25, 128);

    u8g2.setFont(u8g2_font_6x10_tr);
    for (int i = 0; i < 4; i++) {
        int y = 38 + i * 9;
        bool sel = (subMenuIndex == i);
        if (sel) {
            u8g2.drawBox(0, y - 8, 128, 10);
            u8g2.setDrawColor(0);
        }
        u8g2.setCursor(5, y);
        u8g2.print(sel ? "> " : "  ");
        u8g2.print(subMenuLabels[i]);
        if (sel) u8g2.setDrawColor(1);
    }
}

static void serviceWifiJoinFlow() {
    if (WiFi.status() == WL_CONNECTED) {
        target_ssid = WiFi.SSID();
        WiFi.scanDelete();
        resetHostScanState();
        wifiJoinState = WIFI_READY;
        Input.resetAll();
        return;
    }

    if (wifiJoinState == WIFI_READY) {
        startOpenNetworkScan();
    }

    if (Input.pressed(BTN_ID_BACK)) {
        if (wifiJoinState == WIFI_CONNECTING) {
            WiFi.disconnect(true);
            wifiJoinState = (openCount > 0) ? WIFI_OPEN_LIST : WIFI_SCAN_OPEN;
            Input.consume(BTN_ID_BACK);
            return;
        }

        runningApp = false;
        Input.consume(BTN_ID_BACK);
        return;
    }

    u8g2.clearBuffer();

    if (wifiJoinState == WIFI_SCAN_OPEN) {
        int result = WiFi.scanComplete();
        if (result == WIFI_SCAN_RUNNING) {
            if (millis() - wifiScanStartMs > 9000) {
                esp_wifi_scan_stop();
                buildOpenNetworkList();
                wifiJoinState = WIFI_OPEN_LIST;
            } else {
                drawWifiOpenScan(millis() - wifiScanStartMs);
            }
            u8g2.sendBuffer();
            return;
        }

        buildOpenNetworkList();
        wifiJoinState = WIFI_OPEN_LIST;
    }

    if (wifiJoinState == WIFI_OPEN_LIST) {
        if (openCount > 0) {
            if (Input.repeating(BTN_ID_UP)) {
                openSelection = (openSelection - 1 + openCount) % openCount;
            }
            if (Input.repeating(BTN_ID_DOWN)) {
                openSelection = (openSelection + 1) % openCount;
            }
            if (Input.pressed(BTN_ID_OK)) {
                beginOpenConnection();
                Input.consume(BTN_ID_OK);
            }
        }

        if (Input.pressed(BTN_ID_AUX)) {
            startOpenNetworkScan();
            Input.consume(BTN_ID_AUX);
        }

        if (wifiJoinState == WIFI_OPEN_LIST) {
            drawOpenNetworkList();
            u8g2.sendBuffer();
        }
        return;
    }

    if (wifiJoinState == WIFI_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            target_ssid = connectingSSID;
            WiFi.scanDelete();
            resetHostScanState();
            wifiJoinState = WIFI_READY;
            Input.resetAll();
            return;
        }

        if (millis() - connectStartMs > 12000) {
            WiFi.disconnect(true);
            connectFailMs = millis();
            wifiJoinState = WIFI_CONNECT_FAILED;
        } else {
            drawConnectingOpenNetwork();
            u8g2.sendBuffer();
            return;
        }
    }

    if (wifiJoinState == WIFI_CONNECT_FAILED) {
        drawConnectFailed();
        u8g2.sendBuffer();
        if (millis() - connectFailMs > 1200) {
            wifiJoinState = WIFI_OPEN_LIST;
        }
        return;
    }
}

// =============================================================
// Ciclo de vida
// =============================================================

void ipScannerSetup() {
    resetHostScanState();
    openCount = 0;
    openSelection = 0;
    connectingSSID = "";

    if (WiFi.status() == WL_CONNECTED) {
        target_ssid = WiFi.SSID();
        wifiJoinState = WIFI_READY;
    } else {
        startOpenNetworkScan();
    }
}

void ipScannerEnter() { ipScannerSetup(); }

void ipScannerExit() {
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    WiFi.scanDelete();
    resetHostScanState();
    openCount = 0;
    openSelection = 0;
    connectingSSID = "";
    wifiJoinState = WIFI_READY;
}

void ipScannerLoop() {
    // BACK propio (main.cpp tiene excepción para menu_index == 12)
    if (Input.pressed(BTN_ID_BACK)) {
        if (inSubMenu) {
            inSubMenu = false;
            return;
        }
        runningApp = false;
        return;
    }

    // -------- Sub-menú --------
    if (WiFi.status() != WL_CONNECTED || wifiJoinState != WIFI_READY) {
        serviceWifiJoinFlow();
        return;
    }

    if (inSubMenu) {
        u8g2.clearBuffer();
        drawIPSubMenu();
        u8g2.sendBuffer();

        if (Input.repeating(BTN_ID_UP)) {
            subMenuIndex--;
            if (subMenuIndex < 0) subMenuIndex = 3;
        }
        if (Input.repeating(BTN_ID_DOWN)) {
            subMenuIndex++;
            if (subMenuIndex > 3) subMenuIndex = 0;
        }
        if (Input.pressed(BTN_ID_OK)) {
            if      (subMenuIndex == 0) runPingAction(selectedIP);
            else if (subMenuIndex == 1) runFloodAction(selectedIP);
            else if (subMenuIndex == 2) runScanAction(selectedIP);
            else if (subMenuIndex == 3) inSubMenu = false;
        }
        return;
    }

    u8g2.clearBuffer();

    if (!scanFinished) {
        if (Input.pressed(BTN_ID_OK) && found_ips > 0) {
            scanFinished = true;
            list_selection = 0;
            scroll_offset = 0;
            Input.consume(BTN_ID_OK);
            drawResultList();
            u8g2.sendBuffer();
            return;
        }

        // Un Ping por iteración (igual que el original)
        IPAddress ip = WiFi.localIP();
        ip[3] = current_scan_ip;
        bool responded = Ping.ping(ip, 1);
        markScanned(current_scan_ip);
        if (responded) {
            markFound(current_scan_ip);
            if (found_ips < 20) {
                foundList[found_ips] = ip.toString();
                found_ips++;
            }
        }
        current_scan_ip++;
        if (current_scan_ip > 254) scanFinished = true;

        drawScanGrid();
    } else {
        // Navegar la lista de resultados
        if (Input.repeating(BTN_ID_UP)) {
            list_selection--;
            if (list_selection < 0) list_selection = max(0, found_ips - 1);
        }
        if (Input.repeating(BTN_ID_DOWN)) {
            list_selection++;
            if (list_selection >= found_ips) list_selection = 0;
        }
        if (list_selection < scroll_offset)      scroll_offset = list_selection;
        if (list_selection >= scroll_offset + 4) scroll_offset = list_selection - 3;

        if (Input.pressed(BTN_ID_OK) && found_ips > 0) {
            selectedIP   = foundList[list_selection];
            inSubMenu    = true;
            subMenuIndex = 0;
        }

        drawResultList();
    }

    u8g2.sendBuffer();
}
