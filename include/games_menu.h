#ifndef GAMES_MENU_H
#define GAMES_MENU_H

#include <Arduino.h>

// API existente (no romper main.cpp)
void gamesLoop();
extern bool inGame;

// API nueva opcional para integrarse con AppHost
void gamesEnter();
void gamesExit();

#endif