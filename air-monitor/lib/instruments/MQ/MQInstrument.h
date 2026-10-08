#ifndef MQ_INSTRUMENT_H
#define MQ_INSTRUMENT_H

/**
 * @file MQInstrument.h
 * @brief Clase instanciable que integra un sensor MQ con la infraestructura
 *        de la plataforma (MQTT, Serial).
 *
 * [REUSABLE] Depende de lib/infra/ pero NO de ningún archivo src/.
 * Sin calibración: solo reporta el ADC filtrado del sensor.
 *
 * Uso en main.cpp:
 *   MQInstrument mq135(34, "mq135");
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
  /**
   * @param pin  GPIO analógico (ADC1) del sensor.
   * @param id   Identificador único, p.ej. "mq135". Máx 15 chars.
   *             Se usa como prefijo del campo MQTT (`<id>_adc`) y de comandos Serial.
   */
  MQInstrument(uint8_t pin, const char* id) {
    strncpy(id_, id, sizeof(id_) - 1); id_[sizeof(id_) - 1] = '\0';
    memset(&state_, 0, sizeof(state_));
    state_.pin         = pin;
    state_.firstSample = true;
  }

  // -------------------------------------------------------------------------
  // Hooks de infraestructura
  // -------------------------------------------------------------------------

  /**
   * @brief Registra todos los hooks de infra.
   *
   * DEBE llamarse ANTES de libSetup() para que los fillers MQTT y los
   * comandos Serial queden registrados en tiempo de inicialización.
   */
  void registerHooks() {
    // --- MQTT: payload de métricas ---
    mqttPayloadAddMetricsFiller([this](JsonObject& m) {
      char adcKey[24];
      snprintf(adcKey, sizeof(adcKey), "%s_adc", id_);
      m[adcKey] = static_cast<int>(getFilteredADC());
    });

    // --- Status extender (se imprime con comando serial "status") ---
    systemAddStatusExtender([this]() {
      Serial.printf("Sensor [%s] : ADC=%.0f\n", id_, getFilteredADC());
    });

    registerSerialCommands_();
  }

  /**
   * @brief Inicializa el hardware del sensor.
   *
   * DEBE llamarse dentro del callback de libSetup(), DESPUÉS de que
   * loadConfig() del proyecto haya restaurado la config global.
   */
  void setup() {
    Serial.printf("\n▶ Inicializando sensor MQ [%s] (pin %d)…\n", id_, state_.pin);
    mqSetup(state_);
    Serial.printf("✓ Sensor [%s] listo\n\n", id_);
  }

  /**
   * @brief Actualiza el filtro EMA del ADC. Llamar en loop().
   */
  void update() {
    mqUpdate(state_);
  }

  // -------------------------------------------------------------------------
  // Getters
  // -------------------------------------------------------------------------

  float       getFilteredADC() const { return mqGetFilteredADC(state_); }
  bool        isInitialized()  const { return state_.initialized; }
  const char* getId()          const { return id_; }
  String      getInfo()        const { return mqGetInfo(state_); }

private:
  char          id_[16];
  MQSensorState state_;

  // -------------------------------------------------------------------------
  // Registro de comandos Serial
  // -------------------------------------------------------------------------

  void registerSerialCommands_() {
    // "<id>.read" — lectura instantánea
    char cmd[20]; snprintf(cmd, sizeof(cmd), "%s.read", id_);
    serialRegisterCommand(cmd, "Lectura instantánea del sensor",
      [this](const String&) {
        Serial.printf("Lectura [%s] : ADC filtrado = %.0f\n", id_, getFilteredADC());
      });
  }
};

#endif // MQ_INSTRUMENT_H
