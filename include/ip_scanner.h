#ifndef IP_SCANNER_H
#define IP_SCANNER_H

#include <Arduino.h>

// Ciclo de vida nuevo
void ipScannerEnter();
void ipScannerLoop();
void ipScannerExit();

// Backward compat: alias del enter
void ipScannerSetup();

#endif