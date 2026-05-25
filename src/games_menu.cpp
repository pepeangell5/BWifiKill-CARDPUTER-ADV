#include "games_menu.h"
#include <U8g2lib.h>
#include "snake_game.h"
#include "pong_game.h"
#include "flappy_game.h"
#include "space_game.h"
#include "dino_game.h"
#include "input_manager.h"
#include "app_config.h"
#include "ui_theme.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static int game_index = 0;
static const int TOTAL_GAMES = 5;
bool inGame = false;

static const char* game_labels[] = {"SNAKE", "PONG", "FLAPPY", "INVADERS", "DINO RUN"};
static const char* game_tags[] = {"GRID", "VERSUS", "WINGS", "LASER", "RUNNER"};

static void drawArcadeHeader(uint32_t ms) {
    char status[8];
    snprintf(status, sizeof(status), "%d/%d", game_index + 1, TOTAL_GAMES);
    UiTheme::drawHeader(u8g2, "ARCADE", status);
    UiTheme::drawMiniWave(u8g2, 2, 15, (ms / 120) & 0xFF);
    UiTheme::drawMiniWave(u8g2, 114, 15, ((ms / 120) + 4) & 0xFF);
}

static void drawGamePreview(int idx, int x, int y, uint32_t ms) {
    u8g2.drawFrame(x, y, 34, 24);

    switch (idx) {
        case 0:
            u8g2.drawBox(x + 7, y + 7, 4, 4);
            u8g2.drawBox(x + 11, y + 7, 3, 3);
            u8g2.drawBox(x + 14, y + 7, 3, 3);
            u8g2.drawDisc(x + 25, y + 15, ((ms / 220) % 2) + 1);
            break;
        case 1:
            u8g2.drawVLine(x + 6, y + 5, 12);
            u8g2.drawVLine(x + 27, y + 7, 12);
            u8g2.drawBox(x + 16 + ((ms / 180) % 3), y + 11, 3, 3);
            break;
        case 2:
            u8g2.drawBox(x + 9, y + 9, 5, 4);
            u8g2.drawPixel(x + 8, y + 10 + ((ms / 120) % 2));
            u8g2.drawBox(x + 24, y + 2, 5, 8);
            u8g2.drawBox(x + 24, y + 16, 5, 7);
            break;
        case 3:
            u8g2.drawTriangle(x + 16, y + 18, x + 21, y + 18, x + 18, y + 12);
            if ((ms / 140) % 2 == 0) u8g2.drawVLine(x + 18, y + 5, 5);
            for (int i = 0; i < 3; i++) u8g2.drawFrame(x + 7 + i * 8, y + 4, 5, 4);
            break;
        default:
            u8g2.drawBox(x + 8, y + 12, 8, 6);
            u8g2.drawBox(x + 13, y + 8, 6, 4);
            u8g2.drawPixel(x + 17, y + 9);
            u8g2.drawVLine(x + 25, y + 7, 12);
            break;
    }
}

static void drawGamesMenu(uint32_t ms) {
    u8g2.clearBuffer();
    drawArcadeHeader(ms);

    drawGamePreview(game_index, 6, 25, ms);

    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.setCursor(46, 34);
    u8g2.print(game_labels[game_index]);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(47, 45);
    u8g2.print(game_tags[game_index]);

    int prev = (game_index + TOTAL_GAMES - 1) % TOTAL_GAMES;
    int next = (game_index + 1) % TOTAL_GAMES;
    u8g2.drawStr(4, 61, game_labels[prev]);
    int nw = u8g2.getStrWidth(game_labels[next]);
    u8g2.drawStr(124 - nw, 61, game_labels[next]);

    int dotX = 50;
    for (int i = 0; i < TOTAL_GAMES; i++) {
        if (i == game_index) {
            u8g2.drawBox(dotX + i * 6, 56, 4, 4);
        } else {
            u8g2.drawFrame(dotX + i * 6, 56, 4, 4);
        }
    }

    u8g2.sendBuffer();
}

void gamesEnter() {
    game_index = 0;
    inGame = false;
    Input.resetAll();
}

void gamesExit() {
    inGame = false;
    u8g2.setDrawColor(1);
}

void gamesLoop() {
    if (!inGame) {
        drawGamesMenu(millis());

        if (Input.repeating(BTN_ID_UP)) {
            game_index--;
            if (game_index < 0) game_index = TOTAL_GAMES - 1;
        }
        if (Input.repeating(BTN_ID_DOWN)) {
            game_index++;
            if (game_index >= TOTAL_GAMES) game_index = 0;
        }

        if (Input.pressed(BTN_ID_OK)) {
            inGame = true;
            switch (game_index) {
                case 0: resetSnake();   break;
                case 1: pongSetup();    break;
                case 2: flappySetup();  break;
                case 3: spaceSetup();   break;
                case 4: dinoSetup();    break;
            }
            Input.resetAll();
        }
    } else {
        switch (game_index) {
            case 0: snakeLoop();  break;
            case 1: pongLoop();   break;
            case 2: flappyLoop(); break;
            case 3: spaceLoop();  break;
            case 4: dinoLoop();   break;
        }

        if (Input.pressed(BTN_ID_BACK)) {
            inGame = false;
            u8g2.setDrawColor(1);
            Input.resetAll();
        }
    }
}
