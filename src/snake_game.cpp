#include "snake_game.h"
#include <U8g2lib.h>
#include "input_manager.h"
#include "app_config.h"
#include "ui_theme.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

struct Point { int8_t x, y; };
static Point snake[25];
static Point food;
static int8_t snake_len = 3;
static int8_t dirX = 1, dirY = 0;
static int8_t pendingDirX = 1, pendingDirY = 0;
static bool gameOver = false;
static unsigned long lastMove = 0;
static int currentScore = 0;
RTC_DATA_ATTR static int highScore = 0;

void resetSnake() {
    snake_len = 3;
    currentScore = 0;
    snake[0] = {10, 8};
    snake[1] = {9, 8};
    snake[2] = {8, 8};
    dirX = 1;
    dirY = 0;
    pendingDirX = 1;
    pendingDirY = 0;
    food = {(int8_t)random(2, 29), (int8_t)random(3, 14)};
    gameOver = false;
    Input.resetAll();
}

static void drawSnakeGameOver() {
    u8g2.clearBuffer();

    for (int x = 10; x < 118; x += 12) {
        if (((millis() / 140) + x) % 3 == 0) u8g2.drawPixel(x, 10);
    }

    if ((millis() / 400) % 2 == 0) {
        u8g2.drawFrame(10, 8, 108, 48);
    }

    u8g2.setFont(u8g2_font_6x12_tr);
    const char* msg = "GAME OVER";
    int w = u8g2.getStrWidth(msg);
    u8g2.drawStr((128 - w) / 2, 22, msg);

    u8g2.setFont(u8g2_font_5x7_tr);
    char line[20];
    snprintf(line, sizeof(line), "SCORE %d", currentScore);
    int lw = u8g2.getStrWidth(line);
    u8g2.drawStr((128 - lw) / 2, 35, line);

    snprintf(line, sizeof(line), "BEST  %d", highScore);
    lw = u8g2.getStrWidth(line);
    u8g2.drawStr((128 - lw) / 2, 45, line);

    UiTheme::drawProgressBar(u8g2, 34, 58, 60, 4, (millis() / 12) % 100);
    u8g2.sendBuffer();
}

static void drawSnakeHud() {
    char scoreBuf[12];
    u8g2.setFont(u8g2_font_4x6_tf);

    snprintf(scoreBuf, sizeof(scoreBuf), "SC %d", currentScore);
    u8g2.drawStr(2, 6, scoreBuf);

    snprintf(scoreBuf, sizeof(scoreBuf), "HI %d", highScore);
    int hiW = u8g2.getStrWidth(scoreBuf);
    u8g2.drawStr(126 - hiW, 6, scoreBuf);

    for (int x = 36; x < 92; x += 7) {
        if (((millis() / 180) + x) % 2 == 0) u8g2.drawPixel(x, 3);
    }
}

void snakeLoop() {
    if (gameOver) {
        drawSnakeGameOver();
        if (Input.pressed(BTN_ID_OK)) {
            resetSnake();
        }
        return;
    }

    if (Input.pressed(BTN_ID_UP)) {
        pendingDirX = -dirY;
        pendingDirY = dirX;
    }
    if (Input.pressed(BTN_ID_DOWN)) {
        pendingDirX = dirY;
        pendingDirY = -dirX;
    }

    if (millis() - lastMove > 130) {
        lastMove = millis();
        dirX = pendingDirX;
        dirY = pendingDirY;

        for (int i = snake_len - 1; i > 0; i--) snake[i] = snake[i - 1];
        snake[0].x += dirX;
        snake[0].y += dirY;

        if (snake[0].x < 0 || snake[0].x > 31 || snake[0].y < 2 || snake[0].y > 15) {
            gameOver = true;
        }

        for (int i = 1; i < snake_len; i++) {
            if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) gameOver = true;
        }

        if (snake[0].x == food.x && snake[0].y == food.y) {
            currentScore += 10;
            if (currentScore > highScore) highScore = currentScore;
            if (snake_len < 25) snake_len++;
            food = {(int8_t)random(0, 31), (int8_t)random(2, 15)};
        }
    }

    u8g2.clearBuffer();
    drawSnakeHud();
    u8g2.drawFrame(0, 8, 128, 56);

    for (int i = 0; i < snake_len; i++) {
        if (i == 0) {
            u8g2.drawBox(snake[i].x * 4, snake[i].y * 4, 4, 4);
            u8g2.setDrawColor(0);
            u8g2.drawPixel(snake[i].x * 4 + 2, snake[i].y * 4 + 1);
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawBox(snake[i].x * 4 + 1, snake[i].y * 4 + 1, 3, 3);
        }
    }

    int r = ((millis() / 200) % 2) ? 2 : 1;
    u8g2.drawDisc(food.x * 4 + 2, food.y * 4 + 2, r);

    u8g2.sendBuffer();
}
