#ifndef PACKET_MONITOR_H
#define PACKET_MONITOR_H

#include <Arduino.h>

// Ciclo de vida nuevo
void monitorEnter();
void monitorLoop();
void monitorExit();

// Backward compat: alias del enter
void monitorSetup();

#endif