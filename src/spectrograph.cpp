#include "spectrograph.h"
#include "ui_theme.h"
#include "app_config.h"
#include "input_manager.h"
#include "audio_feedback.h"
#include <RF24.h>
#include <U8g2lib.h>

extern RF24 jam1;
extern RF24 jam2;
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

#define ALERT_DURATION 1800

static const uint8_t  CALIBRATION_FRAMES    = 14;
static const uint8_t  CONFIRM_FRAMES        = 5;
static const uint16_t MIN_ATTACK_TOTAL      = 1400;
static const uint16_t MIN_ATTACK_DELTA      = 900;
static const uint8_t  ACTIVE_SAMPLE_LEVEL   = 10;
static const uint8_t  STRONG_SAMPLE_LEVEL   = 22;
static const uint8_t  MIN_ACTIVE_CHANNELS   = 20;
static const uint8_t  MIN_STRONG_CHANNELS   = 8;

static const uint8_t GRAPH_TOP    = 20;
static const uint8_t GRAPH_BOTTOM = 52;
static const uint8_t GRAPH_HEIGHT = GRAPH_BOTTOM - GRAPH_TOP;

static uint8_t peak_heights[125];
static unsigned long lastPeakReset  = 0;
static unsigned long alertStartTime = 0;
static uint8_t       frameTick      = 0;
static long          ambientEnergy  = 0;
static uint8_t       calibrationFrames = 0;
static uint8_t       suspectFrames  = 0;
static bool          calibrated     = false;

static uint8_t scaleSample(int sample) {
    int safeSample = constrain(sample, 0, 40);
    return map(safeSample, 0, 40, 0, GRAPH_HEIGHT);
}

// =============================================================
// Dibujo del espectro: barras de frecuencia + peak hold
// =============================================================

// Líneas verticales tenues marcando los canales WiFi 1, 6, 11 (los más usados).
// En nRF24 cada canal son 1 MHz, los canales WiFi están a:
//   ch1 → 12, ch6 → 37, ch11 → 62
static void drawBandMarkers() {
    const uint8_t wifiCh[] = {12, 37, 62};
    const char* labels[]   = {"1", "6", "11"};
    for (int i = 0; i < 3; i++) {
        uint8_t x = wifiCh[i];
        // Línea vertical punteada en el fondo
        for (uint8_t y = GRAPH_TOP; y < GRAPH_BOTTOM; y += 3) {
            u8g2.drawPixel(x, y);
        }
        // Pequeña etiqueta debajo
        u8g2.setFont(u8g2_font_4x6_tf);
        u8g2.drawStr(x - 2, GRAPH_BOTTOM + 7, labels[i]);
    }

    // Marca del segmento BT (canales 0-78 en 2.4 GHz ISM, los 124 nRF cubren todo)
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(110, GRAPH_BOTTOM + 7, "BT+");
}

// Piso del gráfico (linea tenue de referencia)
static void drawFloor() {
    for (uint8_t x = 0; x < 128; x += 2) {
        u8g2.drawPixel(x, GRAPH_BOTTOM);
    }
}

static void drawSpectrumBar(uint8_t channel, int sample) {
    uint8_t barHeight = scaleSample(sample);

    if (sample > peak_heights[channel]) {
        peak_heights[channel] = sample;
    }

    if (barHeight > 0) {
        u8g2.drawVLine(channel, GRAPH_BOTTOM - barHeight, barHeight);
    }

    // Peak hold: pequeño punto que cae lentamente
    uint8_t peakHeight = scaleSample(peak_heights[channel]);
    if (peakHeight > 0) {
        u8g2.drawPixel(channel, GRAPH_BOTTOM - peakHeight);
        // Marca más visible cada 4 canales (sin saturar)
        if ((channel & 3) == 0 && peakHeight > 4) {
            u8g2.drawPixel(channel, GRAPH_BOTTOM - peakHeight - 1);
        }
    }
}

static void drawHeader(long totalEnergy) {
    // Status compacto: energía RF actual + indicador de actividad
    char status[12];
    if (!calibrated) {
        snprintf(status, sizeof(status), "CAL%02u", calibrationFrames);
    } else {
        snprintf(status, sizeof(status), "RF%4ld", totalEnergy);
    }
    UiTheme::drawHeader(u8g2, "SPECTRUM", status);

    // Sub-header con info de banda y micro-onda animada
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(2, GRAPH_TOP - 1, "2.4 GHz");
    UiTheme::drawMiniWave(u8g2, 118, GRAPH_TOP - 1, frameTick);
}

static void drawAlertOverlay(long totalEnergy) {
    char energyText[18];
    snprintf(energyText, sizeof(energyText), "RF %ld", totalEnergy);

    // Caja de alerta inversa, mejor centrada
    u8g2.drawBox(6, 26, 116, 22);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_7x14_tf);
    UiTheme::drawCenteredText(u8g2, 39, "ATAQUE RF");
    u8g2.setFont(u8g2_font_5x7_tr);
    UiTheme::drawCenteredText(u8g2, 47, energyText);
    u8g2.setDrawColor(1);
}

static void drawCalibrationOverlay() {
    u8g2.drawBox(18, 29, 92, 16);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_5x7_tr);
    UiTheme::drawCenteredText(u8g2, 39, "CALIBRANDO RF");
    u8g2.setDrawColor(1);
}

static bool updateAttackDetector(long totalEnergy, uint8_t activeChannels, uint8_t strongChannels) {
    if (!calibrated) {
        if (calibrationFrames == 0) {
            ambientEnergy = totalEnergy;
        } else {
            ambientEnergy = ((ambientEnergy * 3L) + totalEnergy) / 4L;
        }

        calibrationFrames++;
        if (calibrationFrames >= CALIBRATION_FRAMES) {
            calibrated = true;
            suspectFrames = 0;
            alertStartTime = 0;
        }
        return false;
    }

    const long dynamicThreshold = max((long)MIN_ATTACK_TOTAL, ambientEnergy + (long)MIN_ATTACK_DELTA);
    const bool broadActivity = activeChannels >= MIN_ACTIVE_CHANNELS && strongChannels >= MIN_STRONG_CHANNELS;
    const bool energySpike = totalEnergy > dynamicThreshold;
    const bool suspectedAttack = energySpike && broadActivity;

    if (suspectedAttack) {
        if (suspectFrames < CONFIRM_FRAMES) suspectFrames++;
        if (suspectFrames >= CONFIRM_FRAMES) {
            alertStartTime = millis();
        }
    } else {
        if (suspectFrames > 0) suspectFrames--;
        ambientEnergy = ((ambientEnergy * 15L) + totalEnergy) / 16L;
    }

    return alertStartTime != 0 && (millis() - alertStartTime < ALERT_DURATION);
}

// =============================================================
// Ciclo de vida
// =============================================================

void spectrographSetup() {
    jam1.begin();
    jam1.setPALevel(RF24_PA_MAX);
    jam1.setDataRate(RF24_2MBPS);
    jam1.startListening();

    jam2.begin();
    jam2.setPALevel(RF24_PA_MAX);
    jam2.setDataRate(RF24_2MBPS);
    jam2.startListening();

    memset(peak_heights, 0, sizeof(peak_heights));
    alertStartTime    = 0;
    lastPeakReset     = millis();
    frameTick         = 0;
    ambientEnergy     = 0;
    calibrationFrames = 0;
    suspectFrames     = 0;
    calibrated        = false;
}

void spectrographEnter() { spectrographSetup(); }

void spectrographExit() {
    jam1.stopListening();
    jam2.stopListening();
    memset(peak_heights, 0, sizeof(peak_heights));
    alertStartTime    = 0;
    calibrationFrames = 0;
    suspectFrames     = 0;
    calibrated        = false;
}

void spectrographLoop() {
    u8g2.clearBuffer();

    long totalEnergy = 0;
    uint8_t activeChannels = 0;
    uint8_t strongChannels = 0;
    frameTick++;

    // Header + sub-header
    drawHeader(0);    // placeholder, luego sobrescribe con energía real
    drawFloor();
    drawBandMarkers();

    // Sample completo del espectro (124 canales con 2 radios)
    for (int i = 0; i < 62; i++) {
        int ch1 = i;
        int ch2 = i + 63;

        int s1 = 0;
        int s2 = 0;
#ifdef BWK_CARDPUTER_ADV
        jam1.setChannel(ch1);
        delayMicroseconds(100);
        for (int s = 0; s < 40; s++) {
            if (jam1.testCarrier()) s1++;
        }
        jam1.setChannel(ch2);
        delayMicroseconds(100);
        for (int s = 0; s < 40; s++) {
            if (jam1.testCarrier()) s2++;
        }
#else
        jam1.setChannel(ch1);
        jam2.setChannel(ch2);
        delayMicroseconds(100);
        for (int s = 0; s < 40; s++) {
            if (jam1.testCarrier()) s1++;
            if (jam2.testCarrier()) s2++;
        }
#endif

        totalEnergy += s1;
        totalEnergy += s2;

        if (s1 >= ACTIVE_SAMPLE_LEVEL) activeChannels++;
        if (s2 >= ACTIVE_SAMPLE_LEVEL) activeChannels++;
        if (s1 >= STRONG_SAMPLE_LEVEL) strongChannels++;
        if (s2 >= STRONG_SAMPLE_LEVEL) strongChannels++;

        drawSpectrumBar(ch1, s1);
        drawSpectrumBar(ch2, s2);
    }

    // Re-dibujar header con energía real (ya conocemos totalEnergy)
    drawHeader(totalEnergy);

    bool showAlert = updateAttackDetector(totalEnergy, activeChannels, strongChannels);
    if (!calibrated) {
        drawCalibrationOverlay();
    } else if (showAlert) {
        AudioFeedback::alert();
        drawAlertOverlay(totalEnergy);
    } else {
        AudioFeedback::activity(AUDIO_ACTIVITY_RF,
                                min<long>(100, totalEnergy / 18));
    }

    // Decay de peaks
    if (millis() - lastPeakReset > 4000) {
        for (int p = 0; p < 125; p++) {
            if (peak_heights[p] > 0) peak_heights[p]--;
        }
        lastPeakReset = millis();
    }

    u8g2.sendBuffer();
}
