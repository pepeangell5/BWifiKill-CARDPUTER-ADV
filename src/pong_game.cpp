#include "pong_game.h"
#include <U8g2lib.h>
#include "input_manager.h"
#include "app_config.h"
#include "ui_theme.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

// =============================================================
// Estado del juego (sin cambios en mecánica)
// =============================================================
static float ballX = 64, ballY = 32;
static float ballDX = 2.0, ballDY = 1.0;
static float speedMultiplier = 1.0;
static int paddleY = 24;
static int cpuY = 24;
static int scorePlayer = 0;
static int scoreCPU = 0;
static const int PONG_PLAY_TOP = 14;
static const int PONG_PLAY_BOTTOM = 63;
static const int PONG_PLAY_H = PONG_PLAY_BOTTOM - PONG_PLAY_TOP + 1;

// Estela de la pelota: últimas N posiciones
static const int TRAIL_LEN = 4;
static float trailX[TRAIL_LEN] = {64, 64, 64, 64};
static float trailY[TRAIL_LEN] = {32, 32, 32, 32};
static int trailIdx = 0;

void pongSetup() {
    ballX = 64; ballY = 32;
    ballDX = 2.0; ballDY = 1.0;
    speedMultiplier = 1.0;
    scorePlayer = 0; scoreCPU = 0;
    paddleY = 24; cpuY = 24;
    for (int i = 0; i < TRAIL_LEN; i++) {
        trailX[i] = ballX;
        trailY[i] = ballY;
    }
    trailIdx = 0;
    Input.resetAll();
}

static void pushTrail(float x, float y) {
    trailX[trailIdx] = x;
    trailY[trailIdx] = y;
    trailIdx = (trailIdx + 1) % TRAIL_LEN;
}

void pongLoop() {
    u8g2.clearBuffer();

    u8g2.drawHLine(0, 13, 128);
    for (int i = PONG_PLAY_TOP + 2; i < 64; i += 5) u8g2.drawVLine(64, i, 2);

    // Controles del jugador
    if (Input.held(BTN_ID_UP)   && paddleY > 0)  paddleY -= 3;
    if (Input.held(BTN_ID_DOWN) && paddleY < 48) paddleY += 3;

    // IA del CPU
    if (ballX > 50) {
        if (ballY > cpuY + 8 && cpuY < 48) cpuY += 2;
        if (ballY < cpuY + 8 && cpuY > 0)  cpuY -= 2;
    }

    // Física
    pushTrail(ballX, ballY);
    ballX += ballDX * speedMultiplier;
    ballY += ballDY * speedMultiplier;

    if (speedMultiplier < 3.5) speedMultiplier += 0.0005;

    if (ballY <= 0 || ballY >= 63) {
        ballDY = -ballDY;
        speedMultiplier += 0.02;
    }

    // Colisiones con paletas
    if (ballX <= 10 && ballY >= paddleY && ballY <= paddleY + 16) {
        ballDX = -ballDX;
        ballX = 11;
        speedMultiplier += 0.05;
    }
    if (ballX >= 118 && ballY >= cpuY && ballY <= cpuY + 16) {
        ballDX = -ballDX;
        ballX = 117;
        speedMultiplier += 0.05;
    }

    // Puntuación
    if (ballX < 0) {
        scoreCPU++;
        ballX = 64; ballY = 32;
        ballDX = 2.0;
        speedMultiplier = 1.0;
    }
    if (ballX > 127) {
        scorePlayer++;
        ballX = 64; ballY = 32;
        ballDX = -2.0;
        speedMultiplier = 1.0;
    }

    // ===== DIBUJAR =====
    // Estela (más vieja = más tenue, simulada con menor tamaño)
    for (int i = 0; i < TRAIL_LEN; i++) {
        int age = (trailIdx - 1 - i + TRAIL_LEN) % TRAIL_LEN;
        int size = (i < 2) ? 2 : 1;
        int drawY = PONG_PLAY_TOP + ((int)trailY[age] * PONG_PLAY_H) / 64;
        u8g2.drawBox((int)trailX[age], drawY, size, size);
    }

    u8g2.drawFrame(0, PONG_PLAY_TOP, 128, PONG_PLAY_H);

    int playerY = PONG_PLAY_TOP + (paddleY * (PONG_PLAY_H - 16)) / 48;
    int cpuDrawY = PONG_PLAY_TOP + (cpuY * (PONG_PLAY_H - 16)) / 48;
    int ballDrawY = PONG_PLAY_TOP + ((int)ballY * (PONG_PLAY_H - 3)) / 63;

    u8g2.drawBox(4, playerY, 4, 16);
    u8g2.drawBox(120, cpuDrawY, 4, 16);

    u8g2.drawBox((int)ballX, ballDrawY, 3, 3);

    u8g2.setFont(u8g2_font_6x12_tr);
    char buf[12];
    snprintf(buf, sizeof(buf), "%d:%d", scorePlayer, scoreCPU);
    int scoreW = u8g2.getStrWidth(buf);
    u8g2.drawStr((128 - scoreW) / 2, 10, buf);

    u8g2.setFont(u8g2_font_5x7_tr);
    char vbuf[8];
    snprintf(vbuf, sizeof(vbuf), "x%.1f", speedMultiplier);
    u8g2.drawStr(2, 9, vbuf);
    UiTheme::drawProgressBar(u8g2, 98, 4, 26, 4, min(100, (int)(speedMultiplier * 28)));

    u8g2.sendBuffer();
}
