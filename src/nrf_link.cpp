#include "nrf_link.h"
#include "input_manager.h"
#include "ui_theme.h"
#include <RF24.h>
#include <U8g2lib.h>
#include <WiFi.h>

extern RF24 jam1;
extern RF24 jam2;
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static const uint16_t MASTER_SEND_MS = 550;
static const uint16_t MASTER_WAIT_MS = 80;
static const uint16_t DRAW_INTERVAL_MS = 120;
static const uint16_t SLAVE_REPLY_DELAY_US = 250;
static const uint8_t LINK_CHANNELS[] = {2, 26, 40, 60, 76};

static const uint8_t ADDR_MASTER[6] = {'B', 'W', 'F', 'M', '1', 0};
static const uint8_t ADDR_SLAVE[6]  = {'B', 'W', 'F', 'S', '1', 0};

enum LinkScreen : uint8_t {
    LINK_ROLE_SELECT = 0,
    LINK_MASTER,
    LINK_SLAVE
};

struct LinkPacket {
    uint8_t magic;
    uint8_t type;
    uint16_t seq;
    uint32_t uptime;
};

struct LinkReply {
    uint8_t magic;
    uint8_t type;
    uint16_t seq;
    uint32_t slaveUptime;
};

static LinkScreen screen = LINK_ROLE_SELECT;
static uint8_t roleSelection = 0;
static uint8_t fieldSelection = 0;
static uint8_t radioSelection = 1;
static uint8_t channelSelection = 4;
static bool radioReady = false;
static bool masterWaiting = false;
static uint16_t txSeq = 0;
static uint16_t lastReplySeq = 0;
static uint16_t lastRxSeq = 0;
static uint32_t sentCount = 0;
static uint32_t okCount = 0;
static uint32_t rxCount = 0;
static uint32_t replyCount = 0;
static uint32_t lastSendMs = 0;
static uint32_t waitStartMs = 0;
static uint32_t lastLatencyMs = 0;
static uint32_t bestLatencyMs = 0;
static uint32_t lastRxMs = 0;
static bool radioBeginOk = false;
static bool lastWriteOk = false;
static uint32_t lastDrawMs = 0;
static uint8_t frameTick = 0;

static RF24& linkRadio() {
    return radioSelection == 0 ? jam1 : jam2;
}

static RF24& idleRadio() {
    return radioSelection == 0 ? jam2 : jam1;
}

static uint8_t selectedChannel() {
    return LINK_CHANNELS[channelSelection];
}

static void resetStats() {
    masterWaiting = false;
    txSeq = 0;
    lastReplySeq = 0;
    lastRxSeq = 0;
    sentCount = 0;
    okCount = 0;
    rxCount = 0;
    replyCount = 0;
    lastSendMs = 0;
    waitStartMs = 0;
    lastLatencyMs = 0;
    bestLatencyMs = 0;
    lastRxMs = 0;
    lastWriteOk = false;
}

static void configureBaseRadio() {
    WiFi.mode(WIFI_OFF);
    jam1.stopConstCarrier();
    jam2.stopConstCarrier();
    jam1.stopListening();
    jam2.stopListening();

    RF24& radio = linkRadio();
    RF24& idle = idleRadio();
    idle.powerDown();

    radioBeginOk = radio.begin();
    radio.powerDown();
    delay(5);
    radio.powerUp();
    radio.setAddressWidth(5);
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.setPayloadSize(sizeof(LinkPacket));
    radio.setChannel(selectedChannel());
    radio.setDataRate(RF24_250KBPS);
    radio.setPALevel(RF24_PA_HIGH);
    radio.setCRCLength(RF24_CRC_16);
    radio.flush_rx();
    radio.flush_tx();
}

static void startMasterRadio() {
    configureBaseRadio();
    RF24& radio = linkRadio();
    radio.stopListening();
    radio.openWritingPipe(ADDR_SLAVE);
    radio.openReadingPipe(1, ADDR_MASTER);
    radioReady = true;
}

static void startSlaveRadio() {
    configureBaseRadio();
    RF24& radio = linkRadio();
    radio.openWritingPipe(ADDR_MASTER);
    radio.openReadingPipe(1, ADDR_SLAVE);
    radio.startListening();
    radioReady = true;
}

static void drawRoleSelect() {
    UiTheme::drawHeader(u8g2, "NRF LINK", "SET");

    u8g2.setFont(u8g2_font_5x7_tr);
    const char* role = roleSelection == 0 ? "MAESTRO" : "ESCLAVO";
    char radio[8];
    char channel[8];
    snprintf(radio, sizeof(radio), "NRF%u", radioSelection + 1);
    snprintf(channel, sizeof(channel), "CH%u", selectedChannel());

    const char* labels[] = {"ROL", "RADIO", "CANAL"};
    const char* values[] = {role, radio, channel};

    for (uint8_t i = 0; i < 3; i++) {
        int y = 25 + i * 12;
        int x = 8;
        int w = 112;
        if (fieldSelection == i) {
            u8g2.drawBox(x, y - 8, w, 10);
            u8g2.setDrawColor(0);
        }
        u8g2.drawStr(x + 4, y, labels[i]);
        u8g2.drawStr(x + 51, y, values[i]);
        u8g2.setDrawColor(1);
    }

    u8g2.drawHLine(10, 59, 108);
    uint8_t markerX = 22 + fieldSelection * 42;
    u8g2.drawBox(markerX, 57, 8, 4);
}

static uint8_t linkQuality() {
    if (sentCount == 0) return 0;
    return constrain((okCount * 100UL) / sentCount, 0UL, 100UL);
}

static bool shouldDrawNow() {
    uint32_t now = millis();
    if (lastDrawMs == 0 || now - lastDrawMs >= DRAW_INTERVAL_MS) {
        lastDrawMs = now;
        return true;
    }
    return false;
}

static void drawLinkWave(int x, int y, bool active) {
    u8g2.drawCircle(x, y, 2);
    if (!active) {
        u8g2.drawPixel(x + 8, y);
        u8g2.drawPixel(x - 8, y);
        return;
    }

    uint8_t phase = (frameTick / 3) % 4;
    for (uint8_t i = 0; i <= phase; i++) {
        uint8_t r = 5 + i * 4;
        u8g2.drawCircle(x, y, r, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
        u8g2.drawCircle(x, y, r, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
    }
}

static void drawMaster() {
    char status[12];
    snprintf(status, sizeof(status), radioBeginOk ? "%u%% R%u" : "RF ERR", linkQuality(), radioSelection + 1);
    UiTheme::drawHeader(u8g2, "NRF MASTER", status);

    u8g2.setFont(u8g2_font_5x7_tr);
    char line[26];
    snprintf(line, sizeof(line), "TX %lu OK %lu", sentCount, okCount);
    u8g2.drawStr(3, 24, line);

    uint32_t lost = sentCount > okCount ? sentCount - okCount : 0;
    snprintf(line, sizeof(line), "LOST %lu SEQ %u", lost, txSeq);
    u8g2.drawStr(3, 34, line);

    if (okCount > 0) {
        snprintf(line, sizeof(line), "LAT %lums BEST %lums", lastLatencyMs, bestLatencyMs);
    } else {
        snprintf(line, sizeof(line), lastWriteOk ? "CH%u ESPERANDO" : "CH%u SIN TX", selectedChannel());
    }
    u8g2.drawStr(3, 44, line);

    drawLinkWave(64, 57, masterWaiting || (millis() - lastRxMs < 500));
}

static void drawSlave() {
    char status[12];
    uint32_t age = lastRxMs == 0 ? 9999 : (millis() - lastRxMs);
    if (radioBeginOk) {
        snprintf(status, sizeof(status), "%s R%u", age < 1000 ? "ON" : "WAIT", radioSelection + 1);
    } else {
        snprintf(status, sizeof(status), "RF ERR");
    }
    UiTheme::drawHeader(u8g2, "NRF SLAVE", status);

    u8g2.setFont(u8g2_font_5x7_tr);
    char line[26];
    snprintf(line, sizeof(line), "RX %lu ACK %lu", rxCount, replyCount);
    u8g2.drawStr(3, 24, line);

    snprintf(line, sizeof(line), "SEQ %u CH %u", lastRxSeq, selectedChannel());
    u8g2.drawStr(3, 34, line);

    snprintf(line, sizeof(line), "AGE %lums", age == 9999 ? 0 : age);
    u8g2.drawStr(3, 44, line);

    drawLinkWave(64, 57, age < 600);
}

static void sendMasterPing() {
    RF24& radio = linkRadio();
    LinkPacket pkt;
    pkt.magic = 0xB4;
    pkt.type = 1;
    pkt.seq = ++txSeq;
    pkt.uptime = millis();

    radio.stopListening();
    radio.flush_tx();
    radio.openWritingPipe(ADDR_SLAVE);
    lastWriteOk = radio.write(&pkt, sizeof(pkt));
    sentCount++;
    waitStartMs = millis();
    masterWaiting = true;
    radio.openReadingPipe(1, ADDR_MASTER);
    radio.flush_rx();
    radio.startListening();

    uint32_t deadline = waitStartMs + MASTER_WAIT_MS;
    while ((int32_t)(millis() - deadline) < 0) {
        if (!radio.available()) {
            delayMicroseconds(150);
            continue;
        }

        LinkReply reply;
        radio.read(&reply, sizeof(reply));
        if (reply.magic == 0xB4 && reply.type == 2 &&
            (reply.seq == txSeq || reply.seq != lastReplySeq)) {
            okCount++;
            lastReplySeq = reply.seq;
            uint32_t now = millis();
            lastLatencyMs = now - waitStartMs;
            if (bestLatencyMs == 0 || lastLatencyMs < bestLatencyMs) {
                bestLatencyMs = lastLatencyMs;
            }
            lastRxMs = now;
            masterWaiting = false;
            radio.stopListening();
            return;
        }
    }

    masterWaiting = false;
    radio.stopListening();
}

static void serviceMaster() {
    RF24& radio = linkRadio();
    uint32_t now = millis();
    if (!masterWaiting && now - lastSendMs >= MASTER_SEND_MS) {
        lastSendMs = now;
        sendMasterPing();
        return;
    }

    if (masterWaiting && radio.available()) {
        LinkReply reply;
        radio.read(&reply, sizeof(reply));
        if (reply.magic == 0xB4 && reply.type == 2) {
            if (reply.seq == txSeq || reply.seq != lastReplySeq) {
                okCount++;
                lastReplySeq = reply.seq;
                lastLatencyMs = now - waitStartMs;
                if (bestLatencyMs == 0 || lastLatencyMs < bestLatencyMs) {
                    bestLatencyMs = lastLatencyMs;
                }
                lastRxMs = now;
            }
        }
        masterWaiting = false;
        radio.stopListening();
    }

    if (masterWaiting && now - waitStartMs > MASTER_WAIT_MS) {
        masterWaiting = false;
        radio.stopListening();
    }
}

static void serviceSlave() {
    RF24& radio = linkRadio();
    if (!radio.available()) return;

    LinkPacket pkt;
    radio.read(&pkt, sizeof(pkt));
    if (pkt.magic != 0xB4 || pkt.type != 1) return;

    rxCount++;
    lastRxSeq = pkt.seq;
    lastRxMs = millis();

    LinkReply reply;
    reply.magic = 0xB4;
    reply.type = 2;
    reply.seq = pkt.seq;
    reply.slaveUptime = millis();

    delayMicroseconds(SLAVE_REPLY_DELAY_US);
    radio.stopListening();
    radio.flush_tx();
    radio.openWritingPipe(ADDR_MASTER);
    if (radio.write(&reply, sizeof(reply))) {
        replyCount++;
    }
    radio.openReadingPipe(1, ADDR_SLAVE);
    radio.flush_rx();
    radio.startListening();
}

static void serviceSlaveBurst() {
    uint32_t start = millis();
    do {
        serviceSlave();
    } while (millis() - start < 4 && linkRadio().available());
}

void nrfLinkEnter() {
    screen = LINK_ROLE_SELECT;
    roleSelection = 0;
    fieldSelection = 0;
    radioSelection = 1;
    channelSelection = 4;
    radioReady = false;
    frameTick = 0;
    radioBeginOk = false;
    lastDrawMs = 0;
    resetStats();
    Input.resetAll();
}

void nrfLinkExit() {
    jam1.stopListening();
    jam2.stopListening();
    jam1.powerDown();
    jam2.powerDown();
    radioReady = false;
    screen = LINK_ROLE_SELECT;
    resetStats();
}

void nrfLinkLoop() {
    frameTick++;

    if (screen == LINK_ROLE_SELECT) {
        if (Input.repeating(BTN_ID_UP)) {
            if (fieldSelection == 0) {
                roleSelection = roleSelection == 0 ? 1 : 0;
            } else if (fieldSelection == 1) {
                radioSelection = radioSelection == 0 ? 1 : 0;
            } else {
                channelSelection = (channelSelection + 1) % (sizeof(LINK_CHANNELS) / sizeof(LINK_CHANNELS[0]));
            }
        }
        if (Input.repeating(BTN_ID_DOWN)) {
            if (fieldSelection == 0) {
                roleSelection = roleSelection == 0 ? 1 : 0;
            } else if (fieldSelection == 1) {
                radioSelection = radioSelection == 0 ? 1 : 0;
            } else {
                uint8_t count = sizeof(LINK_CHANNELS) / sizeof(LINK_CHANNELS[0]);
                channelSelection = (channelSelection + count - 1) % count;
            }
        }
        if (Input.pressed(BTN_ID_AUX)) {
            fieldSelection = (fieldSelection + 1) % 3;
            Input.consume(BTN_ID_AUX);
        }
        if (Input.pressed(BTN_ID_OK)) {
            resetStats();
            lastDrawMs = 0;
            if (roleSelection == 0) {
                startMasterRadio();
                screen = LINK_MASTER;
            } else {
                startSlaveRadio();
                screen = LINK_SLAVE;
            }
            Input.consume(BTN_ID_OK);
        }

        u8g2.clearBuffer();
        drawRoleSelect();
        u8g2.sendBuffer();
        return;
    }

    if (Input.pressed(BTN_ID_OK)) {
        resetStats();
        lastDrawMs = 0;
        Input.consume(BTN_ID_OK);
    }

    if (!radioReady) {
        screen == LINK_MASTER ? startMasterRadio() : startSlaveRadio();
    }

    if (screen == LINK_MASTER) {
        serviceMaster();
        if (shouldDrawNow()) {
            u8g2.clearBuffer();
            drawMaster();
            u8g2.sendBuffer();
        }
    } else {
        serviceSlaveBurst();
        if (shouldDrawNow()) {
            u8g2.clearBuffer();
            drawSlave();
            u8g2.sendBuffer();
        }
    }
}
