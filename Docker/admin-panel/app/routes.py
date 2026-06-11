from __future__ import annotations

import hashlib
import json
import os
import re

from flask import (
    Blueprint,
    current_app,
    flash,
    redirect,
    render_template,
    request,
    url_for,
)
from flask_login import login_required, login_user, logout_user

from .auth import User, verify_password
from .influx_client import get_recent_devices
from .mqtt_client import publish_remote_action

bp = Blueprint("main", __name__)

DEVICE_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,31}$")
OTA_VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")

OTA_JSON_FILENAME = "narizdigital.json"
OTA_BIN_FILENAME = "narizdigital.bin"


def _ota_paths(config):
    ota_dir = config["OTA_DIR"]
    return os.path.join(ota_dir, OTA_JSON_FILENAME), os.path.join(ota_dir, OTA_BIN_FILENAME)


def _ota_bin_url(config):
    return f"{config['OTA_BASE_URL']}/{OTA_BIN_FILENAME}"


def _load_ota_manifest(config):
    json_path, _ = _ota_paths(config)
    if os.path.exists(json_path):
        with open(json_path) as f:
            return json.load(f)
    return {"version": "1.0.0", "url": _ota_bin_url(config), "md5": ""}


@bp.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        username = request.form.get("username", "").strip()
        password = request.form.get("password", "")
        users = current_app.config["USERS"]

        if verify_password(users, username, password):
            login_user(User(username))
            return redirect(url_for("main.index"))

        flash("Usuario o contraseña incorrectos.", "error")

    return render_template("login.html")


@bp.route("/logout")
@login_required
def logout():
    logout_user()
    return redirect(url_for("main.login"))


@bp.route("/")
@login_required
def index():
    devices = get_recent_devices(current_app.config)
    return render_template("devices.html", devices=devices)


@bp.route("/devices/<device_id>/calibrate", methods=["POST"])
@login_required
def calibrate(device_id: str):
    raw_value = request.form.get("value", "")
    try:
        value = float(raw_value)
    except ValueError:
        flash(f"Valor de calibracion invalido: '{raw_value}'.", "error")
        return redirect(url_for("main.index"))

    publish_remote_action(
        current_app.config,
        device_id,
        action="calibrate_mq135",
        target="mq135",
        params={"mode": "manual", "value": value},
    )
    flash(f"Comando de calibracion enviado a {device_id}.", "success")
    return redirect(url_for("main.index"))


@bp.route("/devices/<device_id>/coordinates", methods=["POST"])
@login_required
def set_coordinates(device_id: str):
    try:
        lat = float(request.form.get("lat", ""))
        lon = float(request.form.get("lon", ""))
    except ValueError:
        flash("Latitud/longitud invalidas.", "error")
        return redirect(url_for("main.index"))

    if not (-90 <= lat <= 90) or not (-180 <= lon <= 180):
        flash("Latitud/longitud fuera de rango.", "error")
        return redirect(url_for("main.index"))

    publish_remote_action(
        current_app.config,
        device_id,
        action="set_coordinates",
        params={"lat": lat, "lon": lon},
    )
    flash(f"Comando de coordenadas enviado a {device_id}.", "success")
    return redirect(url_for("main.index"))


@bp.route("/devices/<device_id>/rename", methods=["POST"])
@login_required
def rename_device(device_id: str):
    new_id = request.form.get("new_device_id", "").strip()

    if not DEVICE_ID_RE.match(new_id):
        flash("ID de dispositivo invalido (max 31 caracteres, letras, numeros, '-' y '_').", "error")
        return redirect(url_for("main.index"))

    publish_remote_action(
        current_app.config,
        device_id,
        action="setDeviceId",
        params={"deviceId": new_id},
    )
    flash(f"Comando de cambio de ID enviado a {device_id} -> {new_id}.", "success")
    return redirect(url_for("main.index"))


@bp.route("/ota", methods=["GET"])
@login_required
def ota():
    manifest = _load_ota_manifest(current_app.config)
    _, bin_path = _ota_paths(current_app.config)
    has_firmware = os.path.exists(bin_path)
    return render_template("ota.html", manifest=manifest, has_firmware=has_firmware)


@bp.route("/ota", methods=["POST"])
@login_required
def update_ota():
    version = request.form.get("version", "").strip()
    if not OTA_VERSION_RE.match(version):
        flash("Version invalida. Formato esperado: X.Y.Z", "error")
        return redirect(url_for("main.ota"))

    file = request.files.get("firmware")
    if not file or file.filename == "":
        flash("Debes seleccionar un archivo .bin de firmware.", "error")
        return redirect(url_for("main.ota"))

    if not file.filename.lower().endswith(".bin"):
        flash("El archivo debe tener extension .bin.", "error")
        return redirect(url_for("main.ota"))

    ota_dir = current_app.config["OTA_DIR"]
    os.makedirs(ota_dir, exist_ok=True)
    json_path, bin_path = _ota_paths(current_app.config)

    file.save(bin_path)

    md5 = hashlib.md5()
    with open(bin_path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            md5.update(chunk)

    manifest = {
        "version": version,
        "url": _ota_bin_url(current_app.config),
        "md5": md5.hexdigest(),
    }
    with open(json_path, "w") as f:
        json.dump(manifest, f, indent=2)

    flash(f"Firmware actualizado a la version {version}.", "success")
    return redirect(url_for("main.ota"))
