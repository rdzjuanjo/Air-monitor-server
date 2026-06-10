#ifndef SYSTEM_H
#define SYSTEM_H

#include <Arduino.h>
#include <WiFi.h>
#include <functional>
#include "NvsConfig.h"

/**
 * @file system.h
 * @brief Funciones del sistema: reinicio diario, estado, comandos serial
 *
 * [REUSABLE] No conoce ningún sensor específico. El proyecto registra sus
 * comandos con serialRegisterCommand() y su extensor de estado con
 * systemSetStatusExtender().
 *
 * NOTA: El watchdog WiFi con reinicio se eliminó intencionalmente.
 *       La reconexión WiFi la gestiona wifimanager.h sin reinicios.
 */

// ============================================================================
// REINICIO DIARIO
// ============================================================================

// ============================================================================
// ESTADO PERIÓDICO
// ============================================================================

extern unsigned long lastStatusUpdate;
const unsigned long STATUS_UPDATE_INTERVAL = 100000;

// ============================================================================
// FORWARD DECLARATIONS EXTERNAS
// ============================================================================

extern bool   wifi_connected;
extern bool   webServerStarted;

// ============================================================================
// REGISTRO DE COMANDOS SERIAL
// [REUSABLE] El proyecto agrega comandos sensor-específicos vía registry.
// ============================================================================

using SerialCmdFn      = std::function<void(const String&)>;
using StatusExtenderFn = std::function<void()>;

struct SerialCommandEntry {
  char        name[24];
  char        help[64];
  SerialCmdFn handler;
};

static constexpr uint8_t  SYSTEM_MAX_COMMANDS  = 12;
static constexpr uint8_t  SYSTEM_MAX_EXTENDERS = 4;

static SerialCommandEntry sSerialCommands[SYSTEM_MAX_COMMANDS];
static uint8_t            sSerialCommandCount  = 0;
static StatusExtenderFn   sStatusExtenders[SYSTEM_MAX_EXTENDERS];
static uint8_t            sStatusExtenderCount = 0;

inline void serialRegisterCommand(const char* name, const char* help, SerialCmdFn fn) {
  if (sSerialCommandCount < SYSTEM_MAX_COMMANDS) {
    strncpy(sSerialCommands[sSerialCommandCount].name, name, 23);
    sSerialCommands[sSerialCommandCount].name[23] = '\0';
    strncpy(sSerialCommands[sSerialCommandCount].help, help, 63);
    sSerialCommands[sSerialCommandCount].help[63] = '\0';
    sSerialCommands[sSerialCommandCount].handler   = fn;
    sSerialCommandCount++;
  }
}

inline void systemAddStatusExtender(StatusExtenderFn fn) {
  if (sStatusExtenderCount < SYSTEM_MAX_EXTENDERS) {
    sStatusExtenders[sStatusExtenderCount++] = fn;
  }
}

// ============================================================================
// VARIABLES
// ============================================================================

unsigned long lastStatusUpdate  = 0;
unsigned long lastWifiConnectedTime = 0;  // requerida por main.cpp

// ============================================================================
// IMPLEMENTACIÓN
// ============================================================================

void printSystemStatus() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint < CFG_SAMPLING_STATUS_INTERVAL_MS) return;
  lastPrint = millis();

  extern bool  isMQTTConnected();

  Serial.println("\n=== ESTADO DEL SISTEMA ===");
  Serial.printf("WiFi STA  : %s\n",          wifi_connected ? WiFi.localIP().toString().c_str() : "Desconectado");
  Serial.printf("AP IP     : %s\n",          WiFi.softAPIP().toString().c_str());
  if (wifi_connected) {
    Serial.printf("MQTT      : %s\n",        isMQTTConnected() ? "Conectado" : "Desconectado");
  }
  for (uint8_t i = 0; i < sStatusExtenderCount; i++) {
    if (sStatusExtenders[i]) sStatusExtenders[i]();
  }
  Serial.println("==========================\n");
}

void handleSerialCommands() {
  if (!Serial.available()) return;

  String command = Serial.readStringUntil('\n');
  command.trim();

  extern void  printConfig();
  extern void  resetConfig();
  extern void  clearWiFiCredentials(bool restart);

  if (command == "status") {
    printSystemStatus();
  }
  else if (command == "config") {
    printConfig();
  }
  else if (command == "reset") {
    Serial.println("Reseteando configuración…");
    resetConfig();
  }
  else if (command == "resetwifi") {
    Serial.println("Borrando credenciales WiFi y reiniciando…");
    clearWiFiCredentials(true);
    delay(500);
    ESP.restart();
  }
  else if (command == "ip") {
    if (!wifi_connected) Serial.println("Sin IP STA");
    else Serial.println("STA IP: " + WiFi.localIP().toString());
    Serial.println("AP  IP: " + WiFi.softAPIP().toString());
  }
  else if (command == "help") {
    Serial.println("\nComandos disponibles:");
    Serial.println("  status       - Estado del sistema");
    Serial.println("  config       - Mostrar configuración");
    Serial.println("  reset        - Resetear configuración NVS");
    Serial.println("  resetwifi    - Borrar credenciales WiFi");
    Serial.println("  ip           - Mostrar IPs");
    Serial.println("  help         - Esta ayuda");
    for (uint8_t i = 0; i < sSerialCommandCount; i++) {
      Serial.printf("  %-12s - %s\n", sSerialCommands[i].name, sSerialCommands[i].help);
    }
    Serial.println();
  }
  else {
    // --- Buscar en el registro del proyecto ---
    bool found = false;
    for (uint8_t i = 0; i < sSerialCommandCount; i++) {
      const String name(sSerialCommands[i].name);
      if (command == name || command.startsWith(name + " ")) {
        sSerialCommands[i].handler(command);
        found = true;
        break;
      }
    }
    if (!found && command.length() > 0) {
      Serial.println("Comando no reconocido. Escribe 'help' para ver opciones.");
    }
  }
}

#endif // SYSTEM_H
