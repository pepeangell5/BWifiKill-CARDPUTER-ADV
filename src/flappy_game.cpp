#include "flappy_game.h"
#include <U8g2lib.h>
#include "input_manager.h"
#include "app_config.h"
#include "ui_theme.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

// =============================================================
// Física del pájaro (sin cambios)
// =============================================================
static float birdY = 32.0;
static float velocity = 0.0;
static const float gravity = 0.15;
static const float jumpImpulse = -1.2;

// =============================================================
// Tubos y dificultad (sin cambios)
// =============================================================
static float pipeX = 128;
static float pipeSpeed = 1.8;
static int gapY = 32;
static const int gapSize = 28;
static int score = 0;
static bool gameOver = false;
static unsigned long lastDifficultyBoost = 0;
static const int FLAPPY_PLAY_TOP = 11;
static const int FLAPPY_PLAY_BOTTOM = 63;

// =============================================================
// Nubes de fondo (puro polish)
// =============================================================
static float cloud1X = 100, cloud2X = 50;

static void drawCloud(int x, int y) {
    if (x < -16 || x > 128) return;
    if (y < FLAPPY_PLAY_TOP + 4) y = FLAPPY_PLAY_TOP + 4;
    u8g2.drawDisc(x, y, 3);
    u8g2.drawDisc(x + 4, y - 1, 3);
    u8g2.drawDisc(x + 8, y, 2);
}

static void drawBird(int x, int y, uint32_t ms) {
    if (y < FLAPPY_PLAY_TOP) y = FLAPPY_PLAY_TOP;
    if (y > FLAPPY_PLAY_BOTTOM - 4) y = FLAPPY_PLAY_BOTTOM - 4;
    u8g2.drawBox(x, y, 4, 4);
    // Ojito
    u8g2.setDrawColor(0);
    u8g2.drawPixel(x + 3, y + 1);
    u8g2.setDrawColor(1);
    // Ala animada
    if ((ms / 80) % 2 == 0) {
        u8g2.drawPixel(x - 1, y + 2);
    } else {
        u8g2.drawPixel(x - 1, y + 1);
    }
}

void flappySetup() {
    birdY = 32.0;
    velocity = 0.0;
    pipeX = 128;
    pipeSpeed = 1.8;
    gapY = random(18, 42);
    score = 0;
    gameOver = false;
    cloud1X = 100;
    cloud2X = 50;
    lastDifficultyBoost = millis();
    Input.resetAll();
}

void flappyLoop() {
    u8g2.clearBuffer();
    uint32_t ms = millis();

    if (!gameOver) {
        // Hold to rise (preserva mecánica original)
        if (Input.held(BTN_ID_OK)) {
            velocity = jumpImpulse;
        }

        // Física
        velocity += gravity;
        birdY += velocity;

        // Dificultad progresiva
        if (ms - lastDifficultyBoost > 5000) {
            if (pipeSpeed < 4.5) pipeSpeed += 0.2;
            lastDifficultyBoost = ms;
        }

        // Movimiento tubos
        pipeX -= pipeSpeed;
        if (pipeX < -15) {
            pipeX = 128;
            gapY = random(18, 42);
            score++;
        }

        // Movimiento nubes (más lento, parallax)
        cloud1X -= pipeSpeed * 0.3;
        cloud2X -= pipeSpeed * 0.2;
        if (cloud1X < -16) cloud1X = 128 + random(0, 30);
        if (cloud2X < -16) cloud2X = 128 + random(0, 30);

        // Colisiones
        if (birdY < 0 || birdY > 63) gameOver = true;
        if (pipeX < 25 && pipeX > 10) {
            if (birdY < gapY - (gapSize / 2) || birdY > gapY + (gapSize / 2)) {
                gameOver = true;
            }
        }

        // ===== DIBUJAR =====
        // Nubes al fondo
        drawCloud((int)cloud1X, 14);
        drawCloud((int)cloud2X, 22);

        // Pájaro
        drawBird(15, (int)birdY, ms);

        int topPipeBottom = max(FLAPPY_PLAY_TOP, gapY - (gapSize / 2));
        int bottomPipeTop = max(FLAPPY_PLAY_TOP, gapY + (gapSize / 2));
        if (topPipeBottom > FLAPPY_PLAY_TOP) {
            u8g2.drawBox((int)pipeX, FLAPPY_PLAY_TOP, 12, topPipeBottom - FLAPPY_PLAY_TOP);
            u8g2.drawBox((int)pipeX - 1, max(FLAPPY_PLAY_TOP, topPipeBottom - 3), 14, 3);
        }
        u8g2.drawBox((int)pipeX, bottomPipeTop, 12, FLAPPY_PLAY_BOTTOM - bottomPipeTop + 1);
        u8g2.drawBox((int)pipeX - 1, bottomPipeTop, 14, 3);

        u8g2.drawHLine(0, 10, 128);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(2, 8);
        u8g2.print("SC "); u8g2.print(score);
        char speedBuf[8];
        snprintf(speedBuf, sizeof(speedBuf), "v%.1f", pipeSpeed);
        int sw = u8g2.getStrWidth(speedBuf);
        u8g2.drawStr(126 - sw, 8, speedBuf);

    } else {
        // ===== GAME OVER =====
        // "Pluma cayendo" — efecto pequeño
        for (int i = 0; i < 6; i++) {
            int fy = ((ms / 50 + i * 11) % 64);
            int fx = 20 + i * 16 + ((ms / 100 + i) % 3);
            u8g2.drawPixel(fx, fy);
        }

        u8g2.setFont(u8g2_font_6x12_tr);
        const char* msg = "GAME OVER";
        int w = u8g2.getStrWidth(msg);
        u8g2.drawStr((128 - w) / 2, 26, msg);

        u8g2.setFont(u8g2_font_5x7_tr);
        char line[20];
        snprintf(line, sizeof(line), "SCORE: %d", score);
        int lw = u8g2.getStrWidth(line);
        u8g2.drawStr((128 - lw) / 2, 40, line);

        UiTheme::drawProgressBar(u8g2, 34, 55, 60, 4, (ms / 12) % 100);

        if (Input.pressed(BTN_ID_OK)) {
            flappySetup();
        }
    }

    u8g2.sendBuffer();
}
