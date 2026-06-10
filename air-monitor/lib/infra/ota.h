// [REUSABLE] Sin dependencias del sensor MQ135. URL y versión OTA vienen de config.h.
#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#include <ESP32OTAPull.h>
#include <cstring>

#include "system_status.h"
#include "config.h"

/**
 * @file ota.h
 * @brief Actualización Over-The-Air (OTA) del firmware
 * 
 * Este módulo permite actualizar el firmware del ESP32 de forma remota
 * sin necesidad de conexión USB.
 * 
 * Funcionamiento:
 * 1. El ESP32 descarga un JSON desde el servidor OTA
 * 2. El JSON contiene:
 *    - Versión disponible
 *    - URL del firmware binario
 *    - Checksum MD5
 * 3. Compara versión actual vs disponible
 * 4. Si hay actualización → descarga y flashea
 * 5. Reinicia automáticamente con nuevo firmware
 * 
 * Arquitectura:
 * ┌─────────┐    HTTPS    ┌──────────────┐
 * │  ESP32  │ ──────────> │ ota.riosvivos│
 * │         │ <────────── │    .org      │
 * └─────────┘   JSON+BIN  └──────────────┘
 * 
 * Formato del JSON (movocs.json):
 * {
 *   "version": "1.0.4",
 *   "url": "https://ota.riosvivos.org/firmware.bin",
 *   "md5": "abc123..."
 * }
 */

// ============================================================================
// CONFIGURACIÓN OTA
// ============================================================================

struct OtaConfig {
  const char *json_url;
  const char *version;
  uint32_t check_interval_ms;
};

extern bool wifi_connected;

static unsigned long lastOtaCheckMs = 0;

inline const char *getFirmwareVersion() {
  return CFG_FIRMWARE_VERSION;
}

/**
 * @def OTA_VERSION
 * @brief Versión actual del firmware
 * 
 * Formato: "MAJOR.MINOR.PATCH"
 * Ejemplo: "1.0.3"
 * 
 * Esta versión se compara con la del JSON del servidor.
 * Si la versión del servidor es mayor → se descarga e instala.
 * 
 * ⚠️ IMPORTANTE: Incrementar este valor al actualizar el código
 * 
 * Historial de versiones:
 * - 1.0.0: Versión inicial
 * - 1.0.1: Mejoras en estabilidad WiFi
 * - 1.0.2: Corrección de bugs en MQTT
 * - 1.0.3: Eliminada calibración remota
 * - 2.0.0: Versión actual
 * Version is sourced from projects/air-monitor-JJR/VERSION at build time.
 */

// ============================================================================
// DECLARACIONES DE FUNCIONES
// ============================================================================

const char* errtext(int code);
void checkOTAUpdate(const OtaConfig &config);
void checkOTAUpdate();
void setupOTA();
void loopOTA();

// ============================================================================
// IMPLEMENTACIÓN
// ============================================================================

/**
 * @brief Convierte código numérico de error a descripción legible
 * 
 * La librería ESP32OTAPull retorna códigos numéricos que indican
 * el resultado de la operación OTA. Esta función los traduce
 * a mensajes en español para facilitar debugging.
 * 
 * Códigos de error comunes:
 * 
 * UPDATE_AVAILABLE (positivo):
 * - Hay actualización disponible pero no se instaló
 * - Puede indicar que se requiere acción manual
 * 
 * NO_UPDATE_PROFILE_FOUND:
 * - El JSON no contiene perfil para este dispositivo
 * - Verificar formato del JSON
 * 
 * NO_UPDATE_AVAILABLE:
 * - Versión del servidor <= versión actual
 * - No hay nada que actualizar
 * 
 * UPDATE_OK:
 * - Actualización completada exitosamente
 * - Normalmente seguido de reinicio automático
 * 
 * HTTP_FAILED:
 * - No se pudo descargar el JSON
 * - Verificar URL, conectividad, certificado SSL
 * 
 * WRITE_ERROR:
 * - Error escribiendo firmware en flash
 * - Puede indicar flash corrupta
 * 
 * JSON_PROBLEM:
 * - JSON mal formado o sin campos requeridos
 * - Verificar sintaxis del JSON en servidor
 * 
 * OTA_UPDATE_FAIL:
 * - Fallo al actualizar partición OTA
 * - Verificar partition table (debe tener 2 particiones OTA)
 * 
 * @param code Código de error de ESP32OTAPull
 * @return Descripción en español del error
 */
const char* errtext(int code) {
  switch(code) {
    case ESP32OTAPull::UPDATE_AVAILABLE:
      return "An update is available but wasn't installed";
      
    case ESP32OTAPull::NO_UPDATE_PROFILE_FOUND:
      return "No profile matches";
      
    case ESP32OTAPull::NO_UPDATE_AVAILABLE:
      return "Profile matched, but update not applicable";
      
    case ESP32OTAPull::UPDATE_OK:
      return "An update was done, but no reboot";
      
    case ESP32OTAPull::HTTP_FAILED:
      return "HTTP GET failure";
      
    case ESP32OTAPull::WRITE_ERROR:
      return "Write error";
      
    case ESP32OTAPull::JSON_PROBLEM:
      return "Invalid JSON";
      
    case ESP32OTAPull::OTA_UPDATE_FAIL:
      return "Update fail (no OTA partition?)";
      
    default:
      if (code > 0)
        return "Unexpected HTTP response code";
      break;
  }
  return "Unknown error";
}

/**
 * @brief Verifica y descarga actualizaciones OTA si están disponibles
 * 
 * Este es el punto de entrada principal para el sistema OTA.
 * Se llama típicamente:
 * - Al inicio del programa (setup)
 * - Con comando serial manual
 * - Periódicamente si se desea auto-actualización
 * 
 * Proceso completo:
 * 
 * 1. DESCARGA JSON
 *    - Se conecta a OTA_JSON_URL vía HTTPS
 *    - Descarga archivo movocs.json
 *    - Ejemplo de JSON:
 *      {
 *        "version": "1.0.4",
 *        "url": "https://ota.riosvivos.org/firmware_1.0.4.bin",
 *        "md5": "abc123def456..."
 *      }
 * 
 * 2. PARSEO Y COMPARACIÓN
 *    - Extrae campo "version"
 *    - Compara con OTA_VERSION actual (1.0.3)
 *    - Comparación semántica: 1.0.4 > 1.0.3
 * 
 * 3. DESCARGA DE BINARIO (si version nueva > actual)
 *    - Descarga desde campo "url" del JSON
 *    - Descarga directa a partición OTA_1
 *    - Muestra progreso en Serial (%)
 * 
 * 4. VALIDACIÓN
 *    - Calcula MD5 del binario descargado
 *    - Compara con campo "md5" del JSON
 *    - Si no coincide → aborta actualización
 * 
 * 5. ACTIVACIÓN
 *    - Marca partición OTA_1 como booteable
 *    - Invalida partición OTA_0 como secundaria
 *    - Configura bootloader para boot desde OTA_1
 * 
 * 6. REINICIO
 *    - ESP32 se reinicia automáticamente
 *    - Bootloader carga nuevo firmware
 *    - Si nuevo firmware falla → rollback a OTA_0
 * 
 * Seguridad:
 * - HTTPS con validación de certificado
 * - Checksum MD5 para integridad
 * - Rollback automático si falla el boot
 * - No se sobrescribe firmware actual hasta validar
 * 
 * Manejo de errores:
 * - Todos los errores se reportan por Serial
 * - Códigos de error traducidos por errtext()
 * - No reinicia si hay error (mantiene firmware actual)
 * 
 * Consideraciones:
 * - Función BLOQUEANTE (puede tardar 30+ segundos)
 * - Requiere WiFi conectado
 * - Consume memoria durante descarga (~32KB buffer)
 * - Descarga típica: 500KB-1MB @ 100KB/s = 5-10 segundos
 * 
 * @note NO llamar en loop() (solo al inicio o manualmente)
 * @note Si encuentra actualización, el ESP32 se reiniciará
 * @note Imprimir progreso para feedback al usuario
 * 
 * Ejemplo de salida exitosa:
 * ▶ Verificando actualizaciones OTA...
 * Conectando a https://ota.riosvivos.org/movocs.json
 * Versión actual: 1.0.3
 * Versión disponible: 1.0.4
 * Descargando firmware... 25%... 50%... 75%... 100%
 * Validando MD5... OK
 * Actualizando partición OTA...
 * Resultado: An update was done, but no reboot
 * Reiniciando...
 * [ESP32 se reinicia con nueva versión]
 */
void checkOTAUpdate(const OtaConfig &config) {
  Serial.println("\n▶ Verificando actualizaciones OTA...");
  
  ESP32OTAPull ota;
  int ret = ota.CheckForOTAUpdate(config.json_url, config.version);
  
  Serial.printf("  Resultado: %s\n", errtext(ret));

  if (ret == ESP32OTAPull::UPDATE_OK) {
    sysStatusAddEvent(SysSeverity::info, "ota", "Firmware actualizado – reiniciando");
  } else if (ret == ESP32OTAPull::NO_UPDATE_AVAILABLE ||
             ret == ESP32OTAPull::NO_UPDATE_PROFILE_FOUND) {
    sysStatusAddEvent(SysSeverity::info, "ota", "OTA check: firmware actualizado");
  } else if (ret == ESP32OTAPull::HTTP_FAILED) {
    sysStatusAddEvent(SysSeverity::warning, "ota", "OTA HTTP fallido");
  } else if (ret > 0) {
    // Código HTTP inesperado
    char msg[64];
    snprintf(msg, sizeof(msg), "OTA HTTP %d: %s", ret, errtext(ret));
    sysStatusAddEvent(SysSeverity::warning, "ota", msg);
  } else if (ret < 0) {
    char msg[64];
    snprintf(msg, sizeof(msg), "OTA error %d: %s", ret, errtext(ret));
    sysStatusAddEvent(SysSeverity::error, "ota", msg);
  }
}

void checkOTAUpdate() {
  const OtaConfig config = {CFG_OTA_JSON_URL, CFG_FIRMWARE_VERSION, CFG_OTA_CHECK_INTERVAL_MS};
  checkOTAUpdate(config);
}

void setupOTA() {
  lastOtaCheckMs = 0;
}

void loopOTA() {
  if (!wifi_connected) {
    return;
  }

  const unsigned long now = millis();
  if (lastOtaCheckMs == 0 || now - lastOtaCheckMs >= CFG_OTA_CHECK_INTERVAL_MS) {
    lastOtaCheckMs = now;
    checkOTAUpdate();
  }
}

#endif
