#include "packet_monitor.h"
#include "ui_theme.h"
#include "app_config.h"
#include "input_manager.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern int target_channel;

struct PktData {
    uint8_t total;
    uint8_t data;
};

// --- Estado del módulo ---
static PktData history[128];
static volatile uint32_t count_all  = 0;
static volatile uint32_t count_data = 0;
static unsigned long lastPktUpdate = 0;
static unsigned long lastHopTime   = 0;
static bool autoHop = true;
static uint32_t lastTotalPackets = 0;
static uint32_t lastDataPackets  = 0;

// Flujo horizontal de paquetes: cada paquete contado en la ventana
// de 100 ms genera un punto que vuela de derecha a izquierda.
struct PacketDot {
    float   x;
    uint8_t y;
    uint8_t type;   // 0 = mgmt/other, 1 = data
    bool    active;
};
static const int MAX_DOTS = 40;
static PacketDot dots[MAX_DOTS];

void sniffer_callback(void* buf, wifi_promiscuous_pkt_type_t type) {
    count_all++;
    if (type == WIFI_PKT_DATA) count_data++;
}

static void clearHistory() {
    for (int i = 0; i < 128; i++) {
        history[i].total = 0;
        history[i].data  = 0;
    }
}

static void clearDots() {
    for (int i = 0; i < MAX_DOTS; i++) dots[i].active = false;
}

static void resetCounters() {
    noInterrupts();
    count_all  = 0;
    count_data = 0;
    interrupts();
    lastTotalPackets = 0;
    lastDataPackets  = 0;
}

static void snapshotCounters(uint32_t& totalPackets, uint32_t& dataPackets) {
    noInterrupts();
    totalPackets = count_all;
    dataPackets  = count_data;
    count_all    = 0;
    count_data   = 0;
    interrupts();
}

// Por cada paquete contado en la última ventana, lanza un punto a la lane.
static void spawnDots(uint32_t totalPackets, uint32_t dataPackets) {
    int spawnTotal = (totalPackets > 8) ? 8 : (int)totalPackets;
    int spawnData  = (dataPackets  > 8) ? 8 : (int)dataPackets;
    if (spawnData > spawnTotal) spawnData = spawnTotal;

    int spawned = 0;
    for (int j = 0; j < MAX_DOTS && spawned < spawnTotal; j++) {
        if (!dots[j].active) {
            dots[j].active = true;
            dots[j].x      = 127;
            dots[j].y      = 28 + ((spawned * 5) % 16);   // distribución vertical
            dots[j].type   = (spawned < spawnData) ? 1 : 0;
            spawned++;
        }
    }
}

// Actualiza posiciones y dibuja los puntos del stream
static void updateAndDrawDots() {
    for (int j = 0; j < MAX_DOTS; j++) {
        if (!dots[j].active) continue;
        if (dots[j].type == 1) {
            u8g2.drawBox((int)dots[j].x, dots[j].y, 2, 2);    // data: bloque 2×2
        } else {
            u8g2.drawPixel((int)dots[j].x, dots[j].y);         // otros: 1 pixel
        }
        dots[j].x -= 2.0f;
        if (dots[j].x < 0) dots[j].active = false;
    }
}

static void drawHistoryBars() {
    // Barras compactas al pie (y=48-63), altura máx 15 px
    for (int i = 0; i < 128; i++) {
        if (history[i].total > 0) {
            uint8_t h = history[i].total;
            if (h > 15) h = 15;
            u8g2.drawVLine(i, 63 - h, h);
        }
    }
}

static void drawStatsLine() {
    char buf[24];
    u8g2.setFont(u8g2_font_5x7_tr);

    u8g2.setCursor(2, 23);
    u8g2.print(autoHop ? "AUTO" : "MAN");

    snprintf(buf, sizeof(buf), "ALL %lu", lastTotalPackets);
    u8g2.setCursor(32, 23);
    u8g2.print(buf);

    snprintf(buf, sizeof(buf), "DATA %lu", lastDataPackets);
    u8g2.setCursor(82, 23);
    u8g2.print(buf);

    u8g2.drawHLine(0, 25, 128);
}

// =============================================================
// Ciclo de vida
// =============================================================

void monitorSetup() {
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&sniffer_callback);
    esp_wifi_set_channel(target_channel, WIFI_SECOND_CHAN_NONE);

    clearHistory();
    clearDots();
    resetCounters();
    lastPktUpdate = millis();
    lastHopTime   = millis();
}

void monitorEnter() { monitorSetup(); }

void monitorExit() {
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    clearHistory();
    clearDots();
    resetCounters();
}

void monitorLoop() {
    // --- Auto-hop o cambio manual ---
    if (autoHop) {
        if (millis() - lastHopTime > 1500) {
            lastHopTime = millis();
            target_channel++;
            if (target_channel > 13) target_channel = 1;
            esp_wifi_set_channel(target_channel, WIFI_SECOND_CHAN_NONE);
        }
    } else if (Input.pressed(BTN_ID_DOWN)) {
        target_channel++;
        if (target_channel > 13) target_channel = 1;
        esp_wifi_set_channel(target_channel, WIFI_SECOND_CHAN_NONE);
        resetCounters();
    }

    // OK = toggle auto/manual
    if (Input.pressed(BTN_ID_OK)) {
        autoHop = !autoHop;
    }

    // --- Ventana de 100 ms: snapshot de contadores y spawn de puntos ---
    if (millis() - lastPktUpdate > 100) {
        lastPktUpdate = millis();

        uint32_t totalPackets = 0;
        uint32_t dataPackets  = 0;
        snapshotCounters(totalPackets, dataPackets);
        lastTotalPackets = totalPackets;
        lastDataPackets  = dataPackets;

        // Spawn flujo
        spawnDots(totalPackets, dataPackets);

        // Historial: shift y agregar nueva entrada al final
        for (int i = 0; i < 127; i++) history[i] = history[i + 1];
        history[127].total = map(constrain(totalPackets, 0UL, 15UL), 0, 15, 0, 15);
        history[127].data  = map(constrain(dataPackets,  0UL, 10UL), 0, 10, 0, 15);
    }

    // --- Dibujo ---
    u8g2.clearBuffer();

    // Header
    char status[8];
    snprintf(status, sizeof(status), "CH%02d", target_channel);
    UiTheme::drawHeader(u8g2, "PACKET MON", status);

    // Línea de stats
    drawStatsLine();

    // Lane de flujo (y=28-44)
    updateAndDrawDots();

    // Separador
    u8g2.drawHLine(0, 46, 128);

    // Barras de historial al pie
    drawHistoryBars();

    u8g2.sendBuffer();
}