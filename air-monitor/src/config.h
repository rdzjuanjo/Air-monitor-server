#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

// ============================================================================
// Configuración centralizada del proyecto
// Edita este archivo para ajustar los parámetros sin tocar el código fuente.
// ============================================================================

// ----------------------------------------------------------------------------
// Red / AP
// ----------------------------------------------------------------------------

/** Prefijo del SSID del punto de acceso local (se añade el último byte de MAC) */
#define CFG_NETWORK_AP_NAME_PREFIX          "Nariz Digital-"

/** Hostname mDNS del dispositivo (disponible como [hostname].local en la red local) */
#define CFG_NETWORK_MDNS_HOSTNAME           "aire"

// ----------------------------------------------------------------------------
// Telemetría MQTT
// ----------------------------------------------------------------------------

/**
 * Dirección del broker MQTT.
 * Sobreescribible vía build_flags (-D CFG_TELEMETRY_BROKER=\"...\")
 * para usar distintos entornos (ver platformio.ini).
 */
#ifndef CFG_TELEMETRY_BROKER
#define CFG_TELEMETRY_BROKER                "mqtt.sensio.mx"
#endif

/** Puerto del broker MQTT */
#define CFG_TELEMETRY_PORT                  1883

/** Tamaño máximo del buffer MQTT en bytes */
#define CFG_TELEMETRY_MQTT_PACKET_SIZE      768

/**
 * Prefijo base del topic de telemetría (se añade el device_id al final).
 * Sobreescribible vía build_flags (-D CFG_TELEMETRY_TOPIC_BASE=\"...\").
 */
#ifndef CFG_TELEMETRY_TOPIC_BASE
#define CFG_TELEMETRY_TOPIC_BASE            "monitoreo/"
#endif

// ----------------------------------------------------------------------------
// Intervalos de muestreo y publicación
// ----------------------------------------------------------------------------

/** Intervalo entre publicaciones MQTT (ms) – por defecto 20 min */
#define CFG_SAMPLING_PUBLISH_INTERVAL_MS         1200000UL

/** Intervalo del reporte de estado por serial cuando hay STA (ms) */
#define CFG_SAMPLING_STATUS_INTERVAL_MS          30000UL

/** Intervalo de reintento de reconexión WiFi STA (ms) */
#define CFG_SAMPLING_RECONNECT_INTERVAL_MS       30000UL

/** Intervalo del reporte offline por serial cuando no hay STA (ms) */
#define CFG_SAMPLING_OFFLINE_STATUS_INTERVAL_MS  30000UL

// ----------------------------------------------------------------------------
// OTA (Over-The-Air)
// ----------------------------------------------------------------------------

/**
 * URL del JSON de control de versiones OTA.
 * Sobreescribible vía build_flags (-D CFG_OTA_JSON_URL=\"...\").
 */
#ifndef CFG_OTA_JSON_URL
#define CFG_OTA_JSON_URL                    "https://ota.sensio.mx/narizdigital.json"
#endif

/** Intervalo entre chequeos OTA (ms) – por defecto 1 hora */
#define CFG_OTA_CHECK_INTERVAL_MS           3600000UL

// ----------------------------------------------------------------------------
// Versión del firmware
// Mantener sincronizado con el archivo VERSION en la raíz del proyecto.
// ----------------------------------------------------------------------------

/** Versión del firmware (sincronizar con el archivo VERSION) */
#define CFG_FIRMWARE_VERSION                "1.0.0"

// ----------------------------------------------------------------------------
// NTP
// ----------------------------------------------------------------------------

/** Servidor NTP para sincronización de tiempo */
#define CFG_NTP_SERVER                      "pool.ntp.org"

/** Offset GMT en segundos (UTC-6 = México) */
#define CFG_NTP_GMT_OFFSET_SEC              (-6L * 3600)

/** Offset de horario de verano en segundos (0 = sin cambio) */
#define CFG_NTP_DAYLIGHT_OFFSET_SEC         0

#endif // PROJECT_CONFIG_H
