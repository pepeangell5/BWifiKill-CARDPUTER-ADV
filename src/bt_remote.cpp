#include <Arduino.h>
#include <BleKeyboard.h>
#include <BLEDevice.h>
#include <U8g2lib.h>
#include "bt_remote.h"
#include "ui_theme.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;
extern void runPepeScript(); 

#define PIN_UP 26    
#define PIN_DOWN 33  
#define PIN_OK 32    
#define PIN_BACK 25  
#define PIN_OFF 27   // Botón de Reset de App

static bool isBleRemoteActive = false;
static bool modeSelected = false;
static int sub_menu_index = 0;
static int iphone_menu_index = 0;
static int iphone_scroll = 0;

BleKeyboard bleKeyboard("Wireless Kbd 3000", "Logitech", 100);

struct IphoneAction {
    const char* title;
    uint8_t action;
};

enum IphoneActionId : uint8_t {
    IPHONE_PAIR = 0,
    IPHONE_UNLOCK,
    IPHONE_SAFARI,
    IPHONE_WEB_SEARCH,
    IPHONE_YOUTUBE,
    IPHONE_SPOTIFY,
    IPHONE_WHATSAPP,
    IPHONE_INSTAGRAM,
    IPHONE_PHOTOS,
    IPHONE_HOME,
    IPHONE_APP_SWITCH,
    IPHONE_PLAY_PAUSE,
    IPHONE_NEXT,
    IPHONE_PREV,
    IPHONE_VOL_UP,
    IPHONE_VOL_DOWN,
    IPHONE_MUTE,
    IPHONE_CAMERA,
    IPHONE_CAMERA_PHOTO_MODE,
    IPHONE_CAMERA_VIDEO_MODE,
    IPHONE_SHUTTER,
    IPHONE_BRIGHTNESS_DOWN,
    IPHONE_BRIGHTNESS_UP,
    IPHONE_WHITE_POINT
};

static const IphoneAction IPHONE_ACTIONS[] = {
    { "PAIR STATUS", IPHONE_PAIR },
    { "UNLOCK", IPHONE_UNLOCK },
    { "SAFARI", IPHONE_SAFARI },
    { "WEB SEARCH", IPHONE_WEB_SEARCH },
    { "YOUTUBE", IPHONE_YOUTUBE },
    { "SPOTIFY", IPHONE_SPOTIFY },
    { "WHATSAPP", IPHONE_WHATSAPP },
    { "INSTAGRAM", IPHONE_INSTAGRAM },
    { "PHOTOS", IPHONE_PHOTOS },
    { "HOME", IPHONE_HOME },
    { "APP SWITCH", IPHONE_APP_SWITCH },
    { "PLAY/PAUSE", IPHONE_PLAY_PAUSE },
    { "NEXT", IPHONE_NEXT },
    { "PREVIOUS", IPHONE_PREV },
    { "VOLUME +", IPHONE_VOL_UP },
    { "VOLUME -", IPHONE_VOL_DOWN },
    { "MUTE", IPHONE_MUTE },
    { "OPEN CAMERA", IPHONE_CAMERA },
    { "CAMERA FOTO", IPHONE_CAMERA_PHOTO_MODE },
    { "CAMERA VIDEO", IPHONE_CAMERA_VIDEO_MODE },
    { "CAMERA SHOT", IPHONE_SHUTTER },
    { "BRILLO -", IPHONE_BRIGHTNESS_DOWN },
    { "BRILLO +", IPHONE_BRIGHTNESS_UP },
    { "PUNTO BLANCO", IPHONE_WHITE_POINT }
};

static const uint8_t IPHONE_ACTION_COUNT = sizeof(IPHONE_ACTIONS) / sizeof(IPHONE_ACTIONS[0]);

void btRemoteEnter() {
    modeSelected = false;
    sub_menu_index = 0;
    iphone_menu_index = 0;
    iphone_scroll = 0;
}

void btRemoteExit() {
    modeSelected = false;
}

bool btRemoteBleActive() {
    return isBleRemoteActive;
}

void btRemoteMarkBleReleased() {
    isBleRemoteActive = false;
}

static void drawRemoteOption(int y, const char* label, bool selected) {
    if (selected) {
        u8g2.drawBox(0, y - 8, 128, 11);
        u8g2.setDrawColor(0);
    }

    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(8, y, selected ? ">" : " ");
    u8g2.drawStr(20, y, label);

    if (selected) {
        u8g2.setDrawColor(1);
    }
}

static void drawRemoteMenu(uint8_t frame) {
    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "BT REMOTE", "MODE");

    drawRemoteOption(26, "CONTROL MEDIA", sub_menu_index == 0);
    drawRemoteOption(39, "PAYLOAD", sub_menu_index == 1);
    drawRemoteOption(52, "IPHONE REMOTE", sub_menu_index == 2);

    UiTheme::drawMiniWave(u8g2, 8, 62, frame);
    u8g2.sendBuffer();
}

static void drawMediaGlyph(int x, int y, uint8_t frame) {
    u8g2.drawTriangle(x, y - 5, x, y + 5, x + 8, y);
    u8g2.drawVLine(x + 13, y - 5, 10);
    if ((frame / 8) % 2 == 0) {
        u8g2.drawCircle(x + 28, y, 6, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
        u8g2.drawCircle(x + 28, y, 10, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
    }
}

static void drawConnectedView(uint8_t frame) {
    const char* title = sub_menu_index == 0 ? "MEDIA CTRL" : "PAYLOAD";

    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, title, "LINK");

    u8g2.drawRFrame(7, 20, 114, 34, 4);
    u8g2.drawBox(7, 20, 114, 10);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_5x7_tr);
    UiTheme::drawCenteredText(u8g2, 28, "CONECTADO");
    u8g2.setDrawColor(1);

    if (sub_menu_index == 0) {
        drawMediaGlyph(28, 42, frame);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(69, 43, "READY");
    } else {
        UiTheme::drawSpinner(u8g2, 37, 42, frame);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(55, 44, "READY");
    }

    UiTheme::drawMiniWave(u8g2, 8, 63, frame);
}

static void drawSearchingView(uint8_t frame) {
    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "BT REMOTE", "SCAN");
    UiTheme::drawSpinner(u8g2, 64, 36, frame);
    u8g2.setFont(u8g2_font_6x10_tr);
    UiTheme::drawCenteredText(u8g2, 56, "BUSCANDO PC");
}

static void drawIphoneRemoteView(uint8_t frame) {
    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "IPHONE", bleKeyboard.isConnected() ? "LINK" : "PAIR");

    const int visible = 4;
    if (iphone_menu_index < iphone_scroll) iphone_scroll = iphone_menu_index;
    if (iphone_menu_index >= iphone_scroll + visible) iphone_scroll = iphone_menu_index - visible + 1;

    for (int row = 0; row < visible; row++) {
        int idx = iphone_scroll + row;
        if (idx >= IPHONE_ACTION_COUNT) break;
        int y = 17 + row * 10;
        bool selected = idx == iphone_menu_index;

        if (selected) {
            u8g2.drawBox(2, y - 1, 120, 9);
            u8g2.setDrawColor(0);
        }

        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(7, y + 6, selected ? ">" : " ");
        u8g2.drawStr(16, y + 6, IPHONE_ACTIONS[idx].title);

        if (selected) u8g2.setDrawColor(1);
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(6, 62, bleKeyboard.isConnected() ? "CONECTADO" : "BT: Wireless Kbd 3000");
    UiTheme::drawMiniWave(u8g2, 92, 62, frame);
}

static void showIphoneStatus(const char* title, const char* detail) {
    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "IPHONE", bleKeyboard.isConnected() ? "LINK" : "PAIR");
    u8g2.drawRFrame(8, 21, 112, 30, 4);
    u8g2.setFont(u8g2_font_6x10_tr);
    UiTheme::drawCenteredText(u8g2, 34, title);
    u8g2.setFont(u8g2_font_5x7_tr);
    UiTheme::drawCenteredText(u8g2, 46, detail);
    u8g2.sendBuffer();
}

static void sendShortcut(uint8_t key) {
    if (!bleKeyboard.isConnected()) return;
    bleKeyboard.press(KEY_LEFT_GUI);
    bleKeyboard.press(key);
    delay(80);
    bleKeyboard.releaseAll();
    delay(250);
}

static void writeIphoneChar(char c) {
    if (!bleKeyboard.isConnected()) return;

    if (c == '/') {
        bleKeyboard.press(KEY_LEFT_SHIFT);
        bleKeyboard.write('7');
        bleKeyboard.releaseAll();
        return;
    }

    bleKeyboard.write(c);
}

static void typeIphoneText(const char* text) {
    if (!bleKeyboard.isConnected()) return;
    for (const char* p = text; *p; p++) {
        writeIphoneChar(*p);
        delay(30);
    }
}

static void openIphoneApp(const char* appName) {
    if (!bleKeyboard.isConnected()) {
        showIphoneStatus("SIN CONEXION", "Empareja primero");
        delay(650);
        return;
    }
    showIphoneStatus("ABRIENDO", appName);
    sendShortcut(' ');
    delay(450);
    typeIphoneText(appName);
    delay(180);
    bleKeyboard.write(KEY_RETURN);
    delay(650);
}

static void openIphoneShortcut(const char* shortcutName) {
    if (!bleKeyboard.isConnected()) {
        showIphoneStatus("SIN CONEXION", "Empareja primero");
        delay(650);
        return;
    }
    showIphoneStatus("ATAJO", shortcutName);
    sendShortcut(' ');
    delay(450);
    typeIphoneText(shortcutName);
    delay(180);
    bleKeyboard.write(KEY_RETURN);
    delay(650);
}

static void runWebSearch() {
    if (!bleKeyboard.isConnected()) {
        showIphoneStatus("SIN CONEXION", "Empareja primero");
        delay(650);
        return;
    }
    showIphoneStatus("WEB SEARCH", "Safari");
    openIphoneApp("Safari");
    delay(700);
    sendShortcut('l');
    typeIphoneText("instagram.com/pepeangelll");
    delay(180);
    bleKeyboard.write(KEY_RETURN);
    delay(650);
}

static void drawNumericPad(uint8_t selectedDigit, uint8_t sentCount) {
    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "UNLOCK", "NUM");

    for (uint8_t i = 0; i < 10; i++) {
        uint8_t col = i % 5;
        uint8_t row = i / 5;
        int x = 13 + col * 21;
        int y = 27 + row * 14;
        bool selected = i == selectedDigit;

        if (selected) {
            u8g2.drawBox(x - 4, y - 9, 16, 12);
            u8g2.setDrawColor(0);
        } else {
            u8g2.drawFrame(x - 4, y - 9, 16, 12);
        }

        char digit[2] = { char('0' + i), 0 };
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(x, y, digit);

        if (selected) u8g2.setDrawColor(1);
    }

    char counter[12];
    snprintf(counter, sizeof(counter), "ENVIADOS:%02u", sentCount);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(35, 62, counter);
    u8g2.sendBuffer();
}

static void runNumericUnlock() {
    showIphoneStatus("UNLOCK", "Activando pantalla");
    bleKeyboard.write(' ');
    delay(550);
    bleKeyboard.write(KEY_RETURN);
    delay(700);

    uint8_t selectedDigit = 0;
    uint8_t sentCount = 0;
    drawNumericPad(selectedDigit, sentCount);

    while (true) {
        if (digitalRead(PIN_UP) == LOW) {
            selectedDigit = selectedDigit == 0 ? 9 : selectedDigit - 1;
            drawNumericPad(selectedDigit, sentCount);
            delay(150);
        }

        if (digitalRead(PIN_DOWN) == LOW) {
            selectedDigit = (selectedDigit + 1) % 10;
            drawNumericPad(selectedDigit, sentCount);
            delay(150);
        }

        if (digitalRead(PIN_OK) == LOW) {
            bleKeyboard.write(char('0' + selectedDigit));
            sentCount++;
            drawNumericPad(selectedDigit, sentCount);
            delay(220);
        }

        if (digitalRead(PIN_BACK) == LOW) {
            delay(250);
            return;
        }

        delay(10);
    }
}

static void runIphoneAction(uint8_t action) {
    if (action == IPHONE_PAIR) {
        showIphoneStatus(bleKeyboard.isConnected() ? "CONECTADO" : "EMPAREJAR", "Bluetooth del iPhone");
        delay(900);
        return;
    }

    if (!bleKeyboard.isConnected()) {
        showIphoneStatus("SIN CONEXION", "Empareja primero");
        delay(650);
        return;
    }

    switch (action) {
        case IPHONE_UNLOCK:
            runNumericUnlock();
            break;
        case IPHONE_SAFARI: openIphoneApp("Safari"); break;
        case IPHONE_WEB_SEARCH:
            runWebSearch();
            break;
        case IPHONE_YOUTUBE: openIphoneApp("YouTube"); break;
        case IPHONE_SPOTIFY: openIphoneApp("Spotify"); break;
        case IPHONE_WHATSAPP: openIphoneApp("WhatsApp"); break;
        case IPHONE_INSTAGRAM: openIphoneApp("Instagram"); break;
        case IPHONE_PHOTOS: openIphoneApp("Photos"); break;
        case IPHONE_HOME:
            showIphoneStatus("HOME", "Command + H");
            sendShortcut('h');
            break;
        case IPHONE_APP_SWITCH:
            showIphoneStatus("CAMBIAR APP", "Command + Tab");
            sendShortcut(KEY_TAB);
            break;
        case IPHONE_PLAY_PAUSE:
            showIphoneStatus("MEDIA", "Play/Pause");
            bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
            break;
        case IPHONE_NEXT:
            showIphoneStatus("MEDIA", "Next");
            bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
            break;
        case IPHONE_PREV:
            showIphoneStatus("MEDIA", "Previous");
            bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
            break;
        case IPHONE_VOL_UP:
            showIphoneStatus("VOLUMEN", "Subir");
            bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
            break;
        case IPHONE_VOL_DOWN:
            showIphoneStatus("VOLUMEN", "Bajar");
            bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
            break;
        case IPHONE_MUTE:
            showIphoneStatus("VOLUMEN", "Mute");
            bleKeyboard.write(KEY_MEDIA_MUTE);
            break;
        case IPHONE_CAMERA:
            openIphoneApp("Camera");
            break;
        case IPHONE_CAMERA_PHOTO_MODE:
            openIphoneShortcut("Camera Foto");
            break;
        case IPHONE_CAMERA_VIDEO_MODE:
            openIphoneShortcut("Camera Video");
            break;
        case IPHONE_SHUTTER:
            showIphoneStatus("CAMARA", "Disparo");
            bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
            break;
        case IPHONE_BRIGHTNESS_DOWN:
            openIphoneShortcut("Bajar Brillo");
            break;
        case IPHONE_BRIGHTNESS_UP:
            openIphoneShortcut("Subir Brillo");
            break;
        case IPHONE_WHITE_POINT:
            openIphoneShortcut("Reducir Punto Blanco");
            break;
        default:
            break;
    }
    delay(450);
}

void btRemoteLoop() {
    static uint8_t frame = 0;
    frame++;

    pinMode(PIN_UP, INPUT_PULLUP);
    pinMode(PIN_DOWN, INPUT_PULLUP);
    pinMode(PIN_OK, INPUT_PULLUP);
    pinMode(PIN_BACK, INPUT_PULLUP);
    pinMode(PIN_OFF, INPUT_PULLUP);

    // 1. MENU DE SELECCION
    if (!modeSelected) {
        drawRemoteMenu(frame);

        if (digitalRead(PIN_UP) == LOW) { sub_menu_index = sub_menu_index == 0 ? 2 : sub_menu_index - 1; delay(150); } 
        if (digitalRead(PIN_DOWN) == LOW) { sub_menu_index = (sub_menu_index + 1) % 3; delay(150); } 
        if (digitalRead(PIN_OK) == LOW) { modeSelected = true; delay(300); } 
        if (digitalRead(PIN_BACK) == LOW) { runningApp = false; delay(300); }
        return; 
    }

    // 2. INICIO DE BLUETOOTH
    if (!isBleRemoteActive) {
        bleKeyboard.begin();
        isBleRemoteActive = true;
    }

    // 3. LOGICA DENTRO DEL MODO
    if (bleKeyboard.isConnected()) {
        if (sub_menu_index == 2) {
            drawIphoneRemoteView(frame);

            if (digitalRead(PIN_UP) == LOW) {
                iphone_menu_index = iphone_menu_index == 0 ? IPHONE_ACTION_COUNT - 1 : iphone_menu_index - 1;
                delay(150);
            }
            if (digitalRead(PIN_DOWN) == LOW) {
                iphone_menu_index = (iphone_menu_index + 1) % IPHONE_ACTION_COUNT;
                delay(150);
            }
            if (digitalRead(PIN_OK) == LOW) {
                runIphoneAction(IPHONE_ACTIONS[iphone_menu_index].action);
                delay(160);
            }
            if (digitalRead(PIN_BACK) == LOW) {
                modeSelected = false;
                runningApp = false;
                delay(300);
                return;
            }
            u8g2.sendBuffer();
            return;
        }

        drawConnectedView(frame);
        
        if (sub_menu_index == 0) { // CONTROL MEDIA
            if (digitalRead(PIN_UP) == LOW) { bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN); delay(150); }
            if (digitalRead(PIN_DOWN) == LOW) { bleKeyboard.write(KEY_MEDIA_VOLUME_UP); delay(150); }
            if (digitalRead(PIN_OK) == LOW) { bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE); delay(400); }
        } else { // PAYLOAD
            if (digitalRead(PIN_OK) == LOW) { runPepeScript(); delay(1000); }
        }

        // --- LA CLAVE DEL EXITO ---
        // Si presionas el 27 o el 25 estando conectado, reiniciamos el ESP.
        // Es la UNICA forma que encontré para que el Bluetooth vuelva a funcionar al 100%.
        if (digitalRead(PIN_OFF) == LOW) {
            u8g2.clearBuffer();
            u8g2.drawStr(30, 35, "REINICIANDO...");
            u8g2.sendBuffer();
            delay(500);
            ESP.restart(); 
        }
        if (digitalRead(PIN_BACK) == LOW) {
            modeSelected = false;
            runningApp = false;
            delay(300);
            return;
        }
    } else {
        if (sub_menu_index == 2) {
            drawIphoneRemoteView(frame);

            if (digitalRead(PIN_UP) == LOW) {
                iphone_menu_index = iphone_menu_index == 0 ? IPHONE_ACTION_COUNT - 1 : iphone_menu_index - 1;
                delay(150);
            }
            if (digitalRead(PIN_DOWN) == LOW) {
                iphone_menu_index = (iphone_menu_index + 1) % IPHONE_ACTION_COUNT;
                delay(150);
            }
            if (digitalRead(PIN_OK) == LOW) {
                runIphoneAction(IPHONE_ACTIONS[iphone_menu_index].action);
                delay(160);
            }
            if (digitalRead(PIN_BACK) == LOW) {
                modeSelected = false;
                runningApp = false;
                delay(300);
                return;
            }
            u8g2.sendBuffer();
            return;
        }

        drawSearchingView(frame);
        if (digitalRead(PIN_OFF) == LOW) {
            ESP.restart(); 
        }
        if (digitalRead(PIN_BACK) == LOW) {
            modeSelected = false;
            runningApp = false;
            delay(300);
            return;
        }
    }
    u8g2.sendBuffer();
}
