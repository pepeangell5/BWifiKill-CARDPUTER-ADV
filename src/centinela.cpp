#include "centinela.h"
#include "ui_theme.h"
#include "app_config.h"
#include "input_manager.h"
#include "audio_feedback.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

bool attack_confirmed = false;

static volatile unsigned long attack_packets   = 0;
static volatile unsigned long last_sec_packets = 0;
static volatile unsigned long suspicious_packets   = 0;
static volatile unsigned long last_sec_suspicious  = 0;
static unsigned long last_check_time = 0;
static volatile uint32_t last_packet_time = 0;
static volatile uint32_t last_suspicious_time = 0;
static volatile uint8_t last_suspicious_subtype = 0;

static int  selected_ch    = 1;
static bool channel_fixed  = false;

static unsigned long total_packets_view = 0;
static unsigned long sec_packets_view   = 0;
static unsigned long suspicious_view    = 0;
static unsigned long sec_suspicious_view = 0;
static int  current_bar_width = 0;
static uint8_t suspicious_windows = 0;
static uint8_t historyBars[24];
static uint8_t historyIndex = 0;

void centinela_sniffer_callback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* payload = pkt->payload;
    if (!payload) return;

    const uint8_t subtype = payload[0] & 0xF0;
    const bool suspicious =
        subtype == 0xA0 ||  // disassociation
        subtype == 0xC0;    // deauthentication

    attack_packets++;
    last_sec_packets++;
    last_packet_time = millis();

    if (suspicious) {
        suspicious_packets++;
        last_sec_suspicious++;
        last_suspicious_time = millis();
        last_suspicious_subtype = subtype;
    }
}

static void drawChannelDots() {
    for (int i = 1; i <= 13; i++) {
        int x = 8 + ((i - 1) * 10);
        if (i == selected_ch) {
            u8g2.drawDisc(x, 55, 3);
        } else {
            u8g2.drawCircle(x, 55, 2);
        }
    }
}

static void drawChannelSelector() {
    char channelText[4];
    snprintf(channelText, sizeof(channelText), "%02d", selected_ch);

    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "CENTINELA", "V4");

    u8g2.setFont(u8g2_font_5x7_tr);
    UiTheme::drawCenteredText(u8g2, 26, "CANAL DE ESCUCHA");

    u8g2.drawRFrame(45, 30, 38, 18, 4);
    u8g2.setFont(u8g2_font_10x20_tr);
    UiTheme::drawCenteredText(u8g2, 47, channelText);

    drawChannelDots();
    u8g2.sendBuffer();
}

static void drawIntensityMeter(uint32_t pps, bool alert) {
    const int x = 8;
    const int y = 32;
    const int w = 112;
    const int h = 13;
    int fill = constrain(current_bar_width, 0, w);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(x, 30, "INTENSIDAD");
    if (current_bar_width >= 86) {
        u8g2.drawStr(84, 30, "ALTA");
    } else if (current_bar_width >= 28) {
        u8g2.drawStr(79, 30, "MEDIA");
    } else {
        u8g2.drawStr(87, 30, "BAJA");
    }

    u8g2.drawRFrame(x, y, w, h, 3);
    if (fill > 2) {
        u8g2.drawBox(x + 2, y + 2, fill - 3, h - 4);
    }

    for (int mark = 25; mark <= 75; mark += 25) {
        int mx = x + ((w - 1) * mark) / 100;
        u8g2.drawVLine(mx, y + h + 1, 3);
    }

    if (alert) {
        uint8_t pulse = (millis() / 90) % 6;
        u8g2.drawRFrame(x - pulse, y - pulse / 2, w + pulse * 2, h + pulse, 3);
    }
}

static void drawHistory() {
    for (uint8_t i = 0; i < 24; i++) {
        uint8_t pos = (historyIndex + i) % 24;
        uint8_t h = historyBars[pos];
        int x = 6 + (i * 5);
        if (h > 0) {
            u8g2.drawVLine(x, 63 - h, h);
        } else {
            u8g2.drawPixel(x, 63);
        }
    }
}

static void drawMonitor() {
    char status[10];
    snprintf(status, sizeof(status), "CH%02d", selected_ch);

    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2,
                        attack_confirmed ? "ALERTA RF" : "CENTINELA",
                        status);

    u8g2.setFont(u8g2_font_5x7_tr);
    char buf[24];
    snprintf(buf, sizeof(buf), "PPS %lu", sec_packets_view);
    u8g2.drawStr(4, 23, buf);
    snprintf(buf, sizeof(buf), "SUS %lu", sec_suspicious_view);
    u8g2.drawStr(72, 23, buf);
    u8g2.drawHLine(0, 25, 128);

    drawIntensityMeter(sec_packets_view, attack_confirmed);

    u8g2.setFont(u8g2_font_5x7_tr);
    if (attack_confirmed) {
        UiTheme::drawCenteredText(u8g2, 54, "ATAQUE DETECTADO");
    } else if (sec_packets_view > 0) {
        UiTheme::drawCenteredText(u8g2, 54, "ACTIVIDAD");
    } else {
        UiTheme::drawCenteredText(u8g2, 54, "MONITOREANDO");
    }

    drawHistory();
    u8g2.sendBuffer();
}

void runCentinelaSetup() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_promiscuous_filter_t my_filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&my_filter);
    esp_wifi_set_promiscuous_rx_cb(centinela_sniffer_callback);
    esp_wifi_set_channel(selected_ch, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(true);
}

static void updateCounters(unsigned long now) {
    if (now - last_check_time <= 1000) return;

    noInterrupts();
    unsigned long oneSecondPackets = last_sec_packets;
    unsigned long oneSecondSuspicious = last_sec_suspicious;
    last_sec_packets = 0;
    last_sec_suspicious = 0;
    total_packets_view = attack_packets;
    suspicious_view = suspicious_packets;
    uint32_t latestSuspiciousTime = last_suspicious_time;
    interrupts();

    sec_packets_view = oneSecondPackets;
    sec_suspicious_view = oneSecondSuspicious;

    if (oneSecondSuspicious >= 8) {
        if (suspicious_windows < 5) suspicious_windows++;
    } else if (suspicious_windows > 0) {
        suspicious_windows--;
    }

    if (oneSecondSuspicious >= 25 || suspicious_windows >= 2) {
        attack_confirmed = true;
    } else if (oneSecondSuspicious == 0 && now - latestSuspiciousTime > 3000) {
        attack_confirmed = false;
        suspicious_windows = 0;
    }

    AudioFeedback::activity(attack_confirmed ? AUDIO_ACTIVITY_WIFI : AUDIO_ACTIVITY_PACKET,
                            min<unsigned long>(100, oneSecondPackets));

    if (oneSecondPackets == 0) current_bar_width = 0;
    else current_bar_width = map(constrain(oneSecondPackets, 1UL, 250UL), 1, 250, 8, 118);
    if (current_bar_width > 118) current_bar_width = 118;

    historyBars[historyIndex] = constrain(map(oneSecondPackets, 0, 250, 0, 8), 0, 8);
    historyIndex = (historyIndex + 1) % 24;
    last_check_time = now;
}

void centinelaEnter() {
    channel_fixed      = false;
    attack_confirmed   = false;
    total_packets_view = 0;
    sec_packets_view   = 0;
    suspicious_view     = 0;
    sec_suspicious_view = 0;
    current_bar_width  = 0;
    suspicious_windows = 0;
    memset(historyBars, 0, sizeof(historyBars));
    historyIndex = 0;
}

void centinelaExit() {
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    channel_fixed = false;
    attack_confirmed = false;
    suspicious_windows = 0;
}

void centinelaLoop() {
    if (!channel_fixed) {
        drawChannelSelector();

        if (Input.repeating(BTN_ID_UP)) {
            selected_ch++;
            if (selected_ch > 13) selected_ch = 1;
        }
        if (Input.repeating(BTN_ID_DOWN)) {
            selected_ch--;
            if (selected_ch < 1) selected_ch = 13;
        }
        if (Input.pressed(BTN_ID_OK)) {
            noInterrupts();
            attack_packets   = 0;
            last_sec_packets = 0;
            suspicious_packets = 0;
            last_sec_suspicious = 0;
            last_packet_time = millis();
            last_suspicious_time = 0;
            last_suspicious_subtype = 0;
            interrupts();
            total_packets_view = 0;
            sec_packets_view   = 0;
            suspicious_view     = 0;
            sec_suspicious_view = 0;
            current_bar_width  = 0;
            suspicious_windows = 0;
            memset(historyBars, 0, sizeof(historyBars));
            historyIndex = 0;
            attack_confirmed = false;
            last_check_time  = millis();
            runCentinelaSetup();
            channel_fixed = true;
        }
        return;
    }

    unsigned long now = millis();
    updateCounters(now);
    drawMonitor();

    if (Input.pressed(BTN_ID_BACK)) {
        esp_wifi_set_promiscuous(false);
        WiFi.mode(WIFI_OFF);
        channel_fixed = false;
        Input.consume(BTN_ID_BACK);
    }
}
