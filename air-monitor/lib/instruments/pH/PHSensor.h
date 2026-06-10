#ifndef PH_SENSOR_H
#define PH_SENSOR_H

/**
 * @file PHSensor.h
 * @brief Driver instanciable para lectura y calculo de pH (sin dependencias de infra).
 */

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

#include "../AnalogInputType.h"

struct PHCalibration {
  int  adcPH4;
  int  adcPH7;
  int  adcPH10;
  bool directa;
};

struct PHSensorState {
  uint8_t pinOrChannel;
  float   filteredADC;
  bool    firstReading;
  bool    initialized;
};

static constexpr float PH_FILTER_ALPHA = 0.05f;
static constexpr int   PH_DEFAULT_ADC4 = 200;
static constexpr int   PH_DEFAULT_ADC7 = 300;
static constexpr int   PH_DEFAULT_ADC10 = 400;

inline PHCalibration phDefaultCalibration() {
  return PHCalibration{PH_DEFAULT_ADC4, PH_DEFAULT_ADC7, PH_DEFAULT_ADC10, true};
}

inline bool phIsValidADC(float adc) {
  return !isnan(adc) && !isinf(adc) && adc >= 0.0f;
}

inline bool phDetectRelation(const PHCalibration& cal) {
  if (cal.adcPH4 < cal.adcPH7 && cal.adcPH7 < cal.adcPH10) return true;
  if (cal.adcPH4 > cal.adcPH7 && cal.adcPH7 > cal.adcPH10) return false;
  return true;
}

inline float phCalculatePH(float adcVal, const PHCalibration& cal) {
  const float denomLo = static_cast<float>(cal.adcPH7 - cal.adcPH4);
  const float denomHi = static_cast<float>(cal.adcPH10 - cal.adcPH7);

  if (denomLo == 0.0f || denomHi == 0.0f) {
    return 7.0f;
  }

  const bool inLowRange = cal.directa
    ? (adcVal <= static_cast<float>(cal.adcPH7))
    : (adcVal >= static_cast<float>(cal.adcPH7));

  float pH;
  if (inLowRange) {
    pH = 7.0f + (adcVal - static_cast<float>(cal.adcPH7)) * 3.0f / denomLo;
  } else {
    pH = 7.0f + (adcVal - static_cast<float>(cal.adcPH7)) * 3.0f / denomHi;
  }

  return constrain(pH, 0.0f, 14.0f);
}

inline void phSetup(PHSensorState& state, uint8_t pinOrChannel) {
  state.pinOrChannel = pinOrChannel;
  state.filteredADC  = 0.0f;
  state.firstReading = true;
  state.initialized  = true;
}

inline bool phUpdate(PHSensorState& state,
                     AnalogInputType inputType,
                     Adafruit_ADS1115* ads) {
  if (!state.initialized) return false;

  float raw = NAN;
  if (inputType == ANALOG_ADS1115) {
    if (!ads) return false;
    raw = static_cast<float>(ads->readADC_SingleEnded(state.pinOrChannel));
  } else {
    raw = static_cast<float>(analogRead(state.pinOrChannel));
  }

  if (!phIsValidADC(raw)) return false;

  if (state.firstReading) {
    state.filteredADC = raw;
    state.firstReading = false;
  } else {
    state.filteredADC = PH_FILTER_ALPHA * raw + (1.0f - PH_FILTER_ALPHA) * state.filteredADC;
  }
  return true;
}

inline float phGetFilteredADC(const PHSensorState& state) {
  return state.filteredADC;
}

inline float phGetReading(const PHSensorState& state, const PHCalibration& cal) {
  return phCalculatePH(state.filteredADC, cal);
}

#endif // PH_SENSOR_H
