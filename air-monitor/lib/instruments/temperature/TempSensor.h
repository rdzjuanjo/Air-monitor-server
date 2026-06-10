#ifndef TEMP_SENSOR_LIB_H
#define TEMP_SENSOR_LIB_H

/**
 * @file TempSensor.h
 * @brief Driver instanciable de DS18B20 asincrono (sin infra).
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

class TempSensor {
public:
  explicit TempSensor(uint8_t pin)
    : oneWire_(pin)
    , dallas_(&oneWire_)
    , lastTempC_(NAN)
    , lastRequestMs_(0)
    , initialized_(false) {}

  void setup() {
    dallas_.begin();
    dallas_.setResolution(12);
    dallas_.setWaitForConversion(false);
    dallas_.requestTemperatures();
    lastRequestMs_ = millis();
    initialized_ = true;
  }

  void update() {
    if (!initialized_) return;

    const unsigned long now = millis();
    if (now - lastRequestMs_ < 750) return;

    const float t = dallas_.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t != -127.0f && !isnan(t) && !isinf(t)) {
      lastTempC_ = t;
    }

    dallas_.requestTemperatures();
    lastRequestMs_ = now;
  }

  float getTemperatureC() const { return lastTempC_; }
  bool  isInitialized() const { return initialized_; }
  bool  hasReading() const { return !isnan(lastTempC_); }

private:
  OneWire            oneWire_;
  DallasTemperature  dallas_;
  float              lastTempC_;
  unsigned long      lastRequestMs_;
  bool               initialized_;
};

#endif // TEMP_SENSOR_LIB_H
