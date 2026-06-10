#ifndef MQ_INSTRUMENT_H
#define MQ_INSTRUMENT_H

/**
 * @file MQInstrument.h
 * @brief Clase instanciable que integra un sensor MQ con la infraestructura
 *        de la plataforma (MQTT, NVS, Serial, acciones remotas).
 *
 * [REUSABLE] Depende de lib/infra/ pero NO de ningún archivo src/.
 *
 * Uso en main.cpp:
 *   static const MQParams MQ135_PARAMS = { 3.6f, 102.2f, -2.473f };
 *   MQInstrument mq135(34, "mq135", "CVOL", MQ135_PARAMS);
 *   MQInstrument mq135b(34, "mq135b", "CVOL");  // usa defaults MQ135
 *
 *   void setup() {
 *     projectInit();                           // NVS hooks del proyecto
 *     mq135.registerHooks();                   // ANTES de libSetup
 *     libSetup([]() { mq135.setup(); });
 *     setupWeb(); webServerStarted = true;
 *   }
 *   void loop() {
 *     mq135.update();
 *     loopWeb();
 *     libLoop();
 *   }
 *
 * NOTA: registerHooks() debe llamarse DESPUÉS de que los headers de infra
 * estén disponibles (incluir app_runner.h o mqttsend.h antes de este archivo).
 */

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#include "MQSensor.h"

// Infra registration APIs (ya compiladas antes de este include en main.cpp)
#include "mqtt_payload.h"
#include "mqtt_handlers.h"
#include "system.h"

// ============================================================================
// CLASE MQInstrument
// ============================================================================

class MQInstrument {
public:
  static constexpr MQParams defaultMQ135Params() {
    return MQParams{3.6f, 102.2f, -2.473f};
  }

  /**
   * @param pin        GPIO analógico (ADC1) del sensor.
   * @param id         Identificador único, p.ej. "mq135". Máx 15 chars.
   *                   Se usa como namespace NVS y prefijo de comandos Serial.
   * @param metricKey  Clave del campo en el payload MQTT de métricas, p.ej. "CVOL".
   * @param params     Parámetros de curva (ratio, a, b). Si se omite, se usan
   *                   los valores por defecto de MQ135.
   */
  MQInstrument(uint8_t pin, const char* id, const char* metricKey)
    : MQInstrument(pin, id, metricKey, defaultMQ135Params()) {}

  MQInstrument(uint8_t pin, const char* id, const char* metricKey, MQParams params)
    : params_(params) {
    strncpy(id_,        id,        sizeof(id_) - 1);        id_[sizeof(id_) - 1]               = '\0';
    strncpy(metricKey_, metricKey, sizeof(metricKey_) - 1); metricKey_[sizeof(metricKey_) - 1] = '\0';
    cfg_   = { 2000.0f, false };
    memset(&state_, 0, sizeof(state_));
    state_.pin       = pin;
    state_.firstSample = true;
  }

  // -------------------------------------------------------------------------
  // Hooks de infraestructura
  // -------------------------------------------------------------------------

  /**
   * @brief Carga config de NVS y registra todos los hooks de infra.
   *
   * DEBE llamarse ANTES de libSetup() para que los fillers MQTT y los
   * comandos Serial queden registrados en tiempo de inicialización.
   */
  void registerHooks() {
    loadConfig_();

    // --- MQTT: payload de métricas ---
    mqttPayloadAddMetricsFiller([this](JsonObject& m) {
      const float ppm = getReading();
      m[metricKey_] = ppm;
      char adcKey[24];
      snprintf(adcKey, sizeof(adcKey), "%s_adc", id_);
      m[adcKey] = static_cast<int>(getFilteredADC());
    });

    // --- MQTT: metadata de instrumento ---
    mqttPayloadAddInstrumentFiller([this](JsonObject& i) {
      char calKey[24], adc0Key[24];
      snprintf(calKey,  sizeof(calKey),  "%s_cal",  id_);
      snprintf(adc0Key, sizeof(adc0Key), "%s_adc0", id_);
      i[calKey]  = cfg_.sensorCalibrated;
      i[adc0Key] = static_cast<int>(cfg_.calibrationValue);
    });

    // --- Status extender (se imprime con comando serial "status") ---
    systemAddStatusExtender([this]() {
      Serial.printf("Sensor [%s] : %.2f ppm | cal: %s\n",
                    id_, getReading(),
                    cfg_.sensorCalibrated ? "Sí" : "No");
    });

    registerSerialCommands_();
    registerRemoteActions_();
  }

  /**
   * @brief Inicializa el hardware del sensor.
   *
   * DEBE llamarse dentro del callback de libSetup(), DESPUÉS de que
   * loadConfig() del proyecto haya restaurado la config global.
   * La config del instrumento ya fue cargada en registerHooks().
   */
  void setup() {
    Serial.printf("\n▶ Inicializando sensor MQ [%s] (pin %d)…\n", id_, state_.pin);
    mqSetup(state_, params_, cfg_);
    Serial.printf("✓ Sensor [%s] listo\n\n", id_);
  }

  /**
   * @brief Actualiza el filtro EMA y comprueba auto-calibración. Llamar en loop().
   */
  void update() {
    if (mqUpdate(state_, params_, cfg_)) {
      saveConfig_();  // auto-calibración aplicada — persistir en NVS
    }
  }

  // -------------------------------------------------------------------------
  // Getters
  // -------------------------------------------------------------------------

  float       getReading()          const { return mqGetReading(state_, params_, cfg_); }
  float       getFilteredADC()      const { return mqGetFilteredADC(state_); }
  bool        isCalibrated()        const { return cfg_.sensorCalibrated; }
  bool        isInitialized()       const { return state_.initialized; }
  float       getCalibrationValue() const { return cfg_.calibrationValue; }
  const char* getId()               const { return id_; }
  String      getInfo()             const { return mqGetInfo(state_, params_, cfg_); }

  /**
   * @brief Aplica una calibración manual (ADC0 en aire limpio) y persiste en NVS.
   *
   * Exponiendo applyCalibration_ como API pública para shims de compatibilidad
   * (e.g. setCalibrationADC0 en main.cpp para webs.h).
   */
  bool calibrate(float adc0, String* detail = nullptr) {
    return applyCalibration_(adc0, detail);
  }

private:
  char             id_[16];
  char             metricKey_[16];
  MQParams         params_;
  MQSensorState    state_;
  MQInstanceConfig cfg_;

  // -------------------------------------------------------------------------
  // NVS (namespace propio = id_)
  // -------------------------------------------------------------------------

  void loadConfig_() {
    Preferences prefs;
    if (!prefs.begin(id_, /*readOnly=*/true)) return;
    cfg_.calibrationValue = prefs.getFloat("cal",        2000.0f);
    cfg_.sensorCalibrated = prefs.getBool ("calibrated", false);
    prefs.end();
  }

  void saveConfig_() {
    Preferences prefs;
    if (!prefs.begin(id_, /*readOnly=*/false)) return;
    prefs.putFloat("cal",        cfg_.calibrationValue);
    prefs.putBool ("calibrated", cfg_.sensorCalibrated);
    prefs.end();
  }

  // -------------------------------------------------------------------------
  // Calibración interna
  // -------------------------------------------------------------------------

  bool applyCalibration_(float adc0, String* detail = nullptr) {
    if (!mqSetCalibration(state_, cfg_, adc0)) {
      if (detail) *detail = String("ADC0 inválido: ") + String(adc0, 1);
      return false;
    }
    saveConfig_();
    if (detail) *detail = String("ADC0=") + String(cfg_.calibrationValue, 0) + " guardado";
    return true;
  }

  // -------------------------------------------------------------------------
  // Registro de comandos Serial
  // -------------------------------------------------------------------------

  void registerSerialCommands_() {
    // "<id>.read" — lectura instantánea
    {
      char cmd[20]; snprintf(cmd, sizeof(cmd), "%s.read", id_);
      serialRegisterCommand(cmd, "Lectura instantánea del sensor",
        [this](const String&) {
          const float ppm = getReading();
          Serial.printf("Lectura [%s] : %.2f ppm\n", id_, ppm);
          Serial.printf("Calidad      : %s\n",        mqGetAirQualityLevel(ppm).c_str());
          Serial.printf("ADC filtrado : %.0f\n",      getFilteredADC());
          Serial.printf("ADC0 cal     : %.0f\n",      cfg_.calibrationValue);
        });
    }

    // "<id>.autocal" — estado de auto-calibración
    {
      char cmd[20]; snprintf(cmd, sizeof(cmd), "%s.autocal", id_);
      serialRegisterCommand(cmd, "Estado de auto-calibración",
        [this](const String&) {
          Serial.println(mqGetAutoCalInfo(state_, cfg_));
        });
    }

    // "<id>.adc0 <valor>" — calibración manual
    {
      char cmd[20]; snprintf(cmd, sizeof(cmd), "%s.adc0", id_);
      serialRegisterCommand(cmd, "Setear ADC0 manual (uso: <id>.adc0 <valor>)",
        [this](const String& line) {
          const int spacePos = line.indexOf(' ');
          if (spacePos < 0) {
            Serial.printf("Uso: %s.adc0 <valor ADC0>\n", id_);
            return;
          }
          const float adc0 = line.substring(spacePos + 1).toFloat();
          String detail;
          if (applyCalibration_(adc0, &detail)) {
            Serial.println("✓ " + detail);
          } else {
            Serial.println("✗ " + detail);
          }
        });
    }
  }

  // -------------------------------------------------------------------------
  // Registro de acciones remotas MQTT
  // -------------------------------------------------------------------------

  void registerRemoteActions_() {
    char action[40]; snprintf(action, sizeof(action), "calibrate_%s", id_);
    mqttHandlerRegisterAction(action,
      [this](const JsonDocument& doc, const char* /*topic*/, String& detail) -> bool {
        const char* mode   = doc["params"]["mode"] | "manual";
        if (String(mode).equalsIgnoreCase("manual")) {
          const bool hasVal = doc["params"]["value"].is<float>() ||
                              doc["params"]["value"].is<int>();
          if (hasVal) {
            const float adc0 = doc["params"]["value"].as<float>();
            return applyCalibration_(adc0, &detail);
          }
        }
        detail = "Use params.mode=manual y params.value=<ADC0>";
        return false;
      });
  }
};

#endif // MQ_INSTRUMENT_H
