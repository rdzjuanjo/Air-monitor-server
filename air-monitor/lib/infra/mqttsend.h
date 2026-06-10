// [ADAPTABLE] Infraestructura MQTT genérica. TODO:SENSOR — reemplazar llamadas a
// getMQ135Reading() y getFilteredADC() con las funciones del sensor correspondiente.
#ifndef MQTTSEND_H
#define MQTTSEND_H

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "NvsConfig.h"
#include "config.h"

/**
 * @file mqttsend.h
 * @brief Comunicación MQTT para envío de datos del sensor a la nube
 * 
 * Este módulo maneja:
 * - Conexión al broker MQTT
 * - Publicación periódica de datos del sensor
 * - Reconexión automática en caso de pérdida de conexión
 * - Construcción de topics y payloads JSON
 * 
 * Arquitectura MQTT:
 * - Protocol: MQTT 3.1.1
 * - QoS: 0 (At most once - fire and forget)
 * - Retain: false
 * - Clean Session: true
 * - Buffer size: 512 bytes (para mensajes más grandes)
 */

// ============================================================================
// CONFIGURACIÓN DEL BROKER MQTT
// ============================================================================

#define MQTT_BROKER CFG_TELEMETRY_BROKER
#define MQTT_PORT   CFG_TELEMETRY_PORT

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

extern float lat;
extern float lon;
extern WiFiClient espClient;
extern PubSubClient mqttClient;
extern unsigned long lastMQTTSend;
extern const unsigned long MQTT_INTERVAL;
extern bool mqttInitialized;

// ============================================================================
// DECLARACIONES DE FUNCIONES
// ============================================================================

void setupMQTT();
void loopMQTT();
void reconnectMQTT();
void sendMQTTData();
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool isMQTTConnected();
String getMQTTStatus();

enum class RemoteActionStatus : uint8_t { success = 0, failed, unsupported };

// ============================================================================
// IMPLEMENTACIÓN
// ============================================================================

float lat;
float lon;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMQTTSend = 0;
const unsigned long MQTT_INTERVAL = CFG_SAMPLING_PUBLISH_INTERVAL_MS;
bool mqttInitialized = false;
static bool pendingRemoteActionResponse = false;
static char pendingRemoteActionTopic[160] = {0};
static char pendingRemoteActionPayload[512] = {0};
static char lastMqttPublishTopic[160] = {0};
static char lastMqttPublishPayload[768] = {0};
static bool lastMqttPublishSuccess = false;
static char lastMqttInboundTopic[160] = {0};
static char lastMqttInboundPayload[768] = {0};
static char lastMqttInboundCommand[48] = {0};
static unsigned long lastMqttInboundAtMs = 0;
static int lastMqttReconnectState = MQTT_DISCONNECTED;
static unsigned long lastMqttReconnectAtMs = 0;
static unsigned long nextMqttReconnectAttemptMs = 0;
static uint8_t mqttReconnectAttempts = 0;
static constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000UL;
static constexpr unsigned long MQTT_RECONNECT_BACKOFF_MS = 30000UL;
static constexpr uint8_t MQTT_RECONNECT_BURST_ATTEMPTS = 3;

// Forward declarations de funciones externas
extern bool wifi_connected;
const char *getFirmwareVersion();

const char *getLastMqttPublishTopic();
const char *getLastMqttPublishPayload();
bool getLastMqttPublishSuccess();
const char *getLastMqttInboundTopic();
const char *getLastMqttInboundPayload();
const char *getLastMqttInboundCommand();
unsigned long getLastMqttInboundAtMs();
int getLastMqttReconnectState();
unsigned long getLastMqttReconnectAtMs();

static void rememberLastMqttPublish(const char *topic, const char *payload, bool success) {
  if (topic && *topic) {
    snprintf(lastMqttPublishTopic, sizeof(lastMqttPublishTopic), "%s", topic);
  }
  if (payload && *payload) {
    snprintf(lastMqttPublishPayload, sizeof(lastMqttPublishPayload), "%s", payload);
  }
  lastMqttPublishSuccess = success;
}

static void rememberLastMqttInbound(const char *topic, const char *payload, const char *command) {
  if (topic && *topic) {
    snprintf(lastMqttInboundTopic, sizeof(lastMqttInboundTopic), "%s", topic);
  }
  if (payload && *payload) {
    snprintf(lastMqttInboundPayload, sizeof(lastMqttInboundPayload), "%s", payload);
  }
  if (command && *command) {
    snprintf(lastMqttInboundCommand, sizeof(lastMqttInboundCommand), "%s", command);
  } else {
    lastMqttInboundCommand[0] = '\0';
  }
  lastMqttInboundAtMs = millis();
}

static const char *mqttStateToString(int state) {
  switch (state) {
    case MQTT_CONNECTION_TIMEOUT: return "Timeout";
    case MQTT_CONNECTION_LOST: return "Conexión perdida";
    case MQTT_CONNECT_FAILED: return "Conexión fallida";
    case MQTT_DISCONNECTED: return "Desconectado";
    case MQTT_CONNECTED: return "Conectado";
    case MQTT_CONNECT_BAD_PROTOCOL: return "Protocolo incorrecto";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "ID cliente incorrecto";
    case MQTT_CONNECT_UNAVAILABLE: return "Servidor no disponible";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "Credenciales incorrectas";
    case MQTT_CONNECT_UNAUTHORIZED: return "No autorizado";
    default: return "Estado desconocido";
  }
}

#include "mqtt_payload.h"
#include "system_status.h"
#include "mqtt_handlers.h"

void setupMQTT() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  
  // Configurar buffer size a 512 bytes para mensajes más grandes
  mqttClient.setBufferSize(CFG_TELEMETRY_MQTT_PACKET_SIZE);
  
  mqttInitialized = true;
  
  Serial.println("MQTT inicializado");
  Serial.print("Broker: ");
  Serial.print(MQTT_BROKER);
  Serial.print(":");
  Serial.println(MQTT_PORT);
  Serial.printf("Buffer size: %d bytes\n", CFG_TELEMETRY_MQTT_PACKET_SIZE);
}

void loopMQTT() {
  // Guard clauses: No hacer nada si no estamos listos
  if (!mqttInitialized || !wifi_connected) {
    return;
  }

  // Mantener conexión activa
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();  // Procesa mensajes entrantes y mantiene keepalive

  if (pendingRemoteActionResponse) {
    flushPendingRemoteActionResponse();
  }

  // Enviar datos según intervalo configurado
  if (millis() - lastMQTTSend >= MQTT_INTERVAL) {
    sendMQTTData();
    lastMQTTSend = millis();
  }
}

void reconnectMQTT() {
  if (!mqttInitialized || !wifi_connected) return;

  const unsigned long now = millis();
  if (nextMqttReconnectAttemptMs != 0 && now < nextMqttReconnectAttemptMs) {
    return;
  }

  Serial.print("Intentando conexión MQTT...");
  lastMqttReconnectAtMs = now;

  // Generar Client ID único aleatorio
  String clientId = "ESP32Client-" + String(random(0xffff), HEX);

  // Intentar una sola vez por ciclo (no bloqueante)
  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("conectado!");
    lastMqttReconnectState = MQTT_CONNECTED;
    mqttReconnectAttempts = 0;
    nextMqttReconnectAttemptMs = 0;
    sysStatusSetMqtt(true);

    // Suscribirse al topic del dispositivo para recibir comandos
    String topic = buildTopic();
    if (mqttClient.subscribe(topic.c_str())) {
      Serial.println("Suscrito al topic: " + topic);
    } else {
      Serial.println("Error suscribiéndose al topic");
    }
    return;
  }

  // Fallo en conexión, programar próximo reintento sin bloquear el loop
  lastMqttReconnectState = mqttClient.state();
  mqttReconnectAttempts++;
  const unsigned long waitMs = mqttReconnectAttempts >= MQTT_RECONNECT_BURST_ATTEMPTS
      ? MQTT_RECONNECT_BACKOFF_MS
      : MQTT_RECONNECT_INTERVAL_MS;
  nextMqttReconnectAttemptMs = now + waitMs;
  sysStatusSetMqtt(false);

  Serial.print("falló, rc=");
  Serial.print(mqttClient.state());
  Serial.print(" reintento en ");
  Serial.print(waitMs / 1000UL);
  Serial.println("s");
}

void sendMQTTData() {
  if (!mqttClient.connected()) {
    Serial.println("MQTT no conectado, no se pueden enviar datos");
    return;
  }

  // Remove: #include "mq_remote.h"  — acciones específicas van en mq_init.h
  // Aplicar ruido aleatorio a coordenadas GPS (±1 metro aproximadamente)
  lat = nvsGetLatitude()  + (random(-1000, 1000) * 0.000001);
  lon = nvsGetLongitude() + (random(-1000, 1000) * 0.000001);

  // Construir mensaje MQTT (métricas provistas por el hook de proyecto)
  String topic = buildTopic();
  String payload = buildJSONPayload(lon, lat);
  
  // Publicar (QoS 0 = fire and forget, no retain)
  bool success = mqttClient.publish(topic.c_str(), payload.c_str());
  rememberLastMqttPublish(topic.c_str(), payload.c_str(), success);

  if (success) {
    sysStatusRecordTelemetrySent(payload.length());
    Serial.println("Datos enviados por MQTT:");
    Serial.println("Topic: " + topic);
    Serial.println("Payload: " + payload);
  } else {
    sysStatusRecordTelemetryDrop();
    Serial.println("Error enviando datos por MQTT");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Convertir payload a string null-terminated
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  // Debug: imprimir mensaje recibido
  Serial.print("Mensaje recibido [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  rememberLastMqttInbound(topic, message, "");

  if (handleRemoteActionCommand(topic, message)) {
    return;
  }
}

bool isMQTTConnected() {
  return mqttClient.connected();
}

String getMQTTStatus() {
  if (!mqttInitialized) return "No inicializado";

  return mqttStateToString(mqttClient.state());
}

const char *getLastMqttPublishTopic() { return lastMqttPublishTopic[0] ? lastMqttPublishTopic : ""; }
const char *getLastMqttPublishPayload() { return lastMqttPublishPayload[0] ? lastMqttPublishPayload : ""; }
bool getLastMqttPublishSuccess() { return lastMqttPublishSuccess; }
const char *getLastMqttInboundTopic() { return lastMqttInboundTopic[0] ? lastMqttInboundTopic : ""; }
const char *getLastMqttInboundPayload() { return lastMqttInboundPayload[0] ? lastMqttInboundPayload : ""; }
const char *getLastMqttInboundCommand() { return lastMqttInboundCommand[0] ? lastMqttInboundCommand : ""; }
unsigned long getLastMqttInboundAtMs() { return lastMqttInboundAtMs; }
int getLastMqttReconnectState() { return lastMqttReconnectState; }
unsigned long getLastMqttReconnectAtMs() { return lastMqttReconnectAtMs; }

#endif
