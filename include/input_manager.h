#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>
#include "app_config.h"

// =============================================================
// InputManager
// -------------------------------------------------------------
// Reemplaza el patrón:
//     if (digitalRead(BTN_UP) == LOW) { ...; delay(200); }
// por una API basada en eventos, sin delay() y con debounce real.
//
// USO TÍPICO en cada loop de app:
//
//   Input.update();                              // una vez por frame
//   if (Input.pressed(BTN_ID_OK))   { ... }
//   if (Input.repeating(BTN_ID_UP)) { menu_idx--; }
//   if (Input.longPressed(BTN_ID_BACK)) { hardExit(); }
//
// "repeating" = primer pulso + auto-repeat sostenido (ideal menús).
// "pressed"   = solo el flanco de bajada (ideal confirmaciones).
// "longPressed" = se dispara UNA sola vez al cruzar el umbral.
//
// CONSUME:
//   Cuando un módulo con sub-vistas (ej. wifiscan en detalle)
//   maneja un evento y NO quiere que main.cpp también lo procese,
//   llama Input.consume(BTN_ID_BACK) para marcarlo como atendido
//   en este frame. La siguiente call a Input.update() lo limpia.
// =============================================================

enum ButtonId : uint8_t {
    BTN_ID_UP = 0,
    BTN_ID_DOWN,
    BTN_ID_OK,
    BTN_ID_BACK,
    BTN_ID_AUX,
    BTN_COUNT
};

enum ButtonEvent : uint8_t {
    EVT_NONE = 0,
    EVT_PRESSED,      // Flanco: acaba de pulsarse
    EVT_RELEASED,     // Flanco: acaba de soltarse
    EVT_REPEAT,       // Auto-repeat mientras se mantiene
    EVT_LONG_PRESS    // Sostén pasó el umbral (dispara una sola vez)
};

class InputManager {
public:
    void begin();
    void update();

    // Consultas
    ButtonEvent event(ButtonId id) const   { return events[id]; }
    bool        held(ButtonId id) const    { return stableState[id]; }
    uint32_t    heldMs(ButtonId id) const;

    // Conveniencias (lo que vas a usar el 95% del tiempo)
    bool pressed(ButtonId id) const     { return events[id] == EVT_PRESSED; }
    bool released(ButtonId id) const    { return events[id] == EVT_RELEASED; }
    bool repeating(ButtonId id) const   {
        return events[id] == EVT_PRESSED || events[id] == EVT_REPEAT;
    }
    bool longPressed(ButtonId id) const { return events[id] == EVT_LONG_PRESS; }

    // Marca un evento como atendido para que otro consumidor (ej. main.cpp)
    // lo vea como EVT_NONE durante este frame. Útil en módulos con sub-vistas.
    void consume(ButtonId id);

    // Limpia todo el estado. Llamar en appEnter() para que un botón
    // sostenido al salir de la app anterior no se cuele en la nueva.
    void resetAll();

private:
    static const uint8_t PINS[BTN_COUNT];

    bool        stableState[BTN_COUNT]  = {false};
    bool        rawPrev[BTN_COUNT]      = {false};
    uint32_t    lastEdgeMs[BTN_COUNT]   = {0};
    uint32_t    pressStartMs[BTN_COUNT] = {0};
    uint32_t    lastRepeatMs[BTN_COUNT] = {0};
    bool        longFired[BTN_COUNT]    = {false};
    ButtonEvent events[BTN_COUNT]       = {EVT_NONE};
};

extern InputManager Input;

#endif // INPUT_MANAGER_H