"""Reenvía al broker secundario la telemetría de estaciones con firmware viejo.

Desde 1.0.3 el firmware publica él mismo en mqtt.riosvivos.org. Las estaciones
que quedaron en campo con un firmware anterior (sin broker secundario) y que no
se pueden actualizar por OTA solo publican en el broker local; este servicio
replica su telemetría con el mismo topic y payload que usaría 1.0.3.
"""

import json
import logging
import os

import paho.mqtt.client as mqtt

LOCAL_HOST = os.environ.get("MQTT_HOST", "mosquitto")
LOCAL_PORT = int(os.environ.get("MQTT_PORT", "1883"))
LOCAL_TOPIC_BASE = os.environ.get("MQTT_TOPIC_BASE", "monitoreo").rstrip("/")

REMOTE_HOST = os.environ.get("BRIDGE_REMOTE_HOST", "mqtt.riosvivos.org")
REMOTE_PORT = int(os.environ.get("BRIDGE_REMOTE_PORT", "1883"))
REMOTE_TOPIC_BASE = os.environ.get("BRIDGE_REMOTE_TOPIC_BASE", "riosvivos/monitoring").rstrip("/")

# Versiones de firmware cuya telemetría se reenvía (separadas por coma)
FW_VERSIONS = {
    v.strip() for v in os.environ.get("BRIDGE_FW_VERSIONS", "1.0.0").split(",") if v.strip()
}

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("mqtt-bridge")

remote = mqtt.Client(client_id=f"sensio-bridge-{os.getpid()}", clean_session=True)
remote.reconnect_delay_set(min_delay=1, max_delay=60)


def on_remote_connect(client, userdata, flags, rc):
    log.info("Broker secundario %s:%s conectado (rc=%s)", REMOTE_HOST, REMOTE_PORT, rc)


def on_remote_disconnect(client, userdata, rc):
    log.warning("Broker secundario desconectado (rc=%s)", rc)


def on_local_connect(client, userdata, flags, rc):
    log.info("Broker local %s:%s conectado (rc=%s)", LOCAL_HOST, LOCAL_PORT, rc)
    client.subscribe(f"{LOCAL_TOPIC_BASE}/+", qos=0)


def on_local_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload)
    except (ValueError, UnicodeDecodeError):
        return
    if not isinstance(data, dict) or data.get("command") != "telemetry":
        return  # comandos del admin panel y respuestas de las estaciones

    fw = (data.get("metadata") or {}).get("device", {}).get("fw")
    if fw not in FW_VERSIONS:
        return

    device_id = msg.topic.rsplit("/", 1)[-1]
    topic = f"{REMOTE_TOPIC_BASE}/{device_id}"
    if not remote.is_connected():
        log.warning("%s (fw %s): broker secundario no conectado, se omite", device_id, fw)
        return
    result = remote.publish(topic, msg.payload, qos=0)
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        log.info("%s (fw %s) -> %s", device_id, fw, topic)
    else:
        log.warning("%s (fw %s): error publicando en %s (rc=%s)", device_id, fw, topic, result.rc)


def main():
    log.info("Reenviando fw %s: %s/+ -> %s:%s %s/+",
             sorted(FW_VERSIONS), LOCAL_TOPIC_BASE, REMOTE_HOST, REMOTE_PORT, REMOTE_TOPIC_BASE)

    remote.on_connect = on_remote_connect
    remote.on_disconnect = on_remote_disconnect
    remote.connect_async(REMOTE_HOST, REMOTE_PORT, keepalive=60)
    remote.loop_start()

    local = mqtt.Client(client_id=f"sensio-bridge-local-{os.getpid()}", clean_session=True)
    local.on_connect = on_local_connect
    local.on_message = on_local_message
    local.reconnect_delay_set(min_delay=1, max_delay=60)
    local.connect_async(LOCAL_HOST, LOCAL_PORT, keepalive=60)
    local.loop_forever(retry_first_connection=True)


if __name__ == "__main__":
    main()
