#ifndef PH_INSTRUMENT_H
#define PH_INSTRUMENT_H

/**
 * @file PHInstrument.h
 * @brief Clase instanciable para sensor de pH con infra MQTT/NVS/Serial.
 *
 * Ejemplo de uso:
 *   Adafruit_ADS1115 ads;
 *   PHInstrument ph(0, "ph1", "PH", ANALOG_ADS1115, &ads);
 *
 *   void setup() {
 *     registerNvsHooks();
 *     ads.begin(0x48);
 *     ph.registerHooks();
 *     libSetup([]() { ph.setup(); });
 *   }
 *   void loop() {
 *     ph.update();
 *     libLoop();
 *   }
 */

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_ADS1X15.h>

#include "PHSensor.h"

#include "mqtt_payload.h"
#include "system.h"

class PHInstrument {
public:
  PHInstrument(uint8_t pinOrChannel,
               const char* id,
               const char* metricKey,
               AnalogInputType inputType,
               Adafruit_ADS1115* ads = nullptr)
    : inputType_(inputType)
    , ads_(ads)
    , cal_(phDefaultCalibration()) {
    strncpy(id_, id, sizeof(id_) - 1);
    id_[sizeof(id_) - 1] = '\0';
    strncpy(metricKey_, metricKey, sizeof(metricKey_) - 1);
    metricKey_[sizeof(metricKey_) - 1] = '\0';

    memset(&state_, 0, sizeof(state_));
    phSetup(state_, pinOrChannel);
  }

  void registerHooks() {
    loadConfig_();

    mqttPayloadAddMetricsFiller([this](JsonObject& m) {
      m[metricKey_] = getReading();
      char adcKey[24];
      snprintf(adcKey, sizeof(adcKey), "%s_adc", id_);
      m[adcKey] = static_cast<int>(getFilteredADC());
    });

    mqttPayloadAddInstrumentFiller([this](JsonObject& i) {
      char p4Key[24], p7Key[24], p10Key[24], dirKey[24];
      snprintf(p4Key, sizeof(p4Key), "%s_ph4", id_);
      snprintf(p7Key, sizeof(p7Key), "%s_ph7", id_);
      snprintf(p10Key, sizeof(p10Key), "%s_ph10", id_);
      snprintf(dirKey, sizeof(dirKey), "%s_direct", id_);
      i[p4Key] = cal_.adcPH4;
      i[p7Key] = cal_.adcPH7;
      i[p10Key] = cal_.adcPH10;
      i[dirKey] = cal_.directa;
    });

    systemAddStatusExtender([this]() {
      Serial.printf("Sensor [%s] : pH %.2f | ADC %.0f\n", id_, getReading(), getFilteredADC());
    });

    registerSerialCommands_();
  }

  void setup() {
    if (inputType_ == ANALOG_ADS1115 && !ads_) {
      Serial.printf("[pH:%s] ERROR: ANALOG_ADS1115 sin puntero ADS\n", id_);
      return;
    }

    if (inputType_ == ANALOG_ESP32_ADC) {
      pinMode(state_.pinOrChannel, INPUT);
    }

    state_.initialized = true;
    Serial.printf("✓ Sensor pH [%s] listo (%s, entrada=%u)\n",
                  id_,
                  inputType_ == ANALOG_ADS1115 ? "ADS1115" : "ESP32_ADC",
                  state_.pinOrChannel);
  }

  void update() {
    phUpdate(state_, inputType_, ads_);
  }

  float       getReading() const { return phGetReading(state_, cal_); }
  float       getFilteredADC() const { return phGetFilteredADC(state_); }
  const char* getId() const { return id_; }
  bool        isInitialized() const { return state_.initialized; }
  PHCalibration getCalibration() const { return cal_; }

  bool calibratePoint(uint8_t point, String* detail = nullptr) {
    if (state_.firstReading) {
      if (detail) *detail = "Aun no hay lectura ADC";
      return false;
    }

    return setCalibrationADC_(point, static_cast<int>(state_.filteredADC), detail);
  }

  bool setCalibrationADC(uint8_t point, int adc, String* detail = nullptr) {
    return setCalibrationADC_(point, adc, detail);
  }

private:
  char             id_[16];
  char             metricKey_[16];
  AnalogInputType  inputType_;
  Adafruit_ADS1115* ads_;
  PHSensorState    state_;
  PHCalibration    cal_;

  void loadConfig_() {
    Preferences prefs;
    if (!prefs.begin(id_, true)) return;
    cal_.adcPH4  = prefs.getInt("ph4", PH_DEFAULT_ADC4);
    cal_.adcPH7  = prefs.getInt("ph7", PH_DEFAULT_ADC7);
    cal_.adcPH10 = prefs.getInt("ph10", PH_DEFAULT_ADC10);
    cal_.directa = prefs.getBool("direct", true);
    prefs.end();

    cal_.directa = phDetectRelation(cal_);
  }

  void saveConfig_() {
    Preferences prefs;
    if (!prefs.begin(id_, false)) return;
    prefs.putInt("ph4", cal_.adcPH4);
    prefs.putInt("ph7", cal_.adcPH7);
    prefs.putInt("ph10", cal_.adcPH10);
    prefs.putBool("direct", cal_.directa);
    prefs.end();
  }

  bool setCalibrationADC_(uint8_t point, int adc, String* detail = nullptr) {
    if (adc < 0) {
      if (detail) *detail = "ADC invalido";
      return false;
    }

    switch (point) {
      case 4: cal_.adcPH4 = adc; break;
      case 7: cal_.adcPH7 = adc; break;
      case 10: cal_.adcPH10 = adc; break;
      default:
        if (detail) *detail = "punto invalido (use 4,7,10)";
        return false;
    }

    cal_.directa = phDetectRelation(cal_);
    saveConfig_();

    if (detail) {
      *detail = String("pH") + String(point) + "=ADC " + String(adc) +
                " (directa=" + String(cal_.directa ? "si" : "no") + ")";
    }
    return true;
  }

  void registerSerialCommands_() {
    {
      char cmd[20]; snprintf(cmd, sizeof(cmd), "%s.read", id_);
      serialRegisterCommand(cmd, "Lectura instantanea de pH",
        [this](const String&) {
          Serial.printf("Lectura [%s] : pH %.2f\n", id_, getReading());
          Serial.printf("ADC filtrado : %.0f\n", getFilteredADC());
          Serial.printf("Cal pH4/7/10 : %d / %d / %d (%s)\n",
                        cal_.adcPH4, cal_.adcPH7, cal_.adcPH10,
                        cal_.directa ? "directa" : "inversa");
        });
    }

    {
      char cmd[20]; snprintf(cmd, sizeof(cmd), "%s.cal4", id_);
      serialRegisterCommand(cmd, "Guarda ADC actual como pH4",
        [this](const String&) {
          String detail;
          if (calibratePoint(4, &detail)) Serial.println("✓ " + detail);
          else Serial.println("✗ " + detail);
        });
    }

    {
      char cmd[20]; snprintf(cmd, sizeof(cmd), "%s.cal7", id_);
      serialRegisterCommand(cmd, "Guarda ADC actual como pH7",
        [this](const String&) {
          String detail;
          if (calibratePoint(7, &detail)) Serial.println("✓ " + detail);
          else Serial.println("✗ " + detail);
        });
    }

    {
      char cmd[20]; snprintf(cmd, sizeof(cmd), "%s.cal10", id_);
      serialRegisterCommand(cmd, "Guarda ADC actual como pH10",
        [this](const String&) {
          String detail;
          if (calibratePoint(10, &detail)) Serial.println("✓ " + detail);
          else Serial.println("✗ " + detail);
        });
    }
  }

};

#endif // PH_INSTRUMENT_H
