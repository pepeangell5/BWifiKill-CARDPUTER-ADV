#ifndef SPECTROGRAPH_H
#define SPECTROGRAPH_H

#include <Arduino.h>
#include <U8g2lib.h>

// Ciclo de vida nuevo
void spectrographEnter();
void spectrographLoop();
void spectrographExit();

// Backward compat: alias del enter
void spectrographSetup();

#endif