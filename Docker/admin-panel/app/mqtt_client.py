from __future__ import annotations

import json
import uuid

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

    topic = f'{config["MQTT_TOPIC_BASE"]}/{device_id}'

    mqtt_publish.single(
        topic,
        payload=json.dumps(payload),
        hostname=config["MQTT_HOST"],
        port=config["MQTT_PORT"],
    )
