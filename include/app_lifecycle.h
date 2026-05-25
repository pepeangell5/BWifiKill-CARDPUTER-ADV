#ifndef APP_LIFECYCLE_H
#define APP_LIFECYCLE_H

#include <Arduino.h>

// =============================================================
// AppHost
// -------------------------------------------------------------
// Reemplaza la cascada de if/else if (menu_index == X) que tienes
// hoy en main.cpp por un registro declarativo de apps.
//
// Cada app expone HASTA tres funciones:
//   - enter(): se llama una vez al entrar (lo que hoy es Setup)
//   - loop():  se llama cada frame mientras la app esté activa
//   - exit():  se llama una vez al salir, AQUÍ se liberan recursos
//              (WiFi.mode(WIFI_OFF), BLE.deinit(), nRF.powerDown,
//               esp_now_deinit, server.end(), etc.)
//
// El objetivo del exit() es eliminar los ESP.restart() que hoy
// tienes que hacer porque la salida queda en estado sucio.
// Cualquiera de los tres punteros puede ser nullptr.
// =============================================================

typedef void (*AppFn)();

struct App {
    const char* name;
    AppFn       enter;
    AppFn       loop;
    AppFn       exit;
};

class AppHost {
public:
    void registerApps(const App* list, uint8_t count);

    // Lanza la app del índice dado. Si había una corriendo,
    // primero llama a su exit() para limpieza, luego al enter()
    // de la nueva.
    void launch(uint8_t index);

    // Cierra la app actual (llama su exit()) y vuelve al menú.
    void shutdown();

    // Ejecuta el loop() de la app actual. Llamar cada frame.
    void tick();

    bool        isRunning()   const { return current >= 0; }
    int8_t      currentIndex() const { return current; }
    const char* currentName() const {
        return (current >= 0 && apps) ? apps[current].name : "none";
    }
    uint8_t     count() const { return appCount; }
    const App*  appAt(uint8_t i) const {
        return (i < appCount) ? &apps[i] : nullptr;
    }

private:
    const App* apps     = nullptr;
    uint8_t    appCount = 0;
    int8_t     current  = -1;
};

extern AppHost Host;

#endif // APP_LIFECYCLE_H