#ifndef CONDUCTIVITY_INSTRUMENT_H
#define CONDUCTIVITY_INSTRUMENT_H

/**
 * @file ConductivityInstrument.h
 * @brief Clase instanciable para conductividad con infra MQTT/NVS/Serial.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_ADS1X15.h>

#include "ConductivitySensor.h"

#include "mqtt_payload.h"
#include "system.h"

class ConductivityInstrument {
public:
  ConductivityInstrument(uint8_t pinOrChannel,
                         const char* id,
                         const char* metricKey,
                         AnalogInputType inputType,
                         Adafruit_ADS1115* ads = nullptr)
    : inputType_(inputType)
    , ads_(ads)
    , cal_(conductivityDefaultCalibration()) {
    strncpy(id_, id, sizeof(id_) - 1);
    id_[sizeof(id_) - 1] = '\0';
    strncpy(metricKey_, metricKey, sizeof(metricKey_) - 1);
    metricKey_[sizeof(metricKey_) - 1] = '\0';

    memset(&state_, 0, sizeof(state_));
    conductivitySetup(state_, pinOrChannel);
  }

  void registerHooks() {
    loadConfig_();

    mqttPayloadAddMetricsFiller([this](JsonObject& m) {
      m[metricKey_] = getReadingPPM();
      char adcKey[24];
      snprintf(adcKey, sizeof(adcKey), "%s_adc", id_);
      m[adcKey] = static_cast<int>(getFilteredADC());
    });

    mqttPayloadAddInstrumentFiller([this](JsonObject& i) {
      char refKey[24], factorKey[24];
      snprintf(refKey, sizeof(refKey), "%s_ref", id_);
      snprintf(factorKey, sizeof(factorKey), "%s_factor", id_);
      i[refKey] = cal_.referencePPM;
      i[factorKey] = cal_.factor;
    });

    systemAddStatusExtender([this]() {
      Serial.printf("Sensor [%s] : %.1f ppm | ADC %.0f\n", id_, getReadingPPM(), getFilteredADC());
    });

    registerSerialCommands_();
  }

  void setup() {
    if (inputType_ == ANALOG_ADS1115 && !ads_) {
      Serial.printf("[COND:%s] ERROR: ANALOG_ADS1115 sin puntero ADS\n", id_);
      return;
    }

    if (inputType_ == ANALOG_ESP32_ADC) {
      pinMode(state_.pinOrChannel, INPUT);
    }

    state_.initialized = true;
    Serial.printf("✓ Sensor Conductividad [%s] listo (%s, entrada=%u)\n",
                  id_,
                  inputType_ == ANALOG_ADS1115 ? "ADS1115" : "ESP32_ADC",
                  state_.pinOrChannel);
  }

  void update() {
    conductivityUpdate(state_, inputType_, ads_);
  }

  float       getReadingPPM() const { return conductivityGetPPM(state_, cal_); }
  float       getFilteredADC() const { return conductivityGetFilteredADC(state_); }
  const char* getId() const { return id_; }
  bool        isInitialized() const { return state_.initialized; }

  bool calibrate(float knownPPM, String* detail = nullptr) {
    if (!conductivityCalibrate(state_, cal_, knownPPM, detail)) {
      return false;
    }
    saveConfig_();
    return true;
  }

  bool setCalibration(float referencePPM, float factor, String* detail = nullptr) {
    if (referencePPM <= 0.0f || factor <= 0.0f) {
      if (detail) *detail = "reference/factor invalidos";
      return false;
    }
    cal_.referencePPM = referencePPM;
    cal_.factor = factor;
    saveConfig_();
    if (detail) {
      *detail = String("ref=") + String(referencePPM, 1) +
                " factor=" + String(factor, 4);
    }
    return true;
  }

private:
  char                      id_[16];
  char                      metricKey_[16];
  AnalogInputType           inputType_;
  Adafruit_ADS1115*         ads_;
  ConductivitySensorState   state_;
  ConductivityCalibration   cal_;

  void loadConfig_() {
    Preferences prefs;
    if (!prefs.begin(id_, true)) return;
    cal_.referencePPM = prefs.getFloat("ref", 1000.0f);
    cal_.factor = prefs.getFloat("factor", 1.0f);
    prefs.end();
  }

  void saveConfig_() {
    Preferences prefs;
    if (!prefs.begin(id_, false)) return;
    prefs.putFloat("ref", cal_.referencePPM);
    prefs.putFloat("factor", cal_.factor);
    prefs.end();
  }

  void registerSerialCommands_() {
    {
      char cmd[20]; snprintf(cmd, sizeof(cmd), "%s.read", id_);
      serialRegisterCommand(cmd, "Lectura instantanea de conductividad",
        [this](const String&) {
          Serial.printf("Lectura [%s] : %.1f ppm\n", id_, getReadingPPM());
          Serial.printf("ADC filtrado : %.0f\n", getFilteredADC());
          Serial.printf("Ref/factor   : %.1f / %.4f\n", cal_.referencePPM, cal_.factor);
        });
    }

    {
      char cmd[20]; snprintf(cmd, sizeof(cmd), "%s.cal", id_);
      serialRegisterCommand(cmd, "Calibrar con referencia ppm (uso: <id>.cal <ppm>)",
        [this](const String& line) {
          const int spacePos = line.indexOf(' ');
          if (spacePos < 0) {
            Serial.printf("Uso: %s.cal <ppm conocido>\n", id_);
            return;
          }

          const float known = line.substring(spacePos + 1).toFloat();
          String detail;
          if (calibrate(known, &detail)) Serial.println("✓ " + detail);
          else Serial.println("✗ " + detail);
        });
    }
  }

};

#endif // CONDUCTIVITY_INSTRUMENT_H
