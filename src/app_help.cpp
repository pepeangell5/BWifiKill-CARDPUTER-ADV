#include "app_help.h"

#include "app_config.h"
#include "input_manager.h"
#include "ui_theme.h"

#ifdef BWK_CARDPUTER_ADV
#include "cardputer_compat.h"
#endif

#include <U8g2lib.h>
#include <string.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern const char* menu_labels[];

struct HelpInfo {
    const char* purpose;
    const char* controls;
    const char* warning;
    bool usesNrf;
    bool dualNrf;
};

static const HelpInfo HELP_TABLE[] = {
    {"Escanea redes WiFi cercanas.", "UP/DOWN lista OK detalle", "Uso educativo y auditoria propia.", false, false},
    {"Radar visual de una red WiFi.", "UP/DOWN objetivo OK scan", "No rastrear redes ajenas.", false, false},
    {"Resume ocupacion por canal.", "UP/DOWN canal OK detalle", "Analisis pasivo solamente.", false, false},
    {"Analizador RF de 2.4 GHz.", "UP/DOWN zoom SPACE modo", "No transmitir sin autorizacion.", true, true},
    {"Busca dispositivos Bluetooth.", "UP/DOWN detalle BACK salir", "Solo inventario cercano.", false, false},
    {"Monitor pasivo de paquetes WiFi.", "UP/DOWN canal SPACE hold", "No capturar datos privados.", false, false},
    {"Detecta deauth/disassoc WiFi.", "UP/DOWN canal OK fijar", "Indicador defensivo.", false, false},
    {"Prueba jammer por canal.", "UP/DOWN canal OK inicio", "Solo laboratorio controlado.", true, false},
    {"Barrido RF amplio.", "OK inicio BACK parar", "Requiere entorno aislado.", true, true},
    {"Prueba interferencia BLE.", "OK inicio BACK parar", "Solo laboratorio controlado.", true, true},
    {"Beacon spam educativo.", "OK inicia BACK sale", "No usar contra terceros.", false, false},
    {"BLE spam demo.", "UP/DOWN modo OK start", "Pruebas en entorno propio.", false, false},
    {"Modo hibrido WiFi/RF.", "OK inicio BACK parar", "Solo laboratorio controlado.", true, true},
    {"Portal cautivo local.", "OK inicia BACK sale", "Demo educativa sin victimas.", true, true},
    {"Escanea IPs de una red propia.", "UP/DOWN host OK accion", "Usar solo en tu red.", false, false},
    {"Control ESP32 esclavo.", "UP/DOWN opcion OK enviar", "Requiere equipo emparejado.", false, false},
    {"Panel web del firmware.", "Conectate al AP local", "No exponer en redes publicas.", false, false},
    {"Control remoto BLE.", "UP/DOWN opcion OK enviar", "Empareja solo tus equipos.", false, false},
    {"Lee logs guardados.", "UP/DOWN pagina BACK salir", "Protege datos sensibles.", false, false},
    {"Menu de juegos.", "UP/DOWN juego OK entrar", "Sin riesgo RF.", false, false},
    {"Informacion del proyecto.", "BACK salir", "Creditos y version.", false, false},
    {"Analisis BLE avanzado.", "UP/DOWN detalle BACK", "Solo observacion cercana.", false, false},
    {"Espectro BLE con nRF.", "UP/DOWN modo BACK", "Lectura pasiva RF.", true, false},
    {"Mapa de calor RF.", "UP/DOWN rango BACK", "Lectura pasiva RF.", true, false},
    {"Sugiere canales limpios.", "UP/DOWN vista BACK", "No transmite.", true, false},
    {"Prueba enlace nRF.", "UP/DOWN campo OK iniciar", "Requiere otro nodo compatible.", true, false},
    {"Chat simple por nRF.", "UP/DOWN campo OK enviar", "Requiere otro nodo compatible.", true, false},
    {"Coexistencia BT/WiFi.", "UP/DOWN vista BACK", "Lectura pasiva RF.", true, false},
    {"Scope para doble nRF.", "UP/DOWN vista BACK", "Requiere 2 nRF conectados.", true, true},
    {"Deauther educativo.", "UP/DOWN AP OK accion", "Solo laboratorio propio.", false, false},
    {"Configura sonido y brillo.", "UP/DOWN opcion OK editar", "Cambios se guardan.", false, false},
    {"Muestra estado de NRF1 y NRF2.", "OK retest BACK salir", "Verifica cableado y pines.", true, false},
};

static const HelpInfo& helpFor(uint8_t appIndex) {
    if (appIndex < sizeof(HELP_TABLE) / sizeof(HELP_TABLE[0])) return HELP_TABLE[appIndex];
    return HELP_TABLE[20];
}

static uint8_t pageCountFor(const HelpInfo& info) {
    return info.usesNrf ? 4 : 3;
}

static void drawWrappedText(const char* text, int y, uint8_t maxLines) {
    u8g2.setFont(u8g2_font_6x10_tr);
    if (!text) return;

    char line[21] = "";
    char word[21];
    uint8_t wordPos = 0;
    uint8_t lines = 0;
    int drawY = y;

    for (const char* p = text; ; ++p) {
        bool delimiter = (*p == ' ' || *p == 0);
        if (!delimiter && wordPos < sizeof(word) - 1) {
            word[wordPos++] = *p;
            continue;
        }

        word[wordPos] = 0;
        if (wordPos > 0) {
            uint8_t lineLen = strlen(line);
            uint8_t wordLen = strlen(word);
            uint8_t extraSpace = lineLen > 0 ? 1 : 0;

            if (lineLen > 0 && lineLen + extraSpace + wordLen > sizeof(line) - 1) {
                u8g2.drawStr(4, drawY, line);
                drawY += 11;
                lines++;
                line[0] = 0;
                if (lines >= maxLines) return;
            }

            if (line[0] != 0) strncat(line, " ", sizeof(line) - strlen(line) - 1);
            strncat(line, word, sizeof(line) - strlen(line) - 1);
            wordPos = 0;
        }

        if (*p == 0) {
            if (line[0] != 0 && lines < maxLines) {
                u8g2.drawStr(4, drawY, line);
            }
            break;
        }
    }
}

static void drawPinsPage(const HelpInfo& info) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(4, 36, "PINES NRF");

    if (info.usesNrf) {
        char pins[32];
        u8g2.setFont(u8g2_font_5x7_tr);
        snprintf(pins, sizeof(pins), "NRF1 CE%u CS%u", AppConfig::NRF1_CE, AppConfig::NRF1_CSN);
        u8g2.drawStr(4, 46, pins);
        snprintf(pins, sizeof(pins), "NRF2 CE%u CS%u", AppConfig::NRF2_CE, AppConfig::NRF2_CSN);
        u8g2.drawStr(4, 54, pins);
        snprintf(pins, sizeof(pins), "SCK%u MI%u MO%u",
                 AppConfig::NRF_SPI_SCK, AppConfig::NRF_SPI_MISO, AppConfig::NRF_SPI_MOSI);
        u8g2.drawStr(4, 62, pins);
    } else {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(4, 48, "Esta funcion");
        u8g2.drawStr(4, 59, "no usa nRF24.");
    }
}

static void drawContentPage(const char* section, const char* text) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(4, 36, section);
    drawWrappedText(text, 48, 3);
}

static void drawHelpPage(uint8_t appIndex, uint8_t page) {
    const HelpInfo& info = helpFor(appIndex);
    uint8_t totalPages = pageCountFor(info);
    if (page >= totalPages) page = 0;

    const char* title = appIndex < 32 ? menu_labels[appIndex] : "AYUDA";
    char status[6];
    snprintf(status, sizeof(status), "%u/%u", page + 1, totalPages);

    u8g2.clearBuffer();
    UiTheme::drawHeader(u8g2, "AYUDA", status);
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(4, 24, title);
    u8g2.drawHLine(4, 27, 120);

    if (page == 0) {
        drawContentPage("QUE HACE", info.purpose);
    } else if (page == 1) {
        drawContentPage("CONTROLES", info.controls);
    } else if (page == 2) {
        drawContentPage("AVISO", info.warning);
    } else {
        drawPinsPage(info);
    }

    u8g2.sendBuffer();
}

static bool helpKeyHeld() {
#ifdef BWK_CARDPUTER_ADV
    return cardputerKeyPressed('h');
#else
    return false;
#endif
}

void showAppHelp(uint8_t appIndex) {
    uint8_t page = 0;
    bool previousHelp = helpKeyHeld();
    Input.resetAll();
    drawHelpPage(appIndex, page);

    while (true) {
        Input.update();
        bool helpHeld = helpKeyHeld();

        if (Input.pressed(BTN_ID_BACK) || Input.pressed(BTN_ID_OK)) {
            Input.consume(BTN_ID_BACK);
            Input.consume(BTN_ID_OK);
            break;
        }

        if (Input.pressed(BTN_ID_DOWN) || Input.pressed(BTN_ID_UP) ||
            Input.pressed(BTN_ID_AUX) ||
            (helpHeld && !previousHelp)) {
            page = (page + 1) % pageCountFor(helpFor(appIndex));
            drawHelpPage(appIndex, page);
            Input.consume(BTN_ID_DOWN);
            Input.consume(BTN_ID_UP);
            Input.consume(BTN_ID_AUX);
            delay(120);
        }

        previousHelp = helpHeld;
        delay(15);
    }

#ifdef BWK_CARDPUTER_ADV
    cardputerWaitForKeysReleased();
#endif
    Input.resetAll();
}
