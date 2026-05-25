#ifndef WIFISCAN_H
#define WIFISCAN_H

#include <Arduino.h>

// Ciclo de vida nuevo (patrón enter/loop/exit)
void wifiscanEnter();
void wifiscanLoop();
void wifiscanExit();

// Backward compat: nombre antiguo, queda como alias de Enter
void wifiscanSetup();

#endif