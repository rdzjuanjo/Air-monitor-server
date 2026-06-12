from __future__ import annotations

import os

import yaml


def load_hidden_devices(path: str) -> set[str]:
    if not os.path.exists(path):
        return set()
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    return set(data.get("hidden", []))


def hide_device(path: str, device_id: str) -> None:
    hidden = load_hidden_devices(path)
    hidden.add(device_id)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump({"hidden": sorted(hidden)}, f)
