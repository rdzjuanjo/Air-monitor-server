#ifndef MQTT_PAYLOAD_H
#define MQTT_PAYLOAD_H

/**
 * @file mqtt_payload.h
 * @brief Construcción de topics, payloads JSON y metadatos de dispositivo para MQTT.
 *
 * Consolida en un solo lugar: metadatos de dispositivo, topic de telemetría
 * y payload JSON estándar.
 */

#include <functional>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "NvsConfig.h"
#include "config.h"

extern bool wifi_connected;
extern PubSubClient mqttClient;
const char *getFirmwareVersion();

// ---------------------------------------------------------------------------
// Hooks de proyecto: métricas e instrumentos
// [REUSABLE] El módulo no sabe nada del sensor. El proyecto registra sus callbacks.
// Soporta hasta 4 instrumentos simultáneos mediante listas de fillers.
// ---------------------------------------------------------------------------

using MetricsFillerFn    = std::function<void(JsonObject&)>;
using InstrumentFillerFn = std::function<void(JsonObject&)>;

static constexpr uint8_t MQTT_MAX_INSTRUMENTS = 4;
static MetricsFillerFn    sMetricsFillers[MQTT_MAX_INSTRUMENTS];
static InstrumentFillerFn sInstrumentFillers[MQTT_MAX_INSTRUMENTS];
static uint8_t sMetricsFillerCount    = 0;
static uint8_t sInstrumentFillerCount = 0;

// Append — llamar desde MQInstrument::registerHooks() una vez por instancia.
inline void mqttPayloadAddMetricsFiller(MetricsFillerFn fn) {
  if (sMetricsFillerCount < MQTT_MAX_INSTRUMENTS) sMetricsFillers[sMetricsFillerCount++] = fn;
}
inline void mqttPayloadAddInstrumentFiller(InstrumentFillerFn fn) {
  if (sInstrumentFillerCount < MQTT_MAX_INSTRUMENTS) sInstrumentFillers[sInstrumentFillerCount++] = fn;
}

// ---------------------------------------------------------------------------
// Metadatos de dispositivo (firmware, WiFi, calibración)
// ---------------------------------------------------------------------------

static void fillDeviceMetadata(JsonObject metadata) {
  if (metadata.isNull()) {
    return;
  }

  JsonObject device = metadata.createNestedObject("device");
  const char *firmwareVersion = getFirmwareVersion();
  device["fw"] = firmwareVersion && firmwareVersion[0] ? firmwareVersion : "unknown";

  JsonObject wifiMeta = device.createNestedObject("wifi");
  wifiMeta["up"] = wifi_connected;
  wifiMeta["mqtt"] = mqttClient.connected();
  if (wifi_connected) {
    wifiMeta["rssi"] = WiFi.RSSI();
  }

  JsonObject instruments = device.createNestedObject("instruments");
  for (uint8_t i = 0; i < sInstrumentFillerCount; i++) {
    if (sInstrumentFillers[i]) sInstrumentFillers[i](instruments);
  }
}

// ---------------------------------------------------------------------------
// Topic y payload de telemetría
// ---------------------------------------------------------------------------

static String buildTopic() {
  String topic = CFG_TELEMETRY_TOPIC_BASE;
  topic += nvsGetDeviceId();
  return topic;
}

static String buildJSONPayload(float longitude, float latitude) {
  DynamicJsonDocument doc(768);

  doc["command"] = "telemetry";

  JsonObject metrics = doc.createNestedObject("metrics");
  for (uint8_t i = 0; i < sMetricsFillerCount; i++) {
    if (sMetricsFillers[i]) sMetricsFillers[i](metrics);
  }

  JsonObject metadata = doc.createNestedObject("metadata");
  metadata["coord_x"] = longitude;
  metadata["coord_y"] = latitude;
  fillDeviceMetadata(metadata);

  String output;
  serializeJson(doc, output);
  Serial.printf("Payload size: %d bytes\n", output.length());
  return output;
}

#endif // MQTT_PAYLOAD_H
