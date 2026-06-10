// [REUSABLE] Sin dependencias del sensor MQ135. Usable en cualquier sistema de monitoreo con ESP32.
#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

/**
 * @file system_status.h
 * @brief Modelo de estado global del dispositivo (simplificado, sin GSM/SD/batería)
 *
 * Trackea:
 *   - WiFi: connected, rssiDbm, ip
 *   - MQTT: connected, dropCount, lastPayloadBytes
 *   - Storage: fsFreeBytes, fsTotalBytes (LittleFS)
 *   - Telemetría: dropCount, lastPayloadBytes
 *   - Eventos: buffer circular de últimos 8 (severity: info/warning/error)
 */

#include <Arduino.h>
#include <ArduinoJson.h>

// ============================================================================
// TIPOS
// ============================================================================

enum class SysSeverity : uint8_t { info = 0, warning, error };

struct SysEvent {
  unsigned long tsMs;
  SysSeverity   severity;
  char          source[24];
  char          message[96];
};

static constexpr uint8_t SYS_EVENT_BUF_SIZE = 8;

// ============================================================================
// ESTADO INTERNO
// ============================================================================

// WiFi
static bool  sysWifiConnected  = false;
static int   sysWifiRssi       = 0;
static char  sysWifiIp[20]     = "";

// MQTT
static bool     sysMqttConnected      = false;
static uint32_t sysMqttDropCount      = 0;
static uint32_t sysMqttLastPayloadBytes = 0;

// Storage (LittleFS)
static size_t sysFsFreeBytes    = 0;
static size_t sysFsTotalBytes   = 0;

// Telemetría
static uint32_t sysTelDropCount      = 0;
static uint32_t sysTelLastPayloadBytes = 0;

// Buffer de eventos
static SysEvent sysEvents[SYS_EVENT_BUF_SIZE];
static uint8_t  sysEventHead  = 0;
static uint8_t  sysEventCount = 0;

// ============================================================================
// SETTERS
// ============================================================================

inline void sysStatusSetWifi(bool connected, int rssi, const char *ip) {
  sysWifiConnected = connected;
  sysWifiRssi      = rssi;
  if (ip) {
    strncpy(sysWifiIp, ip, sizeof(sysWifiIp) - 1);
    sysWifiIp[sizeof(sysWifiIp) - 1] = '\0';
  } else {
    sysWifiIp[0] = '\0';
  }
}

inline void sysStatusSetMqtt(bool connected) {
  sysMqttConnected = connected;
}

inline void sysStatusUpdateStorage(size_t totalBytes, size_t freeBytes) {
  sysFsTotalBytes = totalBytes;
  sysFsFreeBytes  = freeBytes;
}

inline void sysStatusRecordTelemetryDrop() {
  sysTelDropCount++;
  sysMqttDropCount++;
}

inline void sysStatusRecordTelemetrySent(size_t payloadBytes) {
  sysTelLastPayloadBytes  = (uint32_t)payloadBytes;
  sysMqttLastPayloadBytes = (uint32_t)payloadBytes;
}

inline void sysStatusAddEvent(SysSeverity severity, const char *source, const char *message) {
  SysEvent &ev = sysEvents[sysEventHead];
  ev.tsMs     = millis();
  ev.severity = severity;
  strncpy(ev.source,  source  ? source  : "", sizeof(ev.source)  - 1);
  ev.source[sizeof(ev.source) - 1]   = '\0';
  strncpy(ev.message, message ? message : "", sizeof(ev.message) - 1);
  ev.message[sizeof(ev.message) - 1] = '\0';

  sysEventHead = (sysEventHead + 1) % SYS_EVENT_BUF_SIZE;
  if (sysEventCount < SYS_EVENT_BUF_SIZE) sysEventCount++;
}

// ============================================================================
// LECTURA
// ============================================================================

inline const SysEvent *sysStatusLastEvent() {
  if (sysEventCount == 0) return nullptr;
  uint8_t last = (sysEventHead == 0) ? SYS_EVENT_BUF_SIZE - 1 : sysEventHead - 1;
  return &sysEvents[last];
}

inline const char *sysSeverityLabel(SysSeverity s) {
  switch (s) {
    case SysSeverity::info:    return "info";
    case SysSeverity::warning: return "warning";
    case SysSeverity::error:   return "error";
    default:                   return "unknown";
  }
}

// ============================================================================
// SERIALIZACIÓN
// ============================================================================

/**
 * Rellena un JsonObject existente con el estado actual del sistema.
 * Se usa en buildWsStatusJson() y /api/status.
 */
inline void buildSysStatusJson(JsonObject &obj) {
  obj["sys_wifi_connected"]  = sysWifiConnected;
  obj["sys_wifi_rssi"]       = sysWifiRssi;
  obj["sys_wifi_ip"]         = sysWifiIp;
  obj["sys_mqtt_connected"]  = sysMqttConnected;
  obj["sys_mqtt_drop_count"] = sysMqttDropCount;
  obj["sys_mqtt_last_bytes"] = sysMqttLastPayloadBytes;
  obj["sys_fs_free_bytes"]   = (uint32_t)sysFsFreeBytes;
  obj["sys_fs_total_bytes"]  = (uint32_t)sysFsTotalBytes;
  obj["sys_tel_drop_count"]  = sysTelDropCount;

  const SysEvent *ev = sysStatusLastEvent();
  if (ev) {
    JsonObject last = obj.createNestedObject("sys_last_event");
    last["ts_ms"]    = (unsigned long)ev->tsMs;
    last["severity"] = sysSeverityLabel(ev->severity);
    last["source"]   = ev->source;
    last["message"]  = ev->message;
  }
}

#endif // SYSTEM_STATUS_H
