#include <Arduino.h>
#include <U8g2lib.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <string>
#include <string.h>
#include "btscan.h"
#include "ui_theme.h"
#include "app_config.h"
#include "input_manager.h"
#include "bt_remote.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static const int MAX_BT_DEVICES = 20;
static BLEScan* pBLEScan = nullptr;
static char  btNames[MAX_BT_DEVICES][18];
static char  btAddr[MAX_BT_DEVICES][18];
static char  btMaker[MAX_BT_DEVICES][18];
static char  btType[MAX_BT_DEVICES][12];
static uint16_t btCompanyId[MAX_BT_DEVICES];
static int   btRssi[MAX_BT_DEVICES];
static int   btSelected = 0;
static volatile int btFound = 0;
static volatile bool hasScanResults = false;
static volatile bool scanInProgress = false;
static bool detailView = false;
static uint32_t scanStartMs = 0;

static const int MAX_PINGS = 8;
struct Ping {
    int x, y;
    uint32_t startMs;
};
static Ping pings[MAX_PINGS];
static int  nextPing = 0;

static uint8_t rssiBars(int rssi) {
    if (rssi > -55) return 4;
    if (rssi > -70) return 3;
    if (rssi > -85) return 2;
    return 1;
}

static const char* companyName(uint16_t id) {
    switch (id) {
        case 0x004C: return "Apple";
        case 0x0006: return "Microsoft";
        case 0x0075: return "Samsung";
        case 0x00E0: return "Google";
        case 0x0059: return "Nordic";
        case 0x02E5: return "Espressif";
        case 0x038F: return "Xiaomi";
        case 0x000D: return "TI";
        case 0x000F: return "Broadcom";
        case 0x001D: return "Qualcomm";
        default: return nullptr;
    }
}

static String inferMakerFromName(const String& name) {
    String lower = name;
    lower.toLowerCase();
    if (lower.indexOf("airpods") >= 0 || lower.indexOf("iphone") >= 0 || lower.indexOf("ipad") >= 0) return "Apple";
    if (lower.indexOf("microsoft") >= 0 || lower.indexOf("surface") >= 0 || lower.indexOf("xbox") >= 0) return "Microsoft";
    if (lower.indexOf("galaxy") >= 0 || lower.indexOf("samsung") >= 0) return "Samsung";
    if (lower.indexOf("mi ") >= 0 || lower.indexOf("xiaomi") >= 0 || lower.indexOf("redmi") >= 0) return "Xiaomi";
    if (lower.indexOf("esp") >= 0) return "Espressif";
    return "";
}

static void resetBtCache() {
    btSelected = 0;
    btFound = 0;
    detailView = false;
    for (int i = 0; i < MAX_BT_DEVICES; i++) {
        btNames[i][0] = '\0';
        btAddr[i][0] = '\0';
        btMaker[i][0] = '\0';
        btType[i][0] = '\0';
        btCompanyId[i] = 0;
        btRssi[i] = 0;
    }
    for (int i = 0; i < MAX_PINGS; i++) {
        pings[i].startMs = 0;
    }
    nextPing = 0;
}

static int findDeviceByAddress(const String& address) {
    for (int i = 0; i < btFound; i++) {
        if (address.equalsIgnoreCase(btAddr[i])) return i;
    }
    return -1;
}

static void registerPing(int rssi) {
    int norm = constrain(rssi + 90, 0, 60);
    int dist = 22 - (norm * 16) / 60;
    if (dist < 6)  dist = 6;
    if (dist > 22) dist = 22;

    int angleDeg = random(0, 360);
    float r = angleDeg * 0.0174533f;
    pings[nextPing].x = 64 + (int)(cos(r) * dist);
    pings[nextPing].y = 38 + (int)(sin(r) * dist);
    pings[nextPing].startMs = millis();
    nextPing = (nextPing + 1) % MAX_PINGS;
}

static void storeDevice(int idx, BLEAdvertisedDevice& dev) {
    String address = dev.getAddress().toString().c_str();
    String name = dev.getName().c_str();
    uint16_t companyId = 0;
    String maker = "";

    if (dev.haveManufacturerData()) {
        std::string data = dev.getManufacturerData();
        if (data.length() >= 2) {
            companyId = ((uint8_t)data[1] << 8) | (uint8_t)data[0];
            const char* known = companyName(companyId);
            if (known) {
                maker = known;
            } else {
                char vendor[12];
                snprintf(vendor, sizeof(vendor), "ID %04X", companyId);
                maker = vendor;
            }
        }
    }

    if (maker.length() == 0) maker = inferMakerFromName(name);
    if (maker.length() == 0) maker = "Desconocido";

    String type = dev.haveName() ? "Named" : "BLE";
    if (dev.haveManufacturerData()) type = "MFG DATA";
    if (name.length() == 0) name = maker != "Desconocido" ? maker : address;

    name.substring(0, 17).toCharArray(btNames[idx], sizeof(btNames[idx]));
    address.substring(0, 17).toCharArray(btAddr[idx], sizeof(btAddr[idx]));
    maker.substring(0, 17).toCharArray(btMaker[idx], sizeof(btMaker[idx]));
    type.substring(0, 11).toCharArray(btType[idx], sizeof(btType[idx]));
    btCompanyId[idx] = companyId;
    btRssi[idx] = dev.getRSSI();
    registerPing(btRssi[idx]);
}

class BtDeviceCallback : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        if (!scanInProgress) return;

        String address = advertisedDevice.getAddress().toString().c_str();
        int existing = findDeviceByAddress(address);
        if (existing >= 0) {
            storeDevice(existing, advertisedDevice);
            return;
        }

        if (btFound < MAX_BT_DEVICES) {
            storeDevice(btFound, advertisedDevice);
            btFound++;
        }
    }
};
static BtDeviceCallback deviceCb;

static void onScanCompleteCallback(BLEScanResults results) {
    if (!scanInProgress) return;
    scanInProgress = false;
    hasScanResults = true;
    if (pBLEScan) pBLEScan->clearResults();
}

void btscanSetup() {
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    pBLEScan->setAdvertisedDeviceCallbacks(&deviceCb);
    resetBtCache();
    hasScanResults = false;
    scanInProgress = false;
}

static void drawRadarAnimation(uint32_t elapsedMs) {
    u8g2.clearBuffer();

    char hdr[12];
    snprintf(hdr, sizeof(hdr), "%02ds %02d",
             (int)(elapsedMs / 1000), btFound);
    UiTheme::drawHeader(u8g2, "BT SCANNER", hdr);

    const int cx = 64, cy = 38;
    u8g2.drawCircle(cx, cy,  8);
    u8g2.drawCircle(cx, cy, 16);
    u8g2.drawCircle(cx, cy, 24);
    u8g2.drawHLine(cx - 27, cy, 3);
    u8g2.drawHLine(cx + 25, cy, 3);
    u8g2.drawVLine(cx, cy - 27, 3);
    u8g2.drawVLine(cx, cy + 25, 3);
    u8g2.drawDisc(cx, cy, 2);

    float angle = fmod(elapsedMs * 270.0f / 1000.0f, 360.0f);
    float r0 = angle * 0.0174533f;
    u8g2.drawLine(cx, cy, cx + (int)(cos(r0) * 23), cy + (int)(sin(r0) * 23));
    float r1 = (angle - 22) * 0.0174533f;
    u8g2.drawLine(cx, cy, cx + (int)(cos(r1) * 17), cy + (int)(sin(r1) * 17));
    float r2 = (angle - 44) * 0.0174533f;
    u8g2.drawLine(cx, cy, cx + (int)(cos(r2) * 11), cy + (int)(sin(r2) * 11));

    const uint32_t pingDurMs = 1200;
    const uint32_t now = millis();
    for (int i = 0; i < MAX_PINGS; i++) {
        if (pings[i].startMs == 0) continue;
        uint32_t age = now - pings[i].startMs;
        if (age > pingDurMs) {
            pings[i].startMs = 0;
            continue;
        }
        int ringR = (age * 5) / pingDurMs;
        if (ringR <= 4 && ringR > 0) {
            u8g2.drawCircle(pings[i].x, pings[i].y, ringR);
        }
        if (age < 800) {
            u8g2.drawDisc(pings[i].x, pings[i].y, 1);
        }
    }

    u8g2.sendBuffer();
}

static void runBtScan() {
    resetBtCache();
    hasScanResults = false;
    scanInProgress = true;
    scanStartMs = millis();
    pBLEScan->start(5, onScanCompleteCallback, false);
}

static void drawDeviceRow(int idx, int y) {
    bool selected = idx == btSelected;

    if (selected) {
        u8g2.drawBox(0, y - 8, 128, 10);
        u8g2.setDrawColor(0);
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, y, selected ? ">" : " ");
    u8g2.drawStr(10, y, btNames[idx]);

    int makerW = u8g2.getStrWidth(btMaker[idx]);
    if (strcmp(btNames[idx], btMaker[idx]) != 0 && makerW <= 35) {
        u8g2.drawStr(75, y, btMaker[idx]);
    }
    UiTheme::drawSignalBars(u8g2, 112, y, rssiBars(btRssi[idx]));

    if (selected) u8g2.setDrawColor(1);
}

static void drawBtResults() {
    char status[8];
    snprintf(status, sizeof(status), "%02d/%02d",
             btFound == 0 ? 0 : btSelected + 1, btFound);
    UiTheme::drawHeader(u8g2, "BT SCANNER", status);

    if (btFound == 0) {
        UiTheme::drawToast(u8g2, "SIN DISPOSITIVOS", "OK = RESCAN");
        return;
    }

    int first = (btSelected / 5) * 5;
    for (int i = 0; i < 5; i++) {
        int idx = first + i;
        if (idx < btFound) {
            drawDeviceRow(idx, 24 + (i * 9));
        }
    }
}

static void drawBtDetail() {
    char status[8];
    snprintf(status, sizeof(status), "%02d/%02d", btSelected + 1, btFound);
    UiTheme::drawHeader(u8g2, "BT DETALLE", status);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 21);
    u8g2.print("N: "); u8g2.print(btNames[btSelected]);

    u8g2.setCursor(2, 31);
    u8g2.print("F: "); u8g2.print(btMaker[btSelected]);

    u8g2.setCursor(2, 41);
    u8g2.print("MAC "); u8g2.print(btAddr[btSelected]);

    u8g2.setCursor(2, 51);
    u8g2.print("RSSI "); u8g2.print(btRssi[btSelected]); u8g2.print(" dBm");
    UiTheme::drawSignalBars(u8g2, 112, 51, rssiBars(btRssi[btSelected]));

    u8g2.setCursor(2, 61);
    u8g2.print(btType[btSelected]);
    if (btCompanyId[btSelected] != 0) {
        char idBuf[10];
        snprintf(idBuf, sizeof(idBuf), " %04X", btCompanyId[btSelected]);
        u8g2.print(idBuf);
    }
}

void btscanLoop() {
    if (!scanInProgress && !hasScanResults) {
        runBtScan();
    }

    if (scanInProgress) {
        drawRadarAnimation(millis() - scanStartMs);
        return;
    }

    if (detailView) {
        if (Input.pressed(BTN_ID_BACK)) {
            detailView = false;
            Input.consume(BTN_ID_BACK);
        }
        if (Input.pressed(BTN_ID_OK)) {
            detailView = false;
        }
        u8g2.clearBuffer();
        drawBtDetail();
        u8g2.sendBuffer();
        return;
    }

    if (btFound > 0) {
        if (Input.repeating(BTN_ID_DOWN)) {
            btSelected = (btSelected + 1) % btFound;
        }
        if (Input.repeating(BTN_ID_UP)) {
            btSelected = (btSelected - 1 + btFound) % btFound;
        }
    }

    if (Input.pressed(BTN_ID_OK)) {
        if (btFound > 0) {
            detailView = true;
        } else {
            hasScanResults = false;
            resetBtCache();
        }
    }

    u8g2.clearBuffer();
    drawBtResults();
    u8g2.sendBuffer();
}

void btscanExit() {
    if (pBLEScan != nullptr) {
        if (scanInProgress) {
            scanInProgress = false;
            pBLEScan->stop();
        }
        pBLEScan->clearResults();
        pBLEScan = nullptr;
    }

    if (!btRemoteBleActive()) {
        BLEDevice::deinit(false);
    }

    resetBtCache();
    hasScanResults = false;
    scanInProgress = false;
}
