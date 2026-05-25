#ifndef MENU_CATALOG_H
#define MENU_CATALOG_H

#include <Arduino.h>

struct MenuCategory {
    const char* name;
    const char* subtitle;
    uint8_t icon;
    const uint8_t* apps;
    uint8_t count;
};

enum MenuCategoryIcon : uint8_t {
    MENU_ICON_WIFI = 0,
    MENU_ICON_RF,
    MENU_ICON_BLUETOOTH,
    MENU_ICON_WARNING,
    MENU_ICON_GAMES,
    MENU_ICON_SYSTEM
};

uint8_t menuCategoryCount();
const MenuCategory& menuCategoryAt(uint8_t index);
uint8_t menuCategoryAppIndex(uint8_t category, uint8_t position);

#endif
