#ifndef ADS1115_BUS_H
#define ADS1115_BUS_H

/**
 * @file ADS1115Bus.h
 * @brief Helper compartido para inicializar y exponer un ADS1115 unico.
 */

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

class ADS1115Bus {
public:
  static Adafruit_ADS1115& instance() {
    static Adafruit_ADS1115 ads;
    return ads;
  }

  static bool begin(uint8_t address = 0x48,
                    adsGain_t gain = GAIN_TWOTHIRDS,
                    bool logToSerial = true) {
    static bool initialized = false;
    static bool lastBeginOk = false;

    if (initialized) {
      return lastBeginOk;
    }

    initialized = true;
    lastBeginOk = instance().begin(address);

    if (!lastBeginOk) {
      if (logToSerial) {
        Serial.printf("[ADS1115] ERROR: no se pudo inicializar en 0x%02X\n", address);
      }
      return false;
    }

    instance().setGain(gain);

    if (logToSerial) {
      Serial.printf("[ADS1115] OK en 0x%02X\n", address);
    }
    return true;
  }
};

#endif // ADS1115_BUS_H
