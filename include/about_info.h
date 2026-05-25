#ifndef ABOUT_INFO_H
#define ABOUT_INFO_H

#include <Arduino.h>

void aboutEnter();   // NUEVO: llamado una vez por Host.launch()
void aboutLoop();
void aboutExit();    // NUEVO: llamado una vez por Host.shutdown()

#endif