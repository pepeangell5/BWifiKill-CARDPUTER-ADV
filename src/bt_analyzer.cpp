// bt_analyzer.cpp  -  BLE scanner/analyzer view (revision con mejoras)
//
// Cambios respecto a la version anterior:
//
// Funcionamiento:
//   - Mutex (portMUX) entre BLE task y main loop. Antes habia race condition
//     directa sobre devices[] y deviceCount.
//   - Eviction LRU (lastSeen mas antiguo) protegiendo siempre al seleccionado.
//   - Slots reusados se limpian *antes* de aceptar al nuevo device (bug:
//     antes el nuevo heredaba min/max/avg/history del anterior).
//   - Purga automatica de entradas stale > 60 s (no las conserva eternamente).
//   - Snapshots de devices para renderizar sin mantener el mutex (no se
//     bloquea la BLE task durante los ~5 ms de envio I2C al OLED).
//   - Seleccion atada a la direccion MAC (sobrevive a sort y a purga).
//   - Modos de orden: descubrimiento, RSSI desc, mas reciente (sortMode).
//   - Tipos de direccion RPA-PUB / RPA-RND mapeados.
//   - UUIDs de servicio 16-bit mapeados a nombres legibles.
//   - Heuristica de fabricante con boundaries de palabra (evita falsos
//     positivos como "Espresso" -> Espressif o "Amir" -> Xiaomi).
//   - Camino del callback BLE migrado de Arduino String a char[] para
//     reducir fragmentacion de heap.
//   - Promedio RSSI con redondeo correcto para numeros negativos.
//   - pps en uint16_t (antes saturaba a 255).
//   - Guard de "exiting" en callbacks para evitar crash en btAnalyzerExit.
//   - Gap entre scans reducido a 1050 ms (antes 1200 ms).
//
// Animaciones del grafico RSSI:
//   - Scroll temporal continuo: el grafico avanza con el tiempo, no con
//     cada paquete recibido. La curva se desliza siempre hacia la izquierda.
//   - Disco "latido" en el ultimo sample (radio decae 3->1 px en 400 ms).
//   - Signal lost visual: cuando los samples salen de la ventana temporal
//     la grafica se vacia sola y aparece tag "OLD".
//   - Linea EMA suavizada como marca discreta a la derecha del grafico.
//   - Min/Max dibujados como ticks en el borde izquierdo.
//   - Linea de promedio con patron 3-on/3-off (distinto del grid).
//   - Labels verticales de escala (-40, -70, -99 dBm) en font 4x6.
//   - Samples no inicializados se omiten (antes aparecian como linea en -100).
//   - Transicion "wipe" de ~180 ms entre vistas (list -> detail -> graph).
//   - Pulso en la lista cuando llega paquete fresco (puntito a la derecha).
//   - Animacion "buscando" tipo onda gaussiana que barre cuando no hay
//     devices, reemplazando al spinner solo.
//   - Indicador de scan activo (punto parpadeando) en el header.

#include "bt_analyzer.h"

#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <U8g2lib.h>
#include <math.h>
#include <string>
#include <string.h>
#include <ctype.h>

#include "app_config.h"
#include "bt_remote.h"
#include "input_manager.h"
#include "ui_theme.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

// ============================================================================
// Constantes
// ============================================================================

static const uint8_t  MAX_ANALYZER_DEVICES = 24;
static const uint8_t  RSSI_HISTORY_LEN     = 40;
static const uint16_t SCAN_RESTART_MS      = 1050;   // hueco entre scans mas corto
static const uint16_t STALE_MS             = 6000;   // dimea las barras
static const uint32_t PURGE_MS             = 60000;  // elimina del array
static const uint32_t GRAPH_WINDOW_MS      = 12000;  // ventana temporal visible
static const uint32_t PULSE_MS             = 400;    // latido del disco
static const uint32_t WIPE_MS              = 180;    // transicion entre vistas
static const uint32_t BAR_BLINK_MS         = 200;    // pulso de paquete fresco
static const float    EMA_ALPHA            = 0.25f;

static const int RSSI_GRAPH_TOP = -40;
static const int RSSI_GRAPH_BOT = -100;

static const uint8_t ADV_TYPE_UNKNOWN = 0xFF;
static const int8_t  RSSI_UNSET       = 127;   // slot de history no inicializado

enum SortMode : uint8_t {
    SORT_DISCOVERY = 0,
    SORT_RSSI_DESC,
    SORT_RECENT,
    SORT_MODE_COUNT
};

static const char* sortLabel(SortMode m) {
    switch (m) {
        case SORT_DISCOVERY: return "DISC";
        case SORT_RSSI_DESC: return "RSSI";
        case SORT_RECENT:    return "LAST";
        default:             return "?";
    }
}

// ============================================================================
// Struct del device y estado compartido
// ============================================================================

struct AnalyzerDevice {
    bool     used;
    char     name[18];
    char     addr[18];
    char     maker[16];
    char     type[14];
    char     service[14];
    char     connectable[8];
    char     addrType[8];
    uint16_t companyId;
    int      rssi;
    int      minRssi;
    int      maxRssi;
    int      avgSum;
    uint16_t avgCount;
    uint16_t totalPackets;
    uint16_t secondPackets;
    uint16_t pps;
    float    ema;
    bool     hasEma;
    uint8_t  advType;
    bool     hasTxPower;
    int8_t   txPower;
    uint32_t firstSeen;
    uint32_t lastSeen;
    int8_t   history[RSSI_HISTORY_LEN];
    uint32_t historyTime[RSSI_HISTORY_LEN];
    uint8_t  historyPos;
};

// Compartido entre BLE task y main loop
static AnalyzerDevice  devices[MAX_ANALYZER_DEVICES];
static volatile uint8_t deviceCount = 0;
static portMUX_TYPE    deviceMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool   exitingAnalyzer = false;

// Solo accedido desde main loop
static BLEScan* analyzerScan   = nullptr;
static bool     scanActive     = false;
static uint32_t scanStartedAt  = 0;
static uint32_t lastRateTick   = 0;
static uint32_t lastPurgeTick  = 0;

static char     selectedAddr[18] = "";
static int      detailScroll    = 0;
static bool     detailView      = false;
static bool     graphView       = false;
static SortMode sortMode        = SORT_DISCOVERY;
static uint32_t viewEnterTime   = 0;

// ============================================================================
// Tablas de lookup
// ============================================================================

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
        case 0x0087: return "Garmin";
        case 0x0157: return "Huami";
        case 0x0499: return "Ruuvi";
        case 0x00C4: return "LGE";
        case 0x008A: return "Bose";
        default:     return nullptr;
    }
}

// Nombre legible de un UUID de servicio 16-bit estandar.
static const char* knownServiceName16(uint16_t uuid16) {
    switch (uuid16) {
        case 0x180F: return "Battery";
        case 0x180D: return "HeartRate";
        case 0x1812: return "HID";
        case 0x1800: return "GenAccess";
        case 0x1801: return "GenAttr";
        case 0x180A: return "DevInfo";
        case 0x1802: return "ImmAlert";
        case 0x1803: return "LinkLoss";
        case 0x1804: return "TxPower";
        case 0x1819: return "LocNav";
        case 0x181A: return "EnvSens";
        case 0x181C: return "UserData";
        case 0x181D: return "WeightScl";
        case 0xFEAA: return "Eddystone";
        case 0xFE9F: return "GoogleFM";
        case 0xFD6F: return "ExpNotif";
        case 0xFE50: return "GoogleCnt";
        case 0xFEED: return "Tile";
        case 0xFD3D: return "MS-CDP";
        default:     return nullptr;
    }
}

// ============================================================================
// Helpers de texto
// ============================================================================

static void copyCStr(char* dst, size_t len, const char* src) {
    if (len == 0) return;
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, len - 1);
    dst[len - 1] = 0;
}

// Match con boundaries de palabra (espacio, signo o borde de string).
// Evita que "Despertador" matchee "esp" o "Amir" matchee "mi".
static bool containsToken(const char* haystack, const char* token) {
    size_t tlen = strlen(token);
    if (tlen == 0) return false;
    const char* p = haystack;
    while (*p) {
        const char* m = strstr(p, token);
        if (!m) return false;
        bool boundaryLeft  = (m == haystack) || !isalnum((unsigned char)m[-1]);
        bool boundaryRight = !isalnum((unsigned char)m[tlen]);
        if (boundaryLeft && boundaryRight) return true;
        p = m + 1;
    }
    return false;
}

static const char* inferMakerFromNameC(const char* name) {
    if (!name || !name[0]) return "";
    char lower[40];
    size_t i = 0;
    for (; name[i] && i < sizeof(lower) - 1; i++) {
        lower[i] = (char)tolower((unsigned char)name[i]);
    }
    lower[i] = 0;
    if (containsToken(lower, "airpods") || containsToken(lower, "iphone") ||
        containsToken(lower, "ipad")    || containsToken(lower, "macbook") ||
        containsToken(lower, "homepod") || containsToken(lower, "imac"))   return "Apple";
    if (containsToken(lower, "surface") || containsToken(lower, "xbox") ||
        containsToken(lower, "microsoft")) return "Microsoft";
    if (containsToken(lower, "samsung") || containsToken(lower, "galaxy") ||
        containsToken(lower, "buds"))      return "Samsung";
    if (containsToken(lower, "xiaomi")  || containsToken(lower, "redmi") ||
        containsToken(lower, "poco")    || containsToken(lower, "mi"))     return "Xiaomi";
    if (containsToken(lower, "esp32")   || containsToken(lower, "espressif")) return "Espressif";
    if (containsToken(lower, "garmin")  || containsToken(lower, "fenix"))  return "Garmin";
    if (containsToken(lower, "fitbit")) return "Fitbit";
    if (containsToken(lower, "google")  || containsToken(lower, "nest") ||
        containsToken(lower, "pixel"))     return "Google";
    return "";
}

static uint8_t rssiBars(int rssi) {
    if (rssi > -55) return 4;
    if (rssi > -70) return 3;
    if (rssi > -85) return 2;
    return 1;
}

// ============================================================================
// Deteccion SFINAE de getAdvType() (igual que la version original)
// ============================================================================

template<typename T>
static auto tryGetAdvType(T& dev, int) -> decltype((uint8_t)dev.getAdvType()) {
    return (uint8_t)dev.getAdvType();
}
template<typename T>
static uint8_t tryGetAdvType(T&, long) {
    return ADV_TYPE_UNKNOWN;
}

static const char* connectableLabel(uint8_t advType) {
    switch (advType) {
        case ESP_BLE_EVT_CONN_ADV:     return "SI";
        case ESP_BLE_EVT_CONN_DIR_ADV: return "DIR";
        case ESP_BLE_EVT_DISC_ADV:     return "SCAN";
        case ESP_BLE_EVT_NON_CONN_ADV: return "NO";
        default:                       return "?";
    }
}

static const char* inferConnectableHeuristic(BLEAdvertisedDevice& dev,
                                              uint16_t companyId,
                                              const std::string& mfgData) {
    if (companyId == 0x004C && mfgData.length() >= 3) {
        uint8_t apt = (uint8_t)mfgData[2];
        if (apt == 0x02 || apt == 0x05 || apt == 0x07 || apt == 0x09 ||
            apt == 0x0A || apt == 0x0C || apt == 0x10 || apt == 0x12) {
            return "NO";
        }
    }
    if (companyId == 0x0006 && mfgData.length() >= 3) {
        if ((uint8_t)mfgData[2] == 0x03) return "SI";
    }
    if (dev.haveServiceUUID()) return "SI";
    if (dev.haveManufacturerData()) return "NO";
    return "?";
}

static float estimateDistance(int rssi, int8_t txPower, bool hasTxPower) {
    if (rssi >= 0) return -1.0f;
    int refPower = hasTxPower ? (int)txPower : -59;
    float ratio = (float)(refPower - rssi) / 25.0f;
    return powf(10.0f, ratio);
}

// Si el UUID es un 16-bit standard ("0000XXXX-0000-1000-8000-00805f9b34fb")
// muestra el nombre canonico; si no, los primeros chars del UUID raw.
static void buildServiceLabel(char* out, size_t len, BLEAdvertisedDevice& dev) {
    if (!dev.haveServiceUUID()) { if (len) out[0] = 0; return; }
    std::string uuid = dev.getServiceUUID().toString();
    if (uuid.length() == 36 &&
        uuid.compare(0, 4, "0000") == 0 &&
        uuid.compare(8, 28, "-0000-1000-8000-00805f9b34fb") == 0) {
        unsigned int u16 = 0;
        if (sscanf(uuid.c_str() + 4, "%4x", &u16) == 1) {
            const char* nm = knownServiceName16((uint16_t)u16);
            if (nm) { snprintf(out, len, "%s", nm); return; }
            snprintf(out, len, "0x%04X", u16);
            return;
        }
    }
    copyCStr(out, len, uuid.c_str());
}

// ============================================================================
// Manejo de slots
// ============================================================================

static void clearDeviceSlot(AnalyzerDevice& d) {
    memset(&d, 0, sizeof(d));
    d.rssi    = -100;
    d.minRssi = 0;
    d.maxRssi = -100;
    d.advType = ADV_TYPE_UNKNOWN;
    d.hasTxPower = false;
    d.hasEma  = false;
    d.ema     = -100.0f;
    for (uint8_t j = 0; j < RSSI_HISTORY_LEN; j++) {
        d.history[j]     = RSSI_UNSET;
        d.historyTime[j] = 0;
    }
}

// Llamar SOLO dentro de la seccion critica del mutex
static int findDeviceByAddressUnsafe(const char* address) {
    for (uint8_t i = 0; i < MAX_ANALYZER_DEVICES; i++) {
        if (!devices[i].used) continue;
        if (strcasecmp(devices[i].addr, address) == 0) return i;
    }
    return -1;
}

static int firstFreeSlotUnsafe() {
    for (uint8_t i = 0; i < MAX_ANALYZER_DEVICES; i++) {
        if (!devices[i].used) return i;
    }
    return -1;
}

// LRU eviction respetando al seleccionado: el `protectedAddr` nunca es
// candidato para ser expulsado (evita perder de vista el device que el
// usuario esta mirando en detail/graph).
static int evictionIndexUnsafe(const char* protectedAddr) {
    int oldest = -1;
    uint32_t oldestTime = 0xFFFFFFFFu;
    for (uint8_t i = 0; i < MAX_ANALYZER_DEVICES; i++) {
        if (!devices[i].used) continue;
        if (protectedAddr && protectedAddr[0] &&
            strcasecmp(devices[i].addr, protectedAddr) == 0) continue;
        if (devices[i].lastSeen < oldestTime) {
            oldestTime = devices[i].lastSeen;
            oldest = i;
        }
    }
    if (oldest >= 0) return oldest;
    // Caso extremo: todos los slots son el seleccionado. Devuelve cualquiera.
    for (uint8_t i = 0; i < MAX_ANALYZER_DEVICES; i++) {
        if (devices[i].used) return i;
    }
    return 0;
}

static void resetAnalyzer() {
    portENTER_CRITICAL(&deviceMux);
    for (uint8_t i = 0; i < MAX_ANALYZER_DEVICES; i++) {
        clearDeviceSlot(devices[i]);
    }
    deviceCount = 0;
    portEXIT_CRITICAL(&deviceMux);

    selectedAddr[0] = 0;
    detailView    = false;
    graphView     = false;
    detailScroll  = 0;
    scanActive    = false;
    scanStartedAt = 0;
    uint32_t now  = millis();
    lastRateTick  = now;
    lastPurgeTick = now;
    viewEnterTime = now;
}

// ============================================================================
// Callback de BLE
// ============================================================================

class AnalyzerCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice adv) override {
        if (exitingAnalyzer) return;

        // --- Extraccion: fuera del mutex, no toca el array compartido ------
        const std::string addrStr = adv.getAddress().toString();
        const std::string nameStr = adv.getName();
        const int rssi = adv.getRSSI();

        uint16_t companyId = 0;
        char makerBuf[16] = {0};
        char typeBuf[14]  = "BLE ADV";
        std::string mfgData;
        if (adv.haveManufacturerData()) {
            mfgData = adv.getManufacturerData();
            if (mfgData.length() >= 2) {
                companyId = ((uint8_t)mfgData[1] << 8) | (uint8_t)mfgData[0];
                const char* known = companyName(companyId);
                if (known) copyCStr(makerBuf, sizeof(makerBuf), known);
                else       snprintf(makerBuf, sizeof(makerBuf), "ID %04X", companyId);
                copyCStr(typeBuf, sizeof(typeBuf), "MFG DATA");
            }
        }
        if (!makerBuf[0]) {
            const char* fromName = inferMakerFromNameC(nameStr.c_str());
            if (fromName && fromName[0]) copyCStr(makerBuf, sizeof(makerBuf), fromName);
        }
        if (!makerBuf[0]) copyCStr(makerBuf, sizeof(makerBuf), "Unknown");

        const char* finalName = nameStr.c_str();
        if (!finalName[0]) {
            finalName = (strcmp(makerBuf, "Unknown") != 0) ? makerBuf : addrStr.c_str();
        }

        char svcBuf[14];
        buildServiceLabel(svcBuf, sizeof(svcBuf), adv);

        uint8_t advType = tryGetAdvType(adv, 0);
        bool advIsScanRsp = (advType == ESP_BLE_EVT_SCAN_RSP);
        char connBuf[8] = {0};
        if (advType != ADV_TYPE_UNKNOWN && !advIsScanRsp) {
            copyCStr(connBuf, sizeof(connBuf), connectableLabel(advType));
        } else if (!advIsScanRsp) {
            copyCStr(connBuf, sizeof(connBuf),
                     inferConnectableHeuristic(adv, companyId, mfgData));
        }

        bool   hasTx = adv.haveTXPower();
        int8_t tx    = hasTx ? adv.getTXPower() : 0;

        const char* addrTypeStr = "ADDR?";
        switch (adv.getAddressType()) {
            case BLE_ADDR_TYPE_PUBLIC:     addrTypeStr = "PUBLIC";  break;
            case BLE_ADDR_TYPE_RANDOM:     addrTypeStr = "RANDOM";  break;
            case BLE_ADDR_TYPE_RPA_PUBLIC: addrTypeStr = "RPA-PUB"; break;
            case BLE_ADDR_TYPE_RPA_RANDOM: addrTypeStr = "RPA-RND"; break;
            default: break;
        }

        char protectedAddr[18];
        strncpy(protectedAddr, selectedAddr, sizeof(protectedAddr) - 1);
        protectedAddr[sizeof(protectedAddr) - 1] = 0;

        uint32_t now = millis();

        // --- Seccion critica: solo update de devices[] -----------------
        portENTER_CRITICAL(&deviceMux);

        int idx = findDeviceByAddressUnsafe(addrStr.c_str());
        bool firstPacket = false;
        if (idx < 0) {
            idx = firstFreeSlotUnsafe();
            if (idx < 0) idx = evictionIndexUnsafe(protectedAddr);
            if (idx >= 0) {
                clearDeviceSlot(devices[idx]);
                firstPacket = true;
                if ((uint8_t)idx >= deviceCount) deviceCount = idx + 1;
            }
        }

        if (idx >= 0) {
            AnalyzerDevice& d = devices[idx];

            copyCStr(d.name,     sizeof(d.name),     finalName);
            copyCStr(d.addr,     sizeof(d.addr),     addrStr.c_str());
            copyCStr(d.maker,    sizeof(d.maker),    makerBuf);
            copyCStr(d.type,     sizeof(d.type),     typeBuf);
            copyCStr(d.service,  sizeof(d.service),  svcBuf);
            copyCStr(d.addrType, sizeof(d.addrType), addrTypeStr);

            if (advType != ADV_TYPE_UNKNOWN && !advIsScanRsp) {
                d.advType = advType;
                copyCStr(d.connectable, sizeof(d.connectable),
                         connectableLabel(advType));
            } else if (advIsScanRsp) {
                if (d.advType == ADV_TYPE_UNKNOWN) {
                    copyCStr(d.connectable, sizeof(d.connectable), "SCAN");
                }
            } else {
                copyCStr(d.connectable, sizeof(d.connectable), connBuf);
            }

            if (hasTx) { d.hasTxPower = true; d.txPower = tx; }
            d.companyId = companyId;

            if (firstPacket || d.totalPackets == 0) {
                d.firstSeen = now;
                d.minRssi   = rssi;
                d.maxRssi   = rssi;
            } else {
                if (rssi < d.minRssi) d.minRssi = rssi;
                if (rssi > d.maxRssi) d.maxRssi = rssi;
            }
            d.used = true;
            d.rssi = rssi;

            if (d.avgCount < 1000) {
                d.avgSum += rssi;
                d.avgCount++;
            }

            if (!d.hasEma) {
                d.ema    = (float)rssi;
                d.hasEma = true;
            } else {
                d.ema = d.ema * (1.0f - EMA_ALPHA) + (float)rssi * EMA_ALPHA;
            }

            if (d.totalPackets  < 0xFFFE) d.totalPackets++;
            if (d.secondPackets < 0xFFFE) d.secondPackets++;
            d.lastSeen = now;
            d.history[d.historyPos]     = (int8_t)rssi;
            d.historyTime[d.historyPos] = now;
            d.historyPos = (d.historyPos + 1) % RSSI_HISTORY_LEN;
        }

        portEXIT_CRITICAL(&deviceMux);
    }
};

static AnalyzerCallbacks analyzerCallbacks;

// ============================================================================
// Control de scan
// ============================================================================

static void onAnalyzerScanComplete(BLEScanResults results) {
    scanActive = false;
    BLEScan* s = analyzerScan;  // copia local, evita race con btAnalyzerExit
    if (s && !exitingAnalyzer) s->clearResults();
}

static void startAnalyzerScan() {
    if (!analyzerScan || scanActive || exitingAnalyzer) return;
    scanActive = true;
    scanStartedAt = millis();
    analyzerScan->start(1, onAnalyzerScanComplete, false);
}

static void serviceScan() {
    if (!analyzerScan || exitingAnalyzer) return;
    uint32_t now = millis();
    if (!scanActive) { startAnalyzerScan(); return; }
    if (now - scanStartedAt > SCAN_RESTART_MS) {
        analyzerScan->stop();
        analyzerScan->clearResults();
        scanActive = false;
    }
}

static void updatePacketRates() {
    uint32_t now = millis();
    if (now - lastRateTick < 1000) return;
    lastRateTick = now;

    portENTER_CRITICAL(&deviceMux);
    for (uint8_t i = 0; i < deviceCount; i++) {
        if (!devices[i].used) continue;
        devices[i].pps = devices[i].secondPackets;
        devices[i].secondPackets = 0;
    }
    portEXIT_CRITICAL(&deviceMux);
}

// Marca como libres los slots con lastSeen > PURGE_MS de antiguedad,
// nunca purga el seleccionado actual.
static void purgeStale() {
    uint32_t now = millis();
    if (now - lastPurgeTick < 2000) return;
    lastPurgeTick = now;

    portENTER_CRITICAL(&deviceMux);
    for (uint8_t i = 0; i < deviceCount; i++) {
        if (!devices[i].used) continue;
        if (selectedAddr[0] && strcasecmp(devices[i].addr, selectedAddr) == 0) continue;
        if (now - devices[i].lastSeen > PURGE_MS) {
            devices[i].used = false;
        }
    }
    // Recalcula highwater mark
    int lastUsed = -1;
    for (int i = (int)deviceCount - 1; i >= 0; i--) {
        if (devices[i].used) { lastUsed = i; break; }
    }
    deviceCount = (uint8_t)(lastUsed + 1);
    portEXIT_CRITICAL(&deviceMux);
}

// ============================================================================
// Snapshots para renderizado
// ============================================================================

struct ListItem {
    char     name[18];
    char     addr[18];
    int      rssi;
    uint16_t pps;
    uint32_t lastSeen;
    uint32_t firstSeen;
};

static uint8_t buildListSnapshot(ListItem* out, uint8_t maxItems) {
    uint8_t n = 0;
    portENTER_CRITICAL(&deviceMux);
    for (uint8_t i = 0; i < deviceCount && n < maxItems; i++) {
        const AnalyzerDevice& d = devices[i];
        if (!d.used) continue;
        memcpy(out[n].name, d.name, sizeof(out[n].name));
        memcpy(out[n].addr, d.addr, sizeof(out[n].addr));
        out[n].rssi      = d.rssi;
        out[n].pps       = d.pps;
        out[n].lastSeen  = d.lastSeen;
        out[n].firstSeen = d.firstSeen;
        n++;
    }
    portEXIT_CRITICAL(&deviceMux);
    return n;
}

static bool snapshotByAddr(const char* addr, AnalyzerDevice& out) {
    if (!addr || !addr[0]) return false;
    bool found = false;
    portENTER_CRITICAL(&deviceMux);
    for (uint8_t i = 0; i < deviceCount; i++) {
        if (!devices[i].used) continue;
        if (strcasecmp(devices[i].addr, addr) == 0) {
            out = devices[i];
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&deviceMux);
    return found;
}

static void sortListSnapshot(ListItem* items, uint8_t n) {
    if (n < 2) return;
    // Insertion sort: n <= 24, O(n^2) trivial
    for (uint8_t i = 1; i < n; i++) {
        ListItem tmp = items[i];
        int j = (int)i - 1;
        while (j >= 0) {
            bool swap = false;
            switch (sortMode) {
                case SORT_RSSI_DESC: swap = items[j].rssi      < tmp.rssi;      break;
                case SORT_RECENT:    swap = items[j].lastSeen  < tmp.lastSeen;  break;
                case SORT_DISCOVERY:
                default:             swap = items[j].firstSeen > tmp.firstSeen; break;
            }
            if (!swap) break;
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = tmp;
    }
}

// ============================================================================
// Calculos auxiliares
// ============================================================================

// Promedio con redondeo correcto para enteros negativos (truncacion hacia
// cero daria sesgo positivo: -75.5 -> -75 en lugar de -76).
static int avgRssi(const AnalyzerDevice& d) {
    if (d.avgCount == 0) return d.rssi;
    int sum   = d.avgSum;
    int count = (int)d.avgCount;
    int half  = count / 2;
    if (sum < 0) return (sum - half) / count;
    return (sum + half) / count;
}

static int rssiToGraphY(int rssi, int gy, int gh) {
    if (rssi > RSSI_GRAPH_TOP) rssi = RSSI_GRAPH_TOP;
    if (rssi < RSSI_GRAPH_BOT) rssi = RSSI_GRAPH_BOT;
    int span = RSSI_GRAPH_TOP - RSSI_GRAPH_BOT;
    return gy + ((RSSI_GRAPH_TOP - rssi) * (gh - 1)) / span;
}

// Borde derecho = "ahora", borde izquierdo = "ahora - GRAPH_WINDOW_MS"
static int timeToGraphX(uint32_t t, uint32_t now, int gx, int gw) {
    int32_t age = (int32_t)(now - t);
    if (age < 0) age = 0;
    if ((uint32_t)age > GRAPH_WINDOW_MS) age = GRAPH_WINDOW_MS;
    return gx + gw - 1 - (int)(((uint32_t)age * (uint32_t)(gw - 1)) / GRAPH_WINDOW_MS);
}

// ============================================================================
// Overlay de transicion (wipe)
// ============================================================================

static void drawWipeOverlay() {
    uint32_t now = millis();
    uint32_t elapsed = now - viewEnterTime;
    if (elapsed >= WIPE_MS) return;
    int revealW = (int)((elapsed * 128u) / WIPE_MS);
    if (revealW < 0) revealW = 0;
    if (revealW > 128) revealW = 128;
    u8g2.setDrawColor(0);
    u8g2.drawBox(revealW, 0, 128 - revealW, 64);
    u8g2.setDrawColor(1);
}

// ============================================================================
// Lista de devices
// ============================================================================

static void drawAnalyzerListRow(const ListItem& item, int y, bool selected) {
    uint32_t now = millis();
    uint32_t age = now - item.lastSeen;

    if (selected) {
        u8g2.drawBox(0, y - 8, 128, 10);
        u8g2.setDrawColor(0);
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, y, selected ? ">" : " ");
    u8g2.drawStr(10, y, item.name);

    unsigned displayPps = item.pps > 999 ? 999 : item.pps;
    char meta[14];
    snprintf(meta, sizeof(meta), "%d %u", item.rssi, displayPps);
    u8g2.drawStr(72, y, meta);

    uint8_t bars = age > STALE_MS ? 1 : rssiBars(item.rssi);
    UiTheme::drawSignalBars(u8g2, 114, y, bars);

    // Pulso "paquete fresco": punto a la derecha del row si llego algo
    // en los ultimos BAR_BLINK_MS ms.
    if (age < BAR_BLINK_MS) {
        u8g2.drawDisc(125, y - 4, 1);
    }

    if (selected) u8g2.setDrawColor(1);
}

// Animacion "buscando": linea base con un pulso gaussiano barriendo
// de izquierda a derecha. Cambia cada frame (no requiere paquetes).
static void drawSearchingAnimation(int cx, int cy, int w) {
    uint32_t now = millis();
    const uint32_t period = 1800;
    float phase  = (now % period) / (float)period;
    int peakX    = cx - w / 2 + (int)(phase * w);
    int halfW    = w / 2;
    int prevX = -1, prevY = -1;
    for (int dx = -halfW; dx <= halfW; dx++) {
        int x = cx + dx;
        int dxp = x - peakX;
        float bump = 7.0f * expf(-(float)(dxp * dxp) / 72.0f);
        int y = cy - (int)bump;
        if (prevX >= 0) u8g2.drawLine(prevX, prevY, x, y);
        prevX = x; prevY = y;
    }
}

static void drawAnalyzerList(const ListItem* items, uint8_t n, int selPos) {
    char status[12];
    snprintf(status, sizeof(status), "%02u %s", n, sortLabel(sortMode));
    UiTheme::drawHeader(u8g2, "BT ANALYZER", status);

    // Indicador de scan activo (parpadea cada 500 ms)
    if (scanActive && (millis() / 250) % 2 == 0) {
        u8g2.drawDisc(3, 4, 1);
    }

    if (n == 0) {
        u8g2.setFont(u8g2_font_5x7_tr);
        UiTheme::drawCenteredText(u8g2, 28, "ESCANEANDO BLE");
        drawSearchingAnimation(64, 52, 100);
        drawWipeOverlay();
        return;
    }

    int first = selPos - 2;
    if (first < 0) first = 0;
    if (first > (int)n - 5) first = max(0, (int)n - 5);

    for (uint8_t row = 0; row < 5; row++) {
        int idx = first + row;
        if (idx >= n) break;
        drawAnalyzerListRow(items[idx], 24 + row * 8, idx == selPos);
    }

    drawWipeOverlay();
}

// ============================================================================
// Grafico RSSI (osciloscopio con scroll temporal)
// ============================================================================

// Animaciones que aporta esta funcion:
//   * Scroll temporal continuo (los samples se mueven a la izquierda con el
//     tiempo, no con la llegada de paquetes).
//   * Disco "latido" que decae de radio 3 a 1 px en PULSE_MS.
//   * Marca EMA discreta a la derecha.
//   * Min/Max como ticks en el borde izquierdo.
//   * Linea de promedio con patron 3-on/3-off para diferenciarla del grid.
//   * Si el ultimo sample esta fuera de la ventana, no se dibuja disco
//     (la grafica queda vacia => signal lost).
static void drawRssiGraph(const AnalyzerDevice& d, int x, int y, int w, int h) {
    u8g2.drawFrame(x, y, w, h);

    int inX = x + 1, inY = y + 1;
    int inW = w - 2, inH = h - 2;
    uint32_t now = millis();

    // Grid horizontal de referencia (-50/-70/-90 dBm), pasos de 4 px
    const int refValues[3] = {-50, -70, -90};
    for (uint8_t r = 0; r < 3; r++) {
        int gy = rssiToGraphY(refValues[r], inY, inH);
        for (int gx = inX; gx < inX + inW; gx += 4) u8g2.drawPixel(gx, gy);
    }

    // Linea de promedio (patron 3-on/3-off)
    if (d.avgCount > 0) {
        int ay = rssiToGraphY(avgRssi(d), inY, inH);
        for (int gx = inX; gx < inX + inW; gx++) {
            if (((gx - inX) % 6) < 3) u8g2.drawPixel(gx, ay);
        }
    }

    // Ticks de min/max en el borde izquierdo
    if (d.totalPackets > 0) {
        int ymin = rssiToGraphY(d.minRssi, inY, inH);
        int ymax = rssiToGraphY(d.maxRssi, inY, inH);
        u8g2.drawHLine(inX, ymin, 3);
        u8g2.drawHLine(inX, ymax, 3);
    }

    // Curva principal: cronologica, con timestamps reales.
    // Si un slot esta sin inicializar o sale de la ventana, rompe la
    // polilinea (deja un hueco real en vez de inventar una linea plana).
    int prevX = -1, prevY = -1;
    int lastValidX = -1, lastValidY = -1;
    uint32_t lastValidT = 0;

    for (uint8_t i = 0; i < RSSI_HISTORY_LEN; i++) {
        uint8_t pos = (d.historyPos + i) % RSSI_HISTORY_LEN;
        int8_t   v  = d.history[pos];
        uint32_t t  = d.historyTime[pos];
        if (v == RSSI_UNSET || t == 0) { prevX = -1; continue; }
        if (now - t > GRAPH_WINDOW_MS) { prevX = -1; continue; }
        int gx = timeToGraphX(t, now, inX, inW);
        int gy = rssiToGraphY((int)v, inY, inH);
        if (prevX >= 0) u8g2.drawLine(prevX, prevY, gx, gy);
        prevX = gx; prevY = gy;
        lastValidX = gx; lastValidY = gy; lastValidT = t;
    }

    // EMA discreta como tick horizontal de 4 px en el borde derecho
    if (d.hasEma) {
        int ey = rssiToGraphY((int)roundf(d.ema), inY, inH);
        u8g2.drawHLine(inX + inW - 4, ey, 4);
    }

    // Disco con latido en el ultimo sample
    if (lastValidX >= 0) {
        uint32_t ageLast = now - lastValidT;
        int r = 1;
        if (ageLast < PULSE_MS) {
            // 3 -> 1 px linealmente
            r = 3 - (int)((ageLast * 2u) / PULSE_MS);
            if (r < 1) r = 1;
        }
        u8g2.drawDisc(lastValidX, lastValidY, r);
    }
}

static void drawAnalyzerGraph(const AnalyzerDevice& d) {
    char status[12];
    snprintf(status, sizeof(status), "%dp/s", d.pps);
    UiTheme::drawHeader(u8g2, "RSSI", status);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 22);
    u8g2.print(d.name);

    char stats[32];
    snprintf(stats, sizeof(stats), "%d av:%d %d/%d",
             d.rssi, avgRssi(d), d.minRssi, d.maxRssi);
    u8g2.setCursor(2, 30);
    u8g2.print(stats);

    // Etiquetas de eje (font 4x6, "-99" es 12 px de ancho)
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(0, 37, "-40");
    u8g2.drawStr(0, 49, "-70");
    u8g2.drawStr(0, 62, "-99");

    drawRssiGraph(d, 13, 32, 113, 31);

    // Tag "OLD" si paso STALE_MS sin nada
    uint32_t age = millis() - d.lastSeen;
    if (age > STALE_MS) {
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(98, 41, "OLD");
    }

    drawWipeOverlay();
}

// ============================================================================
// Vista de detalle
// ============================================================================

static void drawDetailLine(uint8_t visibleRow, const char* label, const char* value) {
    int y = 21 + visibleRow * 10;
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(2, y);
    u8g2.print(label);
    u8g2.print(value);
}

static void formatDistance(char* buf, size_t len, float dist) {
    if (dist < 0.0f) {
        snprintf(buf, len, "N/D");
    } else if (dist < 1.0f) {
        snprintf(buf, len, "~%dcm", (int)(dist * 100.0f));
    } else if (dist < 100.0f) {
        int whole = (int)dist;
        int frac  = (int)(dist * 10.0f) % 10;
        snprintf(buf, len, "~%d.%dm", whole, frac);
    } else {
        snprintf(buf, len, ">100m");
    }
}

static void drawAnalyzerDetail(const AnalyzerDevice& d, int totalUsed, int posInList) {
    char status[10];
    snprintf(status, sizeof(status), "%02d/%02d",
             posInList + 1, totalUsed > 0 ? totalUsed : 1);
    UiTheme::drawHeader(u8g2, "BT DETAIL", status);

    char rssiLine[20], rangeLine[24], avgLine[20], pktLine[20];
    char fabLine[22],  ageLine[18],   txLine[16],  distLine[18], emaLine[16];

    snprintf(rssiLine,  sizeof(rssiLine),  "%d dBm", d.rssi);
    snprintf(rangeLine, sizeof(rangeLine), "%d/%d", d.minRssi, d.maxRssi);
    snprintf(avgLine,   sizeof(avgLine),   "%d dBm", avgRssi(d));
    snprintf(pktLine,   sizeof(pktLine),   "%u/s %u tot",
             (unsigned)d.pps, (unsigned)d.totalPackets);

    if (d.companyId) snprintf(fabLine, sizeof(fabLine), "%s (%04X)", d.maker, d.companyId);
    else             snprintf(fabLine, sizeof(fabLine), "%s", d.maker);

    snprintf(ageLine, sizeof(ageLine), "%lus", (unsigned long)((millis() - d.lastSeen) / 1000UL));

    if (d.hasTxPower) snprintf(txLine, sizeof(txLine), "%d dBm", d.txPower);
    else              snprintf(txLine, sizeof(txLine), "N/D");

    if (d.hasEma) snprintf(emaLine, sizeof(emaLine), "%d dBm", (int)roundf(d.ema));
    else          snprintf(emaLine, sizeof(emaLine), "N/D");

    formatDistance(distLine, sizeof(distLine),
                   estimateDistance(d.rssi, d.txPower, d.hasTxPower));

    const char* labels[] = {
        "N: ", "MAC: ", "RSSI: ", "RANGO: ", "AVG: ", "TEND: ",
        "PKT: ", "FAB: ", "SERV: ", "CONN: ", "ADDR: ", "TX: ",
        "DIST: ", "VISTO: "
    };
    const char* values[] = {
        d.name,
        d.addr,
        rssiLine,
        rangeLine,
        avgLine,
        emaLine,
        pktLine,
        fabLine,
        d.service[0] ? d.service : "N/D",
        d.connectable[0] ? d.connectable : "?",
        d.addrType,
        txLine,
        distLine,
        ageLine
    };

    const uint8_t totalLines = sizeof(labels) / sizeof(labels[0]);
    if (detailScroll < 0) detailScroll = 0;
    if (detailScroll > (int)totalLines - 4) detailScroll = totalLines - 4;

    for (uint8_t row = 0; row < 4; row++) {
        uint8_t idx = detailScroll + row;
        if (idx >= totalLines) break;
        drawDetailLine(row, labels[idx], values[idx]);
    }

    // Rail de scroll lateral
    int railX = 124;
    int railY = 18;
    int railH = 40;
    int markerH = max(6, railH / (int)totalLines);
    int markerY = railY + ((railH - markerH) * detailScroll) / max(1, (int)totalLines - 4);
    u8g2.drawVLine(railX, railY, railH);
    u8g2.drawBox(railX - 1, markerY, 3, markerH);

    drawWipeOverlay();
}

// ============================================================================
// Ciclo del modulo
// ============================================================================

void btAnalyzerEnter() {
    exitingAnalyzer = false;
    BLEDevice::init("");
    analyzerScan = BLEDevice::getScan();
    analyzerScan->setActiveScan(true);
    analyzerScan->setInterval(80);
    analyzerScan->setWindow(70);
    analyzerScan->setAdvertisedDeviceCallbacks(&analyzerCallbacks, true);
    resetAnalyzer();
}

void btAnalyzerLoop() {
    serviceScan();
    updatePacketRates();
    purgeStale();

    // Snapshot ordenado de la lista (una sola toma de mutex por frame)
    ListItem listItems[MAX_ANALYZER_DEVICES];
    uint8_t  listCount = buildListSnapshot(listItems, MAX_ANALYZER_DEVICES);
    sortListSnapshot(listItems, listCount);

    // Resuelve la posicion del seleccionado dentro de la lista ordenada
    int selPos = -1;
    if (selectedAddr[0]) {
        for (uint8_t i = 0; i < listCount; i++) {
            if (strcasecmp(listItems[i].addr, selectedAddr) == 0) { selPos = (int)i; break; }
        }
    }
    if (selPos < 0 && listCount > 0) {
        selPos = 0;
        copyCStr(selectedAddr, sizeof(selectedAddr), listItems[0].addr);
    }

    // -----------------------------------------------------------------------
    // Detail / Graph
    // -----------------------------------------------------------------------
    if (detailView) {
        if (Input.pressed(BTN_ID_BACK)) {
            detailView   = false;
            graphView    = false;
            detailScroll = 0;
            viewEnterTime = millis();
            Input.consume(BTN_ID_BACK);
            return;
        }
        if (Input.pressed(BTN_ID_OK)) {
            graphView    = !graphView;
            viewEnterTime = millis();
            return;
        }
        if (!graphView) {
            if (Input.repeating(BTN_ID_DOWN)) detailScroll++;
            if (Input.repeating(BTN_ID_UP))   detailScroll--;
        }

        AnalyzerDevice selDev;
        bool haveSel = snapshotByAddr(selectedAddr, selDev);

        u8g2.clearBuffer();
        if (haveSel) {
            if (graphView) drawAnalyzerGraph(selDev);
            else           drawAnalyzerDetail(selDev, listCount,
                                              selPos < 0 ? 0 : selPos);
        }
        u8g2.sendBuffer();
        return;
    }

    // -----------------------------------------------------------------------
    // Lista
    // -----------------------------------------------------------------------
    if (listCount > 0) {
        if (Input.repeating(BTN_ID_DOWN)) {
            selPos = (selPos + 1) % listCount;
            copyCStr(selectedAddr, sizeof(selectedAddr), listItems[selPos].addr);
        }
        if (Input.repeating(BTN_ID_UP)) {
            selPos = (selPos - 1 + listCount) % listCount;
            copyCStr(selectedAddr, sizeof(selectedAddr), listItems[selPos].addr);
        }
        if (Input.pressed(BTN_ID_OK)) {
            detailView    = true;
            graphView     = false;
            detailScroll  = 0;
            viewEnterTime = millis();
        }
    }

    u8g2.clearBuffer();
    drawAnalyzerList(listItems, listCount, selPos < 0 ? 0 : selPos);
    u8g2.sendBuffer();
}

void btAnalyzerExit() {
    // Avisa al callback que ya no toque nada
    exitingAnalyzer = true;

    BLEScan* s = analyzerScan;
    analyzerScan = nullptr;
    if (s) {
        s->stop();
        s->clearResults();
    }

    if (!btRemoteBleActive()) {
        BLEDevice::deinit(false);
    }

    resetAnalyzer();
    exitingAnalyzer = false;
}