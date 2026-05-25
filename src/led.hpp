#ifndef BLE_STANDALONE_LED_HPP
#define BLE_STANDALONE_LED_HPP

enum LEDMode { OFF = 0, FLASH = 1, ON = 2 };

static const LEDMode stateTable[9][2] = {
    { OFF,   OFF   },
    { OFF,   FLASH },
    { OFF,   ON    },
    { FLASH, OFF   },
    { FLASH, FLASH },
    { FLASH, ON    },
    { ON,    OFF   },
    { ON,    FLASH },
    { ON,    ON    }
};

enum LEDState {
    LEFT_OFF_RIGHT_OFF = 0,
    LEFT_OFF_RIGHT_FLASH,
    LEFT_OFF_RIGHT_ON,
    LEFT_FLASH_RIGHT_OFF,
    LEFT_FLASH_RIGHT_FLASH,
    LEFT_FLASH_RIGHT_ON,
    LEFT_ON_RIGHT_OFF,
    LEFT_ON_RIGHT_FLASH,
    LEFT_ON_RIGHT_ON
};

#endif
