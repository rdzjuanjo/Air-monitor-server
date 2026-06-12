#ifndef APP_RUNNER_H
#define APP_RUNNER_H

/**
 * @file app_runner.h
 * @brief Orquesta la inicialización y el loop de toda la infraestructura (lib/).
 *
 * [REUSABLE] No depende de ningún sensor específico.
 *
 * Uso en main.cpp:
 *   projectInit();        // ANTES de libSetup — registra hooks NVS/MQTT/snapshot
 *   libSetup([]() {       // callback con la init del sensor
 *     setupMySensor();
 *   });
 *
 *   // en loop():
 *   updateMySensor();
 *   libLoop();
 *
 * TODO:SENSOR — no modificar este archivo al adaptar el firmware a otro sensor.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

#include "NvsConfig.h"
#include "system_status.h"
#include "wifimanager.h"
#include "mqttsend.h"
#include "system.h"
#include "ota.h"
#include "remote_actions.h"

typedef void (*SensorSetupFn)();

// ============================================================================
// libSetup — init completa de la infraestructura
// Llamar DESPUÉS de projectInit() y ANTES de cualquier otra cosa en setup().
// ============================================================================
inline void libSetup(SensorSetupFn sensorInit) {
  // 1. NVS — registerNvsHooks() ya registró todos los hooks antes de esta llamada
  Serial.println("▶ Inicializando NVS…");
  initEEPROM();
  loadConfig();

  // 2. LittleFS
  if (!LittleFS.begin(false)) LittleFS.begin(true);

  // 3. Init del sensor (inyectada por el proyecto)
  if (sensorInit) sensorInit();

  // 4. WiFi: AP siempre activo + intento de STA si hay credenciales
  Serial.println("\n▶ Iniciando WiFi…");
  wifiSetup();

  // 5. Acciones remotas MQTT genéricas (coordenadas, deviceId, check_ota, get_status)
  registerBuiltinRemoteActions();
  registerOtaRemoteAction();
  registerStatusRemoteAction();

  // 6. MQTT (se conectará cuando haya STA)
  Serial.println("\n▶ Configurando MQTT…");
  setupMQTT();

  // 7. OTA: solo se ejecuta por comando remoto "check_ota" (ver registerOtaRemoteAction)
  //    para evitar que una actualización defectuosa afecte todas las estaciones a la vez.

  // 8. Timers de sistema
  lastWifiConnectedTime = millis();

  Serial.println(F("\n╔══════════════════════════════════════════════╗"));
  Serial.println(F("║   Sistema inicializado                       ║"));
  Serial.println(F("╚══════════════════════════════════════════════╝"));
  Serial.println("Comandos: help | status | calibrate | read | autocal | config | resetwifi\n");
}

// ============================================================================
// libLoop — loop completo de la infraestructura
// Llamar en cada iteración de loop(), típicamente después de actualizar el sensor.
// ============================================================================
inline void libLoop() {
  handleSerialCommands();

  // Métricas de storage cada 5 minutos
  {
    static unsigned long lastStorageUpdate = 0;
    const unsigned long now = millis();
    if (now - lastStorageUpdate >= 300000UL || lastStorageUpdate == 0) {
      lastStorageUpdate = now;
      const size_t total = LittleFS.totalBytes();
      const size_t used  = LittleFS.usedBytes();
      sysStatusUpdateStorage(total, total > used ? total - used : 0);
    }
  }

  wifiLoop();

  if (wifi_connected) {
    loopMQTT();
    printSystemStatus();
  }
}

#endif // APP_RUNNER_H
