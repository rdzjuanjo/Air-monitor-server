// [REUSABLE] Acciones remotas MQTT de nivel base: coordenadas y deviceId.
// No depende de ningún struct de proyecto — usa los accessors de NvsConfig.h.
//
// Uso: llamar registerBuiltinRemoteActions() desde main.cpp ANTES de libSetup,
// después de incluir app_runner.h (mqtt_handlers.h ya disponible).
#ifndef REMOTE_ACTIONS_H
#define REMOTE_ACTIONS_H

#include "NvsConfig.h"
#include "mqtt_handlers.h"

// ============================================================================
// APLICADORES — lógica pura, sin dependencia de MQTT
// ============================================================================

inline bool builtinSetCoordinates(float lat, float lon, String* detail = nullptr) {
  if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) {
    if (detail) *detail = "Coordenadas fuera de rango";
    return false;
  }
  nvsSetLatitude(lat);
  nvsSetLongitude(lon);
  saveConfig();
  if (detail) *detail = String("Coords: ") + String(lat, 5) + ", " + String(lon, 5);
  return true;
}

inline bool builtinSetDeviceId(const char* newId, String* detail = nullptr) {
  const size_t len = newId ? strlen(newId) : 0;
  if (len == 0) {
    if (detail) *detail = "Falta params.deviceId";
    return false;
  }
  if (len >= sizeof(BaseConfig::device_id)) {
    if (detail) *detail = "deviceId demasiado largo (max 31 chars)";
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    const char c = newId[i];
    if (!isAlphaNumeric(c) && c != '-' && c != '_') {
      if (detail) *detail = "deviceId solo puede contener letras, numeros, '-' y '_'";
      return false;
    }
  }
  nvsSetDeviceId(newId);
  saveConfig();
  if (detail) *detail = String("deviceId: ") + newId;
  return true;
}

// ============================================================================
// REGISTRO — conecta los aplicadores con el sistema MQTT
// ============================================================================

inline void registerBuiltinRemoteActions() {
  mqttHandlerRegisterAction("set_coordinates",
    [](const JsonDocument& doc, const char* /*topic*/, String& detail) -> bool {
      const bool hasLat = doc["params"]["lat"].is<float>() || doc["params"]["lat"].is<int>();
      const bool hasLon = doc["params"]["lon"].is<float>() || doc["params"]["lon"].is<int>();
      if (hasLat && hasLon) {
        return builtinSetCoordinates(
          doc["params"]["lat"].as<float>(),
          doc["params"]["lon"].as<float>(),
          &detail);
      }
      detail = "Faltan params.lat y params.lon";
      return false;
    });

  mqttHandlerRegisterAction("setDeviceId",
    [](const JsonDocument& doc, const char* /*topic*/, String& detail) -> bool {
      return builtinSetDeviceId(doc["params"]["deviceId"] | "", &detail);
    });
}

#endif // REMOTE_ACTIONS_H
