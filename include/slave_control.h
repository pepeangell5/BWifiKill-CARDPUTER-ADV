#ifndef SLAVE_CONTROL_H
#define SLAVE_CONTROL_H

#include <Arduino.h>

typedef struct struct_comando {
    bool estado;
} struct_comando;

// API existente
void slaveControlSetup();
void slaveControlLoop();

// API nueva opcional
void slaveControlEnter();
void slaveControlExit();

#endif