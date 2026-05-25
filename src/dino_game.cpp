#include "dino_game.h"
#include <U8g2lib.h>
#include "input_manager.h"
#include "app_config.h"
#include "ui_theme.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

// =============================================================
// Física y animación (sin cambios)
// =============================================================
static float dinoY = 48.0;
static float dinoVelocity = 0.0;
static float currentGravity = 0.5;
static const float dinoJump = -6.5;
static bool isJumping = false;
static bool isDucking = false;
static int animFrame = 0;

// =============================================================
// Obstáculos
// =============================================================
static float obstX = 128.0;
static float obstY = 42.0;
static int obstType = 0;
static float gameSpeed = 3.2;
static int score = 0;
static bool gameOver = false;

// =============================================================
// Polish: nubes derivando + piedritas en el suelo
// =============================================================
static float cloud1X = 100;
static float cloud2X = 30;

void dinoSetup() {
    dinoY = 48.0;
    dinoVelocity = 0.0;
    currentGravity = 0.5;
    isJumping = false;
    isDucking = false;
    obstX = 128.0;
    obstY = 42.0;
    obstType = 0;
    gameSpeed = 3.2;
    score = 0;
    gameOver = false;
    cloud1X = 100;
    cloud2X = 30;
    Input.resetAll();
}

static void drawCloud(int x, int y) {
    if (x < -14 || x > 128) return;
    u8g2.drawDisc(x, y, 2);
    u8g2.drawDisc(x + 4, y - 1, 2);
    u8g2.drawDisc(x + 8, y, 2);
}

static void drawDino(int x, int y, bool ducking, int frame) {
    if (!ducking) {
        u8g2.drawBox(x, y, 8, 8);
        u8g2.drawBox(x + 4, y - 4, 6, 4);
        u8g2.drawPixel(x + 8, y - 3);
        // Ojito
        u8g2.setDrawColor(0);
        u8g2.drawPixel(x + 7, y - 2);
        u8g2.setDrawColor(1);
        if (frame == 0) { u8g2.drawVLine(x + 1, y + 8, 2); u8g2.drawVLine(x + 5, y + 8, 2); }
        else            { u8g2.drawVLine(x + 2, y + 8, 2); u8g2.drawVLine(x + 6, y + 8, 2); }
    } else {
        u8g2.drawBox(x, y + 4, 12, 4);
        u8g2.drawBox(x + 8, y + 2, 6, 3);
        u8g2.drawPixel(x + 12, y + 3);
    }
}

static void drawRealCactus(int x, int y) {
    u8g2.drawBox(x, y, 4, 16);
    u8g2.drawBox(x - 3, y + 5, 2, 6);
    u8g2.drawHLine(x - 2, y + 9, 2);
    u8g2.drawBox(x + 5, y + 2, 2, 6);
    u8g2.drawHLine(x + 4, y + 6, 1);
}

void dinoLoop() {
    u8g2.clearBuffer();
    uint32_t ms = millis();

    if (!gameOver) {
        // ===== ENTRADA =====
        if (Input.pressed(BTN_ID_OK) && !isJumping && !isDucking) {
            dinoVelocity = dinoJump;
            isJumping = true;
        }

        bool downPressed = Input.held(BTN_ID_DOWN);
        float effectiveGravity = currentGravity;

        if (downPressed) {
            if (isJumping) {
                effectiveGravity = currentGravity * 2.0;
                isDucking = false;
            } else {
                isDucking = true;
                isJumping = false;
            }
        } else {
            if (!isJumping) isDucking = false;
        }

        // ===== FÍSICA =====
        if (!isDucking) {
            dinoVelocity += effectiveGravity;
            dinoY += dinoVelocity;
        }
        if (dinoY >= 48.0) {
            dinoY = 48.0;
            dinoVelocity = 0;
            isJumping = false;
        }

        // ===== MOVIMIENTO Y DIFICULTAD =====
        obstX -= gameSpeed;
        if (obstX < -20) {
            obstX = 128;
            score++;
            if (gameSpeed < 9.5) {
                gameSpeed += 0.18;
                if (currentGravity < 0.85) currentGravity += 0.02;
            }
            if (score > 8 && random(0, 10) > 7) {
                obstType = 1;
                obstY = 32.0;
            } else {
                obstType = 0;
                obstY = 42.0;
            }
        }

        // Nubes derivando (más lentas que el suelo)
        cloud1X -= gameSpeed * 0.25;
        cloud2X -= gameSpeed * 0.15;
        if (cloud1X < -14) cloud1X = 128 + random(0, 40);
        if (cloud2X < -14) cloud2X = 128 + random(0, 40);

        if ((ms / 120) % 2 == 0) animFrame = 0; else animFrame = 1;

        // ===== COLISIONES =====
        if (obstType == 0) {
            if (obstX < 25 && obstX > 8 && dinoY > 38) gameOver = true;
        } else {
            if (obstX < 23 && obstX > 10 && !isDucking) gameOver = true;
        }

        // ===== DIBUJO =====
        drawCloud((int)cloud1X, 12);
        drawCloud((int)cloud2X, 20);

        u8g2.drawHLine(0, 58, 128);  // Suelo
        // Piedritas en el suelo (paralax con gameSpeed)
        for (int i = 0; i < 4; i++) {
            int px = ((int)(ms / 30) * 1 + i * 32) % 128;
            px = 128 - px;
            u8g2.drawPixel(px, 60);
            u8g2.drawPixel(px + 2, 61);
        }

        drawDino(15, (int)dinoY, isDucking, animFrame);

        if (obstType == 0) {
            drawRealCactus((int)obstX, (int)obstY);
        } else {
            u8g2.drawBox((int)obstX, (int)obstY, 10, 4);
            if (animFrame == 0) u8g2.drawTriangle(obstX + 2, obstY,     obstX + 8, obstY,     obstX + 5, obstY - 5);
            else                u8g2.drawTriangle(obstX + 2, obstY + 4, obstX + 8, obstY + 4, obstX + 5, obstY + 9);
        }

        u8g2.drawHLine(0, 9, 128);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(2, 7);
        u8g2.print("SC "); u8g2.print(score);
        char speedBuf[8];
        snprintf(speedBuf, sizeof(speedBuf), "v%.1f", gameSpeed);
        int sw = u8g2.getStrWidth(speedBuf);
        u8g2.drawStr(126 - sw, 7, speedBuf);

    } else {
        // ===== GAME OVER =====
        u8g2.drawHLine(0, 58, 128);
        // El dino caído
        drawDino(15, 48, true, 0);

        u8g2.setFont(u8g2_font_6x12_tr);
        const char* msg = "EXTINCTION";
        int w = u8g2.getStrWidth(msg);
        u8g2.drawStr((128 - w) / 2, 22, msg);

        u8g2.setFont(u8g2_font_5x7_tr);
        char line[20];
        snprintf(line, sizeof(line), "SCORE: %d", score);
        int lw = u8g2.getStrWidth(line);
        u8g2.drawStr((128 - lw) / 2, 36, line);

        UiTheme::drawProgressBar(u8g2, 34, 47, 60, 4, (ms / 12) % 100);

        if (Input.pressed(BTN_ID_OK)) dinoSetup();
    }

    u8g2.sendBuffer();
}
