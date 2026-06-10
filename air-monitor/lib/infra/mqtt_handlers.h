#ifndef MQTT_HANDLERS_H
#define MQTT_HANDLERS_H

#include <functional>

/**
 * @file mqtt_handlers.h
 * @brief Despacho de remote_action.
 *
 * [REUSABLE] El módulo no conoce las acciones del proyecto.
 * El proyecto registra sus handlers con mqttHandlerRegisterAction().
 * Built-in siempre disponible: reboot.
 * Incluir únicamente desde mqttsend.h.
 */

// ---------------------------------------------------------------------------
// Registro de acciones remotas
// ---------------------------------------------------------------------------

using ActionFn = std::function<bool(const JsonDocument& doc, const char* topic, String& detail)>;

struct MqttActionEntry {
  char     action[32];
  ActionFn handler;
};

static MqttActionEntry sMqttActions[8];
static uint8_t         sMqttActionCount = 0;

inline void mqttHandlerRegisterAction(const char* action, ActionFn fn) {
  if (sMqttActionCount < 8) {
    strncpy(sMqttActions[sMqttActionCount].action, action, 31);
    sMqttActions[sMqttActionCount].action[31] = '\0';
    sMqttActions[sMqttActionCount].handler    = fn;
    sMqttActionCount++;
  }
}

static bool queueRemoteActionResponse(const char *topic, const char *payload) {
  if (!topic || !*topic || !payload || !*payload) {
    return false;
  }

  snprintf(pendingRemoteActionTopic, sizeof(pendingRemoteActionTopic), "%s", topic);
  snprintf(pendingRemoteActionPayload, sizeof(pendingRemoteActionPayload), "%s", payload);
  pendingRemoteActionResponse = true;
  return true;
}

static bool flushPendingRemoteActionResponse() {
  if (!pendingRemoteActionResponse || !pendingRemoteActionTopic[0] || !pendingRemoteActionPayload[0]) {
    return false;
  }

  const size_t payloadLength = strlen(pendingRemoteActionPayload);
  const bool sent = mqttClient.publish(pendingRemoteActionTopic, pendingRemoteActionPayload, payloadLength);
  rememberLastMqttPublish(pendingRemoteActionTopic, pendingRemoteActionPayload, sent);
  if (!sent) {
    return false;
  }

  pendingRemoteActionResponse = false;
  pendingRemoteActionTopic[0] = '\0';
  pendingRemoteActionPayload[0] = '\0';
  return true;
}

static bool handleRemoteActionCommand(const char *topic, const char *payload) {
  if (!topic || !*topic || !payload || !*payload) {
    return false;
  }

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    return false;
  }

  const char *command = doc["command"] | doc["cmd"] | "";
  if (String(command) != "remote_action") {
    return false;
  }

  rememberLastMqttInbound(topic, payload, command);

  const char *action   = doc["action"]   | "";
  const char *target   = doc["target"]   | "";
  const char *actionId = doc["actionId"] | "";

  if (!action || !*action) return false;

  char detail[160] = {0};
  RemoteActionStatus status = RemoteActionStatus::unsupported;
  bool handled = false;

  // --- Probar acciones registradas por el proyecto ---
  for (uint8_t i = 0; i < sMqttActionCount; i++) {
    if (String(action).equalsIgnoreCase(sMqttActions[i].action)) {
      String remoteDetail;
      const bool ok = sMqttActions[i].handler(doc, topic, remoteDetail);
      status = ok ? RemoteActionStatus::success : RemoteActionStatus::failed;
      if (remoteDetail.length()) snprintf(detail, sizeof(detail), "%s", remoteDetail.c_str());
      handled = true;
      break;
    }
  }

  // --- Built-ins genéricos ---
  if (!handled) {
    if (String(action).equalsIgnoreCase("reboot")) {
      StaticJsonDocument<256> rebootResp;
      rebootResp["command"]  = "remote_action_result";
      rebootResp["action"]   = "reboot";
      rebootResp["actionId"] = actionId;
      rebootResp["ok"]       = true;
      rebootResp["status"]   = "success";
      rebootResp["detail"]   = "Reiniciando en 1s";
      char rebootBuf[256] = {0};
      serializeJson(rebootResp, rebootBuf, sizeof(rebootBuf));
      mqttClient.publish(topic, rebootBuf, strlen(rebootBuf));
      mqttClient.loop();
      sysStatusAddEvent(SysSeverity::info, "cmd", "remote reboot");
      Serial.println("Accion remota: reboot en 1s");
      delay(1000);
      esp_restart();
      return true;
    } else {
      snprintf(detail, sizeof(detail), "Accion no soportada: %s", action);
      status = RemoteActionStatus::unsupported;
    }
  }

  StaticJsonDocument<384> response;
  response["command"] = "remote_action_result";
  response["action"] = action;
  response["actionId"] = actionId;
  response["target"] = target;
  response["ok"] = (status == RemoteActionStatus::success);
  response["status"] = status == RemoteActionStatus::success ? "success"
                       : status == RemoteActionStatus::unsupported ? "unsupported"
                                                                    : "failed";
  if (detail[0]) {
    response["detail"] = detail;
  }

  char buffer[512] = {0};
  const size_t len = serializeJson(response, buffer, sizeof(buffer));
  if (len == 0) {
    return false;
  }

  rememberLastMqttPublish(topic, buffer, true);
  queueRemoteActionResponse(topic, buffer);
  Serial.printf("Acción remota MQTT recibida: %s (%s)\n", action, target);
  return true;
}

#endif
