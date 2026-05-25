#include "space_game.h"
#include <U8g2lib.h>
#include "input_manager.h"
#include "app_config.h"
#include "ui_theme.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

// =============================================================
// Entidades (mecánica sin cambios)
// =============================================================
struct Entity { float x, y; bool active; };

static Entity player;
static Entity bullet;
static Entity enemies[12];
static int enemyDir = 1;
static float enemySpeed = 0.4;
static int score = 0;
static bool gameOver = false;

// =============================================================
// Starfield de fondo (puro polish)
// =============================================================
static const int NUM_STARS = 14;
static float starX[NUM_STARS];
static int   starY[NUM_STARS];
static float starSpeed[NUM_STARS];

static void initStars() {
    for (int i = 0; i < NUM_STARS; i++) {
        starX[i] = random(0, 128);
        starY[i] = random(15, 60);
        // Tres "capas" de velocidad para sensación de profundidad
        int layer = i % 3;
        starSpeed[i] = (layer == 0) ? 0.4f : (layer == 1 ? 0.7f : 1.1f);
    }
}

static void updateAndDrawStars() {
    for (int i = 0; i < NUM_STARS; i++) {
        starX[i] -= starSpeed[i];
        if (starX[i] < 0) {
            starX[i] = 128 + random(0, 20);
            starY[i] = random(15, 60);
        }
        u8g2.drawPixel((int)starX[i], starY[i]);
    }
}

void spaceSetup() {
    player = {60.0, 55.0, true};
    bullet = {0, 0, false};
    score = 0;
    enemySpeed = 0.4;
    enemyDir = 1;
    gameOver = false;

    for (int i = 0; i < 12; i++) {
        enemies[i] = { (float)(20 + (i % 4) * 25), (float)(10 + (i / 4) * 12), true };
    }
    initStars();
    Input.resetAll();
}

void spaceLoop() {
    u8g2.clearBuffer();

    if (!gameOver) {
        // Starfield al fondo
        updateAndDrawStars();

        // ===== CONTROLES =====
        if (Input.held(BTN_ID_UP)   && player.x > 5)   player.x -= 3;
        if (Input.held(BTN_ID_DOWN) && player.x < 115) player.x += 3;

        if (Input.pressed(BTN_ID_OK) && !bullet.active) {
            bullet = {player.x + 3, player.y - 4, true};
        }

        // ===== FÍSICA =====
        if (bullet.active) {
            bullet.y -= 5;
            if (bullet.y < 0) bullet.active = false;
        }

        // ===== MOVIMIENTO ENEMIGOS =====
        bool touchEdge = false;
        for (int i = 0; i < 12; i++) {
            if (!enemies[i].active) continue;
            enemies[i].x += enemyDir * enemySpeed;
            if (enemies[i].x > 120 || enemies[i].x < 5) touchEdge = true;
            if (enemies[i].y >= player.y - 2) gameOver = true;
        }
        if (touchEdge) {
            enemyDir *= -1;
            for (int i = 0; i < 12; i++) {
                if (enemies[i].active) enemies[i].y += 5;
            }
            if (enemySpeed < 2.5) enemySpeed += 0.08;
        }

        // ===== COLISIONES =====
        if (bullet.active) {
            for (int i = 0; i < 12; i++) {
                if (enemies[i].active &&
                    bullet.x >= enemies[i].x && bullet.x <= enemies[i].x + 6 &&
                    bullet.y >= enemies[i].y && bullet.y <= enemies[i].y + 6) {
                    enemies[i].active = false;
                    bullet.active = false;
                    score += 10;
                }
            }
        }

        // ===== VICTORIA (RESPAWN) =====
        bool allDead = true;
        for (int i = 0; i < 12; i++) if (enemies[i].active) allDead = false;
        if (allDead) {
            // Pequeño bump al respawnear sin perder score
            int keepScore = score;
            spaceSetup();
            score = keepScore;
        }

        // ===== DIBUJAR =====
        // Nave del jugador
        u8g2.drawTriangle(player.x, player.y + 6, player.x + 6, player.y + 6, player.x + 3, player.y);
        // Toberas pequeñas
        if ((millis() / 80) % 2 == 0) {
            u8g2.drawPixel(player.x + 1, player.y + 7);
            u8g2.drawPixel(player.x + 5, player.y + 7);
        }

        if (bullet.active) u8g2.drawVLine(bullet.x, bullet.y, 4);

        for (int i = 0; i < 12; i++) {
            if (enemies[i].active) {
                u8g2.drawFrame(enemies[i].x, enemies[i].y, 6, 5);
                u8g2.drawPixel(enemies[i].x,     enemies[i].y);
                u8g2.drawPixel(enemies[i].x + 5, enemies[i].y);
                u8g2.drawPixel(enemies[i].x + 2, enemies[i].y + 6);
                u8g2.drawPixel(enemies[i].x + 3, enemies[i].y + 6);
            }
        }

        u8g2.drawHLine(0, 9, 128);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(2, 7);
        u8g2.print("SC "); u8g2.print(score);
        UiTheme::drawProgressBar(u8g2, 94, 2, 30, 4, min(100, (int)(enemySpeed * 36)));

    } else {
        // ===== GAME OVER =====
        // Stars de fondo congeladas
        for (int i = 0; i < NUM_STARS; i++) {
            u8g2.drawPixel((int)starX[i], starY[i]);
        }

        u8g2.setFont(u8g2_font_6x12_tr);
        const char* msg = "BREACHED";
        int w = u8g2.getStrWidth(msg);
        u8g2.drawStr((128 - w) / 2, 26, msg);

        u8g2.setFont(u8g2_font_5x7_tr);
        char line[20];
        snprintf(line, sizeof(line), "SCORE %d", score);
        int lw = u8g2.getStrWidth(line);
        u8g2.drawStr((128 - lw) / 2, 40, line);

        UiTheme::drawProgressBar(u8g2, 34, 55, 60, 4, (millis() / 12) % 100);

        if (Input.pressed(BTN_ID_OK)) spaceSetup();
    }

    u8g2.sendBuffer();
}
