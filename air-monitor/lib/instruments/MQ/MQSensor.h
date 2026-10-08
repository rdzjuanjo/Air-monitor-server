#ifndef MQ_SENSOR_H
#define MQ_SENSOR_H

/**
 * @file MQSensor.h
 * @brief Driver instanciable genérico para sensores de la familia MQ.
 *
 * [REUSABLE] No tiene estado global. Toda la lógica opera sobre
 * MQSensorState (estado de ejecución), pasado por referencia.
 * Sin calibración ni curva de PPM: solo reporta el ADC filtrado (EMA).
 *
 * Uso típico (ver MQInstrument.h para la integración completa):
 *   MQSensorState s;
 *   s.pin = 34;
 *   mqSetup(s);            // en setup()
 *   mqUpdate(s);           // en loop()
 *   float adc = mqGetFilteredADC(s);
 */

#include <Arduino.h>

// ============================================================================
// TIPOS PÚBLICOS
// ============================================================================

/**
 * @brief Estado de ejecución de una instancia de sensor.
 *
 * No tiene dependencias de inicialización (cero-inicializable).
 * Se debe fijar `pin` antes de llamar a mqSetup().
 */
struct MQSensorState {
  uint8_t       pin;                     ///< GPIO analógico (ADC1)
  float         filteredADC;             ///< Valor EMA del ADC
  bool          firstSample;             ///< true hasta la primera muestra válida
  bool          initialized;             ///< true tras mqSetup() exitoso
  unsigned long lastSampleMs;            ///< millis() de la última muestra
};

// ============================================================================
// CONSTANTES
// ============================================================================

static constexpr uint16_t MQ_ADC_MAX            = 4095;
static constexpr uint16_t MQ_SAMPLE_INTERVAL_MS = 500;
static constexpr float    MQ_FILTER_ALPHA       = 0.1f;

// ============================================================================
// FUNCIONES INTERNAS (inline, sin estado global)
// ============================================================================

inline bool mqIsValidADC(float adc) {
  return adc > 0.0f && adc <= MQ_ADC_MAX && !isnan(adc) && !isinf(adc);
}

// ============================================================================
// API PÚBLICA
// ============================================================================

/**
 * @brief Inicializa el driver del sensor.
 *
 * Debe llamarse una vez en setup(), con `state.pin` ya fijado.
 */
inline void mqSetup(MQSensorState& s) {
  s.filteredADC  = 0.0f;
  s.firstSample  = true;
  s.initialized  = false;
  s.lastSampleMs = 0;

  pinMode(s.pin, INPUT);

  s.initialized = true;
}

/** @brief Devuelve el valor ADC filtrado (EMA) sin convertir a PPM. */
inline float mqGetFilteredADC(const MQSensorState& s) {
  return s.filteredADC;
}

/**
 * @brief Actualiza el filtro EMA del ADC. Llamar en loop().
 */
inline void mqUpdate(MQSensorState& s) {
  if (!s.initialized) return;

  const unsigned long now = millis();
  if (now - s.lastSampleMs >= MQ_SAMPLE_INTERVAL_MS) {
    s.lastSampleMs = now;

    const int rawADC = analogRead(s.pin);

    if (rawADC > 0 && rawADC <= MQ_ADC_MAX) {
      if (s.firstSample) {
        s.filteredADC = (float)rawADC;
        s.firstSample = false;
        Serial.printf("  Primera muestra ADC [pin %d]: %.0f\n", s.pin, s.filteredADC);
      } else {
        s.filteredADC = MQ_FILTER_ALPHA * (float)rawADC +
                        (1.0f - MQ_FILTER_ALPHA) * s.filteredADC;
      }
    }
  }
}

/** @brief Resumen de estado del sensor para Serial. */
inline String mqGetInfo(const MQSensorState& s) {
  if (!s.initialized) return "Sensor no inicializado";
  char buf[64];
  snprintf(buf, sizeof(buf), "ADC: %.0f", s.filteredADC);
  return String(buf);
}

#endif // MQ_SENSOR_H
