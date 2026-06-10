# AirQ – Monitor de Calidad del Aire

Sistema de monitoreo de calidad del aire basado en ESP32 con sensor MQ135, desarrollado para el proyecto **Ríos Vivos** (El Salto, Jalisco). Publica telemetría vía MQTT, ofrece una interfaz web en tiempo real y soporta actualizaciones remotas de firmware (OTA).

**Versión:** 2.0.0 | **Hardware:** ESP32 DevKit v1 | **Sensor:** MQ135

---

## Características

- **Sensor MQ135** – Detecta COVs (NH₃, NOₓ, alcohol, benceno, humo, CO₂) en PPM con filtrado EMA
- **Portal cautivo** – AP WiFi local siempre activo (`AirQ-XXXXXX / 192.168.4.1`); iOS y Android abren el navegador automáticamente
- **Interfaz web** – Dashboard en tiempo real con historial de lecturas (WebSocket push cada 2 s)
- **MQTT** – Envío de telemetría JSON cada 20 minutos al reconectar a una red STA
- **Almacenamiento offline** – Buffer en LittleFS (hasta 96 KB en formato JSONL); se publica automáticamente al recuperar conexión
- **OTA** – Actualizaciones de firmware Over-The-Air desde `ota.riosvivos.org` sin cable USB
- **mDNS** – Accesible como `http://aire.local` cuando hay conexión STA
- **Consola serial** – Comandos de diagnóstico y calibración por puerto serie

---

## Hardware

| Componente | Detalle |
|---|---|
| Microcontrolador | ESP32 DevKit v1 |
| Sensor | MQ135 – pin GPIO 34 (ADC1_CH6) |
| Alimentación | 5 V USB o fuente externa |

---

## Primeros pasos

### 1. Clonar / abrir el proyecto

```bash
# El proyecto forma parte del mono-repositorio Ríos Vivos
cd monitoring-stations/projects/air-monitor-JJR
```

### 2. Configurar variables de entorno

Copia el archivo de ejemplo y edita los valores:

```bash
cp include/generated/dotenv_config.h.example include/generated/dotenv_config.h
```

El archivo debe definir al menos la URL del servidor OTA:

```c
#define OTA_JSON_URL "https://ota.riosvivos.org/movocs.json"
```

### 3. Compilar y subir

```bash
pio run --target upload
pio run --target uploadfs   # sube el sistema de archivos LittleFS (chart.umd.js)
```

### 4. Monitorear

```bash
pio device monitor          # 115200 baud
```

---

## Interfaz web

| Ruta | Método | Descripción |
|---|---|---|
| `/` | GET | Dashboard en tiempo real |
| `/config` | GET | Formulario de configuración |
| `/config` | POST | Guarda WiFi, device ID, coordenadas |
| `/api/status` | GET | JSON con estado completo del sistema |
| `/calibrate` | GET | Calibración rápida del sensor |
| `*` | ANY | Redirección al portal cautivo |

Cuando la estación se conecta a una red WiFi local también está disponible en **`http://aire.local`**.

---

## Consola serial (115200 baud)

| Comando | Acción |
|---|---|
| `help` | Lista todos los comandos |
| `status` | Estado del sistema, WiFi y MQTT |
| `read` | Lectura instantánea del sensor |
| `calibrate` | Inicia calibración en aire limpio |
| `autocal` | Calibración automática |
| `config` | Muestra configuración guardada |
| `resetwifi` | Borra credenciales WiFi y reinicia |

---

## Payload MQTT

Topic: `riosvivos/monitoring/{device_id}`

```json
{
  "command": "telemetry",
  "recordedAt": "2026-05-19T12:00:00Z",
  "metrics": {
    "CVOL": 142.5,
    "ADC": 1820
  },
  "metadata": {
    "coord_x": -103.177,
    "coord_y": 20.532,
    "device": {
      "fw": "2.0.0",
      "ntp": true,
      "wifi": {
        "up": true,
        "mqtt": true,
        "rssi": -65
      },
      "instruments": {
        "mq135_cal": true,
        "mq135_adc0": 2100
      }
    }
  }
}
```

| Campo | Descripción |
|---|---|
| `metrics.CVOL` | Concentración de COVs en PPM |
| `metrics.ADC` | Valor ADC filtrado (0–4095) |
| `metadata.coord_x` | Longitud geográfica |
| `metadata.coord_y` | Latitud geográfica |
| `metadata.device.ntp` | NTP sincronizado |
| `metadata.device.instruments.mq135_adc0` | Valor ADC en aire limpio (calibración) |

---

## Configuración inicial del dispositivo

### 1. Conectarse a la red del sensor

Al encender, el ESP32 crea una red WiFi propia. Busca en tu celular o computadora una red con el nombre:

```
Nariz Digital-XX
```

donde `XX` son los últimos bytes de la MAC del dispositivo. Conéctate a ella (sin contraseña por defecto).

### 2. Portal cautivo

En iOS y Android el navegador se abre automáticamente al conectarse. Si no ocurre, entra manualmente a:

```
http://192.168.4.1
```

Verás el dashboard en tiempo real del sensor.

### 3. Agregar credenciales WiFi

1. Abre la pestaña **Configuración** del portal.
2. Ingresa el SSID y contraseña de tu red WiFi local.
3. Presiona **Guardar** – el ESP32 intentará conectarse y la red `Nariz Digital-XX` seguirá disponible.

### 4. Acceder desde la red local

Una vez que el ESP32 se conecte a tu WiFi, desde cualquier dispositivo en la misma red abre:

```
http://aire.local
```

> Si `aire.local` no resuelve, consulta la IP asignada al ESP32 por tu router y accede directamente.

### 5. Registrar las coordenadas GPS

1. Desde `http://aire.local`, abre la pestaña **Configuración**.
2. Localiza la sección del **mapa interactivo**.
3. Haz clic sobre el mapa en la ubicación exacta del sensor; las coordenadas se autocompletan.
4. Guarda la configuración.

Las coordenadas quedan almacenadas en la memoria no volátil (NVS) del ESP32 y se incluyen en cada payload MQTT.

---

## OTA (Over-The-Air)

El dispositivo consulta periódicamente (cada hora) el archivo JSON en el servidor OTA. Si detecta una versión más reciente, descarga el binario, verifica el MD5 y reinicia con el nuevo firmware.

```
┌─────────┐   HTTPS    ┌────────────────────┐
│  ESP32  │ ─────────> │ ota.riosvivos.org  │
│         │ <───────── │ movocs.json + .bin │
└─────────┘            └────────────────────┘
```

---

## Estructura del proyecto

```
air-monitor-JJR/
├── platformio.ini          # Configuración PlatformIO
├── partitions.csv          # Tabla de particiones (OTA)
├── VERSION                 # Versión actual del firmware
├── auto_upload.py          # Script post-build de subida automática
├── data/
│   └── chart.umd.js        # Chart.js para el dashboard web
├── include/
│   └── generated/
│       └── dotenv_config.h # Variables de entorno (no versionar)
└── src/
    ├── main.cpp            # Setup y loop principal
    ├── EEPROMManager.h     # Configuración persistente (NVS/Preferences)
    ├── mq135.h             # Driver del sensor MQ135 con filtrado EMA
    ├── wifimanager.h       # Gestión WiFi AP+STA sin librería externa
    ├── webs.h              # Servidor HTTP + WebSocket + portal cautivo
    ├── mqttsend.h          # Cliente MQTT y reconexión
    ├── mqtt_payload.h      # Construcción de topics y payload JSON
    ├── mqtt_handlers.h     # Manejo de mensajes MQTT entrantes
    ├── mqtt_diag_log.h     # Log de diagnóstico MQTT
    ├── mqtt_snapshot.h     # Snapshot del estado MQTT
    ├── offline_store.h     # Buffer offline en LittleFS
    ├── error_log.h         # Log de errores persistente
    ├── system_status.h     # Métricas de salud del sistema
    ├── system.h            # Reinicios programados y estado global
    ├── ota.h               # Actualizaciones OTA
    └── remote_actions.h    # Acciones remotas vía MQTT
```

---

## Dependencias

| Librería | Versión | Uso |
|---|---|---|
| `links2004/WebSockets` | ^2.4.1 | WebSocket servidor |
| `knolleary/PubSubClient` | ^2.8 | Cliente MQTT |
| `bblanchon/ArduinoJson` | ^6.21.4 | Serialización JSON |
| `ESP32Async/ESPAsyncWebServer` | 3.10.0 | Servidor HTTP asíncrono |
| `ESP32Async/AsyncTCP` | 3.4.10 | TCP asíncrono |
| `mikalhart/ESP32-OTA-Pull` | git | Actualizaciones OTA |
| LittleFS | (framework) | Sistema de archivos |
| Preferences | (framework) | Almacenamiento NVS |

---

## Licencia

Proyecto de código abierto desarrollado por y para la comunidad de **Ríos Vivos** (El Salto, Jalisco, México).
