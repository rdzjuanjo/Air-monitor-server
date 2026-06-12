from __future__ import annotations

import json
import threading
import uuid

import paho.mqtt.client as mqtt
import paho.mqtt.publish as mqtt_publish


def publish_remote_action(config, device_id: str, action: str, params: dict | None = None, target: str | None = None) -> None:
    payload = {
        "command": "remote_action",
        "action": action,
        "actionId": str(uuid.uuid4()),
    }
    if target is not None:
        payload["target"] = target
    if params is not None:
        payload["params"] = params

    topic = f'{config["MQTT_TOPIC_BASE"].rstrip("/")}/{device_id}'

    mqtt_publish.single(
        topic,
        payload=json.dumps(payload),
        hostname=config["MQTT_HOST"],
        port=config["MQTT_PORT"],
    )


def publish_remote_action_and_wait(
    config,
    device_id: str,
    action: str,
    params: dict | None = None,
    target: str | None = None,
    timeout: float = 5.0,
) -> dict | None:
    """Publica una accion remota y espera su remote_action_result.

    Se suscribe al topic del dispositivo antes de publicar, para no
    perder la respuesta. Devuelve el payload de respuesta (dict) o
    None si no llego ninguna respuesta antes de `timeout` segundos.
    """
    action_id = str(uuid.uuid4())
    payload = {
        "command": "remote_action",
        "action": action,
        "actionId": action_id,
    }
    if target is not None:
        payload["target"] = target
    if params is not None:
        payload["params"] = params

    topic = f'{config["MQTT_TOPIC_BASE"].rstrip("/")}/{device_id}'

    result: dict | None = None
    received = threading.Event()

    def on_connect(client, userdata, flags, rc):
        client.subscribe(topic)
        client.publish(topic, json.dumps(payload))

    def on_message(client, userdata, msg):
        nonlocal result
        try:
            data = json.loads(msg.payload.decode("utf-8"))
        except ValueError:
            return
        if data.get("command") == "remote_action_result" and data.get("actionId") == action_id:
            result = data
            received.set()

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(config["MQTT_HOST"], config["MQTT_PORT"])
    client.loop_start()
    try:
        received.wait(timeout)
    finally:
        client.loop_stop()
        client.disconnect()

    return result
