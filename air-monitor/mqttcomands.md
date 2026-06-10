# Comandos MQTT — Air Monitor JJR

Referencia de todos los mensajes MQTT de entrada y salida del firmware.

## Topic

El dispositivo publica y escucha **en el mismo topic**:

```
riosvivos/monitoring/{device_id}
```

`device_id` se configura en el webserver (sección Avanzados) y se persiste en NVS. El dispositivo se suscribe a este topic al conectarse al broker.

- **Broker**: `mqtt.riosvivos.org:1883`
- **QoS**: 0 (fire and forget)
- **Retain**: false
- **Clean session**: true
- **Buffer**: 768 bytes
- **Client ID**: `ESP32Client-{hex_aleatorio}` (cambia por reconexión)

---

## Salida — Dispositivo → Broker

### 1. `telemetry`

Publicado automáticamente cada **20 minutos** (`CFG_SAMPLING_PUBLISH_INTERVAL_MS`) mientras hay conexión MQTT activa. Si no hay conexión, simplemente no se envía (no hay cola/almacenamiento offline).

```json
{
  "command": "telemetry",
  "metrics": {
    "CVOL": 452.3,
    "mq135_adc": 1826
  },
  "metadata": {
    "coord_x": -103.2000,
    "coord_y": 20.5181,
    "device": {
      "fw": "2.0.0",
      "wifi": {
        "up": true,
        "mqtt": true,
        "rssi": -68
      },
      "instruments": {
        "mq135_cal": true,
        "mq135_adc0": 2000
      }
    }
  }
}
```

| Campo | Tipo | Descripción |
|---|---|---|
| `command` | string | Siempre `"telemetry"` |
| `metrics.CVOL` | float | Concentración de COVs en ppm (sensor `mq135`) |
| `metrics.mq135_adc` | int | Valor ADC filtrado (EMA) del sensor `mq135` |
| `metadata.coord_x` | float | Longitud GPS (con ruido aleatorio ±~1 m) |
| `metadata.coord_y` | float | Latitud GPS (con ruido aleatorio ±~1 m) |
| `metadata.device.fw` | string | Versión del firmware |
| `metadata.device.wifi.up` | bool | Conectado a la red WiFi (STA) |
| `metadata.device.wifi.mqtt` | bool | Conectado al broker MQTT |
| `metadata.device.wifi.rssi` | int | Señal WiFi en dBm (solo si `wifi.up`) |
| `metadata.device.instruments.mq135_cal` | bool | Sensor `mq135` calibrado |
| `metadata.device.instruments.mq135_adc0` | int | Valor ADC0 de calibración del `mq135` |

`metrics` y `metadata.device.instruments` se construyen dinámicamente: cada instrumento registrado (p. ej. `MQInstrument mq135`) agrega sus propios campos con prefijo `<id>_`.

---

### 2. `remote_action_result`

Respuesta a cualquier [`remote_action`](#1-remote_action) entrante.
Se publica **en el mismo topic** del que llegó el comando.

```json
{
  "command": "remote_action_result",
  "action": "calibrate_mq135",
  "actionId": "abc-123",
  "target": "mq135",
  "ok": true,
  "status": "success",
  "detail": "ADC0=2000 guardado"
}
```

`actionId` y `target` se reenvían tal como llegaron en el comando. `detail` solo se incluye si el handler devolvió un texto.

| Campo `status` | Significado |
|---|---|
| `"success"` | Acción ejecutada correctamente |
| `"failed"` | Acción reconocida pero falló (parámetros inválidos, etc.) |
| `"unsupported"` | Acción no registrada en el firmware |

---

## Entrada — Broker → Dispositivo

Todos los comandos se reciben en el topic del dispositivo: `riosvivos/monitoring/{device_id}`

---

### 1. `remote_action`

Ejecuta una acción en el dispositivo. El resultado llega como [`remote_action_result`](#2-remote_action_result).

```json
{
  "command": "remote_action",
  "action": "<nombre_de_acción>",
  "actionId": "abc-123",
  "target": "AABBCC",
  "params": { }
}
```

`actionId` y `target` son opcionales y solo se reenvían en la respuesta para correlación; el firmware no los valida.

#### Acciones disponibles

##### `calibrate_mq135` — Calibrar el sensor MQ135

```json
{
  "command": "remote_action",
  "action": "calibrate_mq135",
  "actionId": "cal-mq135-001",
  "target": "mq135",
  "params": {
    "mode": "manual",
    "value": 2000
  }
}
```

| Parámetro | Tipo | Descripción |
|---|---|---|
| `params.mode` | string | Debe ser `"manual"` (cualquier otro valor es rechazado) |
| `params.value` | float | Valor ADC0 en aire limpio |

El nombre de la acción es `calibrate_<id>`, donde `<id>` es el identificador del instrumento (`"mq135"` en este proyecto). Si se añade otro sensor MQ, se registra automáticamente su propia acción `calibrate_<id>`.

---

##### `set_coordinates` — Actualizar coordenadas GPS

```json
{
  "command": "remote_action",
  "action": "set_coordinates",
  "actionId": "gps-001",
  "params": {
    "lat": 20.5181,
    "lon": -103.2000
  }
}
```

| Parámetro | Tipo | Rango |
|---|---|---|
| `params.lat` | float | -90 a 90 |
| `params.lon` | float | -180 a 180 |

Se persiste en NVS inmediatamente.

---

##### `setDeviceId` — Cambiar el ID del dispositivo

```json
{
  "command": "remote_action",
  "action": "setDeviceId",
  "actionId": "sid-001",
  "params": {
    "deviceId": "NUEVO_ID"
  }
}
```

| Parámetro | Tipo | Descripción |
|---|---|---|
| `params.deviceId` | string | Nuevo ID (max 31 chars, solo letras, números, `-` y `_`) |

El cambio se persiste en NVS y afecta inmediatamente al topic MQTT, al AP y a los payloads de telemetría. No reinicia el dispositivo.

---

##### `reboot` — Reiniciar el dispositivo

```json
{
  "command": "remote_action",
  "action": "reboot",
  "actionId": "rbt-001"
}
```

Sin parámetros. El dispositivo envía el `remote_action_result` confirmando (`detail: "Reiniciando en 1s"`) **antes** de reiniciar (~1 s después).

---

## Resumen rápido

| Dirección | Comando | Topic |
|---|---|---|
| ↑ salida | `telemetry` | `riosvivos/monitoring/{device_id}` |
| ↑ salida | `remote_action_result` | topic del comando recibido |
| ↓ entrada | `remote_action` → `calibrate_mq135` | `riosvivos/monitoring/{device_id}` |
| ↓ entrada | `remote_action` → `set_coordinates` | `riosvivos/monitoring/{device_id}` |
| ↓ entrada | `remote_action` → `setDeviceId` | `riosvivos/monitoring/{device_id}` |
| ↓ entrada | `remote_action` → `reboot` | `riosvivos/monitoring/{device_id}` |
