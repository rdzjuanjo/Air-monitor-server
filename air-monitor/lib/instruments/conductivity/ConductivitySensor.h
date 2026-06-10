#ifndef CONDUCTIVITY_SENSOR_H
#define CONDUCTIVITY_SENSOR_H

/**
 * @file ConductivitySensor.h
 * @brief Driver instanciable para lectura y conversion a ppm (sin infra).
 */

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

#include "../AnalogInputType.h"

struct ConductivityCalibration {
  float referencePPM;
  float factor;
};

struct ConductivitySensorState {
  uint8_t pinOrChannel;
  float   filteredADC;
  bool    firstReading;
  bool    initialized;
};

static constexpr float COND_FILTER_ALPHA = 0.05f;

inline ConductivityCalibration conductivityDefaultCalibration() {
  return ConductivityCalibration{1000.0f, 1.0f};
}

inline bool conductivityIsValidADC(float adc) {
  return !isnan(adc) && !isinf(adc) && adc > 0.0f;
}

inline void conductivitySetup(ConductivitySensorState& state, uint8_t pinOrChannel) {
  state.pinOrChannel = pinOrChannel;
  state.filteredADC  = 0.0f;
  state.firstReading = true;
  state.initialized  = true;
}

inline bool conductivityUpdate(ConductivitySensorState& state,
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

  if (!conductivityIsValidADC(raw)) return false;

  if (state.firstReading) {
    state.filteredADC = raw;
    state.firstReading = false;
  } else {
    state.filteredADC = COND_FILTER_ALPHA * raw + (1.0f - COND_FILTER_ALPHA) * state.filteredADC;
  }
  return true;
}

inline float conductivityGetFilteredADC(const ConductivitySensorState& state) {
  return state.filteredADC;
}

inline float conductivityGetPPM(const ConductivitySensorState& state,
                                const ConductivityCalibration& cal) {
  return state.filteredADC * cal.factor;
}

inline bool conductivityCalibrate(ConductivitySensorState& state,
                                  ConductivityCalibration& cal,
                                  float knownPPM,
                                  String* detail = nullptr) {
  if (knownPPM <= 0.0f) {
    if (detail) *detail = "knownPPM invalido";
    return false;
  }
  if (!conductivityIsValidADC(state.filteredADC)) {
    if (detail) *detail = "lectura ADC invalida";
    return false;
  }

  cal.referencePPM = knownPPM;
  cal.factor = knownPPM / state.filteredADC;
  if (detail) {
    *detail = String("ADC=") + String(state.filteredADC, 1) +
              " => factor=" + String(cal.factor, 4);
  }
  return true;
}

#endif // CONDUCTIVITY_SENSOR_H
