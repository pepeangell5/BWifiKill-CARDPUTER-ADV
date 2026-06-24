#pragma once

#ifdef BWK_CARDPUTER_ADV

#include <Arduino.h>
#include <U8g2lib.h>

void cardputerBegin();
void cardputerUpdate();
void cardputerWaitForKeysReleased();
void cardputerPresent(U8G2& source);
int cardputerDigitalRead(uint8_t pin);
void cardputerPinMode(uint8_t pin, uint8_t mode);
void cardputerSetBrightness(uint8_t brightness);
uint8_t cardputerBrightness();
int cardputerBatteryLevel();
bool cardputerIsCharging();

using CardputerU8G2Base = U8G2_SSD1306_128X64_NONAME_F_HW_I2C;

class CardputerU8G2 : public CardputerU8G2Base {
public:
    CardputerU8G2(const u8g2_cb_t* rotation,
                  uint8_t reset = U8X8_PIN_NONE,
                  uint8_t clock = U8X8_PIN_NONE,
                  uint8_t data = U8X8_PIN_NONE)
        : CardputerU8G2Base(rotation, U8X8_PIN_NONE) {
        (void)reset;
        (void)clock;
        (void)data;
    }

    void begin() { cardputerBegin(); }
    void sendBuffer() { cardputerPresent(*this); }
};

// Keep the existing UI source compatible while routing it to the Cardputer.
#define U8G2_SSD1306_128X64_NONAME_F_HW_I2C CardputerU8G2
#define digitalRead(pin) cardputerDigitalRead(pin)
#define pinMode(pin, mode) cardputerPinMode(pin, mode)

#endif
