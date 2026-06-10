#ifndef TEMP_INSTRUMENT_H
#define TEMP_INSTRUMENT_H

/**
 * @file TempInstrument.h
 * @brief Clase instanciable para temperatura DS18B20 con infra MQTT/NVS/Serial.
 */

#include <Arduino.h>
#include <ArduinoJson.h>

#include "TempSensor.h"

#include "mqtt_payload.h"
#include "system.h"

class TempInstrument {
public:
  TempInstrument(uint8_t pin, const char* id, const char* metricKey)
    : sensor_(pin) {
    strncpy(id_, id, sizeof(id_) - 1);
    id_[sizeof(id_) - 1] = '\0';
    strncpy(metricKey_, metricKey, sizeof(metricKey_) - 1);
    metricKey_[sizeof(metricKey_) - 1] = '\0';
  }

  void registerHooks() {
    mqttPayloadAddMetricsFiller([this](JsonObject& m) {
      const float t = getReadingC();
      if (!isnan(t)) {
        m[metricKey_] = t;
      }
    });

    mqttPayloadAddInstrumentFiller([this](JsonObject& i) {
      char typeKey[24];
      snprintf(typeKey, sizeof(typeKey), "%s_type", id_);
      i[typeKey] = "ds18b20";
    });

    systemAddStatusExtender([this]() {
      const float t = getReadingC();
      if (isnan(t)) {
        Serial.printf("Sensor [%s] : sin lectura valida\n", id_);
      } else {
        Serial.printf("Sensor [%s] : %.2f C\n", id_, t);
      }
    });

    registerSerialCommands_();
  }

  void setup() {
    sensor_.setup();
    Serial.printf("✓ Sensor Temperatura [%s] listo\n", id_);
  }

  void update() {
    sensor_.update();
  }

  float       getReadingC() const { return sensor_.getTemperatureC(); }
  bool        hasReading() const { return sensor_.hasReading(); }
  const char* getId() const { return id_; }

private:
  char       id_[16];
  char       metricKey_[16];
  TempSensor sensor_;

  void registerSerialCommands_() {
    char cmd[20]; snprintf(cmd, sizeof(cmd), "%s.read", id_);
    serialRegisterCommand(cmd, "Lectura instantanea de temperatura",
      [this](const String&) {
        const float t = getReadingC();
        if (isnan(t)) {
          Serial.printf("Lectura [%s] : sin dato\n", id_);
        } else {
          Serial.printf("Lectura [%s] : %.2f C\n", id_, t);
        }
      });
  }
};

#endif // TEMP_INSTRUMENT_H
