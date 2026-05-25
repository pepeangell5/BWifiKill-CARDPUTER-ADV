#include "web_dashboard.h"
#include "app_config.h"
#include "ui_theme.h"
#include <WiFi.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static void drawInfoRow(int y, const char* label, const char* value) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(4, y, label);

    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(35, y, value);
}

void webDashboardLoop() {
    static uint8_t frame = 0;
    frame++;

    char status[8];
    snprintf(status, sizeof(status), "C%u", WiFi.softAPgetStationNum());

    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "WEB DASHBOARD", status);

    drawInfoRow(26, "SSID", AppConfig::WEB_AP_SSID);
    drawInfoRow(39, "PASS", AppConfig::WEB_AP_PASSWORD);
    drawInfoRow(52, "IP", AppConfig::WEB_AP_IP);

    UiTheme::drawMiniWave(u8g2, 7, 63, frame);
    u8g2.sendBuffer();
}
