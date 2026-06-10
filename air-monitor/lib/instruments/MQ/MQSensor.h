#ifndef MQ_SENSOR_H
#define MQ_SENSOR_H

/**
 * @file MQSensor.h
 * @brief Driver instanciable genérico para sensores de la familia MQ.
 *
 * [REUSABLE] No tiene estado global. Toda la lógica opera sobre structs
 * pasados por referencia: MQSensorState (estado de ejecución) y
 * MQInstanceConfig (calibración persistida). Los parámetros de curva se
 * expresan en MQParams (rs/r0 ratio en aire limpio, coefs. a y b).
 *
 * Uso típico (ver MQInstrument.h para la integración completa):
 *   MQParams p = { 3.6f, 102.2f, -2.473f };
 *   MQInstanceConfig cfg = { 2000.0f, false };
 *   MQSensorState s;
 *   s.pin = 34;
 *   mqSetup(s, p, cfg);            // en setup()
 *   bool autoCalSaved = mqUpdate(s, p, cfg);  // en loop()
 *   float ppm = mqGetReading(s, p, cfg);
 */

#include <Arduino.h>

// ============================================================================
// TIPOS PÚBLICOS
// ============================================================================

/**
 * @brief Parámetros de curva del sensor (específicos de cada modelo MQ).
 *
 *  PPM = a * (Rs/R0) ^ b
 *
 * MQ135 defaults: ratio=3.6, a=102.2, b=-2.473
 * MQ2   defaults: ratio=9.83, a=614.9, b=-1.532
 */
struct MQParams {
  float ratio;  ///< Rs/R0 en aire limpio
  float a;      ///< Coeficiente A de la curva PPM
  float b;      ///< Exponente B (típicamente negativo)
};

/**
 * @brief Datos de calibración por instancia (se persisten en NVS).
 *
 * La lectura en NVS la gestiona MQInstrument, no este módulo.
 */
struct MQInstanceConfig {
  float calibrationValue;  ///< ADC0: lectura ADC en aire limpio calibrado
  bool  sensorCalibrated;  ///< true si ha habido calibración válida
};

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
  float         lowestADCRecorded;       ///< ADC mínimo detectado (para auto-cal)
  unsigned long lastAutoCalMs;           ///< millis() de la última auto-calibración
  int           consecutiveCleanReadings;///< Contador de lecturas más limpias
};

// ============================================================================
// CONSTANTES
// ============================================================================

static constexpr uint16_t     MQ_ADC_MAX              = 4095;
static constexpr uint16_t     MQ_SAMPLE_INTERVAL_MS   = 500;
static constexpr float        MQ_FILTER_ALPHA          = 0.1f;
static constexpr uint8_t      MQ_AUTO_CAL_MIN_READINGS = 50;
static constexpr uint8_t      MQ_AUTO_CAL_MIN_IMPROVE  = 20;
static constexpr uint8_t      MQ_AUTO_CAL_NOISE_THRESH = 10;
static constexpr unsigned long MQ_AUTO_CAL_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;

// ============================================================================
// FUNCIONES INTERNAS (inline, sin estado global)
// ============================================================================

inline bool mqIsValidADC(float adc) {
  return adc > 0.0f && adc <= MQ_ADC_MAX && !isnan(adc) && !isinf(adc);
}

inline bool mqIsValidADC0(float val) {
  return val > 0.0f && val <= MQ_ADC_MAX && !isnan(val) && !isinf(val);
}

/**
 * @brief Calcula el cociente Rs/R0 a partir del ADC medido y el ADC0 de calibración.
 *
 * El circuito de carga del MQ produce una tensión proporcional a la resistencia
 * del sensor (Rs). En aire limpio, Rs = R0. Para calcular la concentración de
 * gas se necesita el cociente Rs/R0, que se puede derivar del cociente de ADC
 * usando la propiedad del divisor resistivo:
 *
 *   Rs/R0 = MQRatio * (ADCmax/ADC - 1) / (ADCmax/ADC0 - 1)
 */
inline float mqCalcRsR0(float adc, float adc0, float ratio) {
  if (!mqIsValidADC(adc) || !mqIsValidADC0(adc0)) return 0.0f;
  const float adcRatio  = (float)MQ_ADC_MAX / adc;
  const float adc0Ratio = (float)MQ_ADC_MAX / adc0;
  const float denom     = adc0Ratio - 1.0f;
  if (denom <= 0.0001f) return 0.0f;
  return ratio * (adcRatio - 1.0f) / denom;
}

/** @brief Calcula PPM desde Rs/R0 usando la curva PPM = a * (Rs/R0)^b. */
inline float mqCalcPPM(float rsR0, float a, float b) {
  if (rsR0 <= 0.0f) return 0.0f;
  return a * powf(rsR0, b);
}

// ============================================================================
// API PÚBLICA
// ============================================================================

/**
 * @brief Inicializa el driver del sensor.
 *
 * Debe llamarse una vez en setup(), con `state.pin` ya fijado.
 * Si la calibración en `cfg` es válida, se usa como base de auto-cal.
 * Si no, se usa un ADC0 temporal de 2000 y se avisa por Serial.
 */
inline void mqSetup(MQSensorState& s, const MQParams& /*p*/, MQInstanceConfig& cfg) {
  s.filteredADC              = 0.0f;
  s.firstSample              = true;
  s.initialized              = false;
  s.lastSampleMs             = 0;
  s.consecutiveCleanReadings = 0;
  s.lastAutoCalMs            = millis();

  pinMode(s.pin, INPUT);

  if (cfg.sensorCalibrated && mqIsValidADC0(cfg.calibrationValue)) {
    s.lowestADCRecorded = cfg.calibrationValue;
    Serial.printf("  ADC0 de calibración: %.0f\n", cfg.calibrationValue);
  } else {
    cfg.calibrationValue = 2000.0f;
    cfg.sensorCalibrated = false;
    s.lowestADCRecorded  = 2000.0f;
    Serial.println("  ⚠️  Sensor NO calibrado — use '<id>.adc0 <valor>'");
  }

  s.initialized = true;
  Serial.println("  🔄 Auto-calibración diaria habilitada");
}

/** @brief Devuelve la lectura de PPM basada en el ADC filtrado actual. */
inline float mqGetReading(const MQSensorState& s, const MQParams& p,
                          const MQInstanceConfig& cfg) {
  if (!s.initialized) return 0.0f;
  const float rsR0 = mqCalcRsR0(s.filteredADC, cfg.calibrationValue, p.ratio);
  return mqCalcPPM(rsR0, p.a, p.b);
}

/** @brief Devuelve el valor ADC filtrado (EMA) sin convertir a PPM. */
inline float mqGetFilteredADC(const MQSensorState& s) {
  return s.filteredADC;
}

/** @brief true si la lectura de PPM está en un rango plausible. */
inline bool mqIsValidReading(float ppm) {
  return ppm >= 0.0f && ppm <= 1000.0f && !isnan(ppm) && !isinf(ppm);
}

/**
 * @brief Clasifica la calidad del aire según el nivel de PPM.
 *
 * Umbrales orientativos para MQ135/COVs:
 *   < 8 ppm  → Alta
 *   8–16 ppm → MEDIO
 *   > 16 ppm → Baja
 */
inline String mqGetAirQualityLevel(float ppm) {
  if (ppm < 8.0f)  return "Alta";
  if (ppm < 16.0f) return "MEDIO";
  return "Baja";
}

/**
 * @brief Aplica manualmente una nueva calibración (ADC0).
 *
 * No persiste en NVS — eso lo hace el llamador (MQInstrument::saveConfig_).
 * @return true si el valor es válido y se aplicó.
 */
inline bool mqSetCalibration(MQSensorState& s, MQInstanceConfig& cfg, float adc0) {
  if (!mqIsValidADC0(adc0)) {
    Serial.printf("✗ ADC0 inválido: %.2f\n", adc0);
    return false;
  }
  cfg.calibrationValue       = adc0;
  cfg.sensorCalibrated       = true;
  s.lowestADCRecorded        = adc0;
  s.consecutiveCleanReadings = 0;
  Serial.printf("ADC0 actualizado: %.0f\n", adc0);
  return true;
}

// ============================================================================
// AUTO-CALIBRACIÓN
// ============================================================================

/** @brief true si las condiciones para una auto-calibración diaria se cumplen. */
inline bool mqShouldAutoCalibrate(const MQSensorState& s, const MQInstanceConfig& cfg) {
  if (s.consecutiveCleanReadings < MQ_AUTO_CAL_MIN_READINGS) return false;
  const float improvement = cfg.calibrationValue - s.lowestADCRecorded;
  if (improvement < MQ_AUTO_CAL_MIN_IMPROVE) return false;
  if (s.lowestADCRecorded < 100.0f || s.lowestADCRecorded > 3500.0f) return false;
  return true;
}

/**
 * @brief Ejecuta la auto-calibración si el intervalo y las condiciones se cumplen.
 *
 * @return true si se aplicó una nueva calibración (el llamador debe persistirla en NVS).
 */
inline bool mqCheckAutoCalibrate(MQSensorState& s, MQInstanceConfig& cfg) {
  if (millis() - s.lastAutoCalMs < MQ_AUTO_CAL_INTERVAL_MS) return false;

  if (!mqShouldAutoCalibrate(s, cfg)) {
    Serial.println("⏭️  Auto-calibración omitida — condiciones no óptimas");
    s.lastAutoCalMs = millis();
    return false;
  }

  const float oldADC0 = cfg.calibrationValue;
  const float newADC0 = s.lowestADCRecorded;

  Serial.println("\n=== AUTO-CALIBRACIÓN DIARIA ===");
  Serial.printf("ADC0 anterior : %.0f\n", oldADC0);
  Serial.printf("Nuevo ADC0    : %.0f (mejora: %.0f pts)\n", newADC0, oldADC0 - newADC0);
  Serial.printf("Lecturas base : %d\n", s.consecutiveCleanReadings);

  cfg.calibrationValue       = newADC0;
  cfg.sensorCalibrated       = true;
  s.lowestADCRecorded        = newADC0;
  s.consecutiveCleanReadings = 0;
  s.lastAutoCalMs            = millis();

  Serial.println("✅ Auto-calibración completada");
  Serial.println("================================\n");
  return true;  // caller must persist cfg to NVS
}

/**
 * @brief Actualiza el filtro EMA y verifica auto-calibración. Llamar en loop().
 *
 * @return true si se aplicó auto-calibración (el llamador debe guardar cfg en NVS).
 */
inline bool mqUpdate(MQSensorState& s, const MQParams& /*p*/, MQInstanceConfig& cfg) {
  if (!s.initialized) return false;

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

      if (cfg.sensorCalibrated) {
        if (s.filteredADC < cfg.calibrationValue - MQ_AUTO_CAL_NOISE_THRESH) {
          s.consecutiveCleanReadings++;
          if (s.filteredADC < s.lowestADCRecorded) {
            s.lowestADCRecorded = s.filteredADC;
          }
        } else {
          s.consecutiveCleanReadings = 0;
        }
      }
    }
  }

  return mqCheckAutoCalibrate(s, cfg);
}

/** @brief Información de estado de auto-calibración formateada para Serial. */
inline String mqGetAutoCalInfo(const MQSensorState& s, const MQInstanceConfig& cfg) {
  const unsigned long elapsed = millis() - s.lastAutoCalMs;
  const unsigned long hoursLeft = (elapsed < MQ_AUTO_CAL_INTERVAL_MS)
      ? (MQ_AUTO_CAL_INTERVAL_MS - elapsed) / 3600000UL : 0UL;
  char buf[200];
  snprintf(buf, sizeof(buf),
           "Auto-cal ON | prox. %luh | ADC min: %.0f | lect. limpias: %d/%d | mejora: %.0f",
           hoursLeft, s.lowestADCRecorded,
           s.consecutiveCleanReadings, (int)MQ_AUTO_CAL_MIN_READINGS,
           cfg.calibrationValue - s.lowestADCRecorded);
  return String(buf);
}

/** @brief Resumen de estado del sensor para Serial. */
inline String mqGetInfo(const MQSensorState& s, const MQParams& p,
                        const MQInstanceConfig& cfg) {
  if (!s.initialized) return "Sensor no inicializado";
  const float ppm = mqGetReading(s, p, cfg);
  char buf[256];
  snprintf(buf, sizeof(buf),
           "Valor: %.2f ppm | Calibrado: %s | Nivel: %s | ADC: %.0f | ADC0: %.0f",
           ppm,
           cfg.sensorCalibrated ? "Sí" : "No",
           mqGetAirQualityLevel(ppm).c_str(),
           s.filteredADC,
           cfg.calibrationValue);
  return String(buf);
}

#endif // MQ_SENSOR_H
