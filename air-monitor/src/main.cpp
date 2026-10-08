/**
 * Sistema de Monitoreo de Calidad del Aire – AirQ
 *
 * Sensor registrado:
 *  - mq135  pin 34  → reporta mq135_adc (ADC crudo filtrado, sin calibración)
 */

#include <Arduino.h>

// Infraestructura completa (MQTT, WiFi, OTA, snapshot…)
#include "app_runner.h"

// Instrumento MQ
#include "MQInstrument.h"

// Servidor web
#include "webs.h"

MQInstrument mq135(34, "mq135");

bool webServerStarted = false;  ///< Consumido vía extern en system.h y webs.h

void setup() {
  Serial.begin(115200);
  delay(500);

  registerNvsHooks();    // Hooks NVS del proyecto — ANTES de libSetup
  mq135.registerHooks(); // Hooks MQTT/snapshot/serial/remote — ANTES de libSetup

  libSetup([]() {
    mq135.setup();
  });

  Serial.println("\n▶ Iniciando servidor web…");
  setupWeb();
  webServerStarted = true;
  Serial.printf("✓ Web disponible en http://%s  (AP)\n", WiFi.softAPIP().toString().c_str());
}

void loop() {
  mq135.update();  // EMA del ADC crudo
  loopWeb();
  libLoop();
  delay(10);
}
