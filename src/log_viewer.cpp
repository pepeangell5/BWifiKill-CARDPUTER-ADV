#include "ui_theme.h"
#include "app_config.h"
#include <Arduino.h>
#include <U8g2lib.h>
#include "FS.h"
#include "SPIFFS.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define BTN_UP 26
#define BTN_DOWN 33
#define BTN_OK 32
#define BTN_ERASE 27

static const char* const LOG_FILE = AppConfig::LOG_FILE_PATH;
static const int MAX_LOG_ROWS = 48;
static const int LOG_ROW_CHARS = 22;

static char logRows[MAX_LOG_ROWS][LOG_ROW_CHARS + 1];
static int logRowCount = 0;
static int logScroll = 0;
static int maxLogScroll = 0;
static bool spiffsReady = false;
static bool cacheLoaded = false;
static unsigned long lastCacheRefresh = 0;
static uint8_t logFrame = 0;

static void drawLogActivity() {
    for (int i = 0; i < 5; i++) {
        uint8_t h = 2 + ((logFrame + i * 3) % 8);
        u8g2.drawVLine(116 + i * 2, 14, h);
    }
}

static void clearLogRows() {
    logRowCount = 0;
    for (int i = 0; i < MAX_LOG_ROWS; i++) {
        logRows[i][0] = '\0';
    }
}

static void clearLogCache() {
    clearLogRows();
    logScroll = 0;
    maxLogScroll = 0;
}

static void appendLogChunk(const String& text, int start, int len) {
    if (logRowCount >= MAX_LOG_ROWS || len <= 0) return;

    int safeLen = min(len, LOG_ROW_CHARS);
    text.substring(start, start + safeLen).toCharArray(logRows[logRowCount], LOG_ROW_CHARS + 1);
    logRowCount++;
}

static void appendLogLine(String line) {
    line.trim();
    if (line.length() == 0) return;

    for (int start = 0; start < line.length() && logRowCount < MAX_LOG_ROWS; start += LOG_ROW_CHARS) {
        appendLogChunk(line, start, line.length() - start);
    }
}

static bool ensureSpiffs() {
    if (spiffsReady) return true;
    spiffsReady = SPIFFS.begin(true);
    return spiffsReady;
}

static void refreshLogCache(bool force = false) {
    unsigned long now = millis();
    if (!force && cacheLoaded && now - lastCacheRefresh < 1500) return;

    lastCacheRefresh = now;
    cacheLoaded = true;
    int previousScroll = logScroll;
    clearLogRows();

    if (!ensureSpiffs()) return;

    File file = SPIFFS.open(LOG_FILE, FILE_READ);
    if (!file || file.size() == 0) {
        if (file) file.close();
        return;
    }

    while (file.available() && logRowCount < MAX_LOG_ROWS) {
        appendLogLine(file.readStringUntil('\n'));
    }
    file.close();

    maxLogScroll = -max(0, (logRowCount * 10) - 42);
    logScroll = previousScroll;
    if (logScroll < maxLogScroll) logScroll = maxLogScroll;
    if (logScroll > 0) logScroll = 0;
}

static void drawEmptyState() {
    UiTheme::drawToast(u8g2, "SIN REGISTROS", "ARCHIVO VACIO");
    UiTheme::drawSpinner(u8g2, 108, 46, logFrame);
}

static void drawLogRows() {
    u8g2.setFont(u8g2_font_5x7_tr);
    int y = 27 + logScroll;

    for (int x = 0; x < 128; x += 8) {
        if (((x / 8) + logFrame) % 5 == 0) {
            u8g2.drawPixel(x, 18);
        }
    }

    for (int i = 0; i < logRowCount; i++) {
        if (y > 16 && y < 63) {
            u8g2.drawStr(3, y, logRows[i]);
        }
        y += 10;
    }

    if (logRowCount > 4) {
        int railH = 41;
        int markerH = max(3, railH / max(1, logRowCount / 2));
        int markerY = 20;
        if (maxLogScroll < 0) {
            markerY = 20 + ((railH - markerH) * (-logScroll)) / (-maxLogScroll);
        }
        u8g2.drawVLine(124, 20, railH);
        u8g2.drawBox(123, markerY, 3, markerH);
    }
}

static void drawDeleteConfirm() {
    UiTheme::drawConfirmBox(u8g2, "BORRAR LOGS?", "CONFIRMAR OK");

    if (digitalRead(BTN_OK) == LOW && ensureSpiffs()) {
        SPIFFS.remove(LOG_FILE);
        cacheLoaded = false;
        clearLogCache();

        u8g2.clearBuffer();
        UiTheme::drawToast(u8g2, "LOGS ELIMINADOS", "LISTO");
        u8g2.sendBuffer();
        delay(1000);
    }
}

void logViewerLoop() {
    logFrame++;

    static bool pinsConfigured = false;
    if (!pinsConfigured) {
        pinMode(BTN_ERASE, INPUT_PULLUP);
        pinMode(BTN_OK, INPUT_PULLUP);
        pinsConfigured = true;
    }

    refreshLogCache();

    if (digitalRead(BTN_UP) == LOW) {
        logScroll += 6;
        if (logScroll > 0) logScroll = 0;
        delay(30);
    }
    if (digitalRead(BTN_DOWN) == LOW) {
        logScroll -= 6;
        if (logScroll < maxLogScroll) logScroll = maxLogScroll;
        delay(30);
    }

    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "LOG VIEWER", cacheLoaded ? "SPIFFS" : "WAIT");
    drawLogActivity();

    if (!spiffsReady && !ensureSpiffs()) {
        UiTheme::drawToast(u8g2, "SPIFFS ERROR", nullptr);
    } else if (logRowCount == 0) {
        drawEmptyState();
    } else {
        drawLogRows();
    }

    if (digitalRead(BTN_ERASE) == LOW && logRowCount > 0) {
        drawDeleteConfirm();
    }

    u8g2.sendBuffer();
}
