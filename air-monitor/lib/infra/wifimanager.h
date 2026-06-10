// [REUSABLE] Sin dependencias del sensor MQ135. Valores de red vienen de config.h.
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

/**
 * @file wifimanager.h
 * @brief Gestión WiFi sin librerías externas
 *
 * Estrategia:
 *  - Modo WIFI_AP_STA siempre activo:
 *      AP  → red local "AirQ-XXXXXX" / 192.168.4.1  (siempre disponible)
 *      STA → intenta conectar al SSID guardado en NVS
 *  - Si no hay credenciales guardadas → sólo AP (modo configuración)
 *  - Si pierde la conexión STA → reintenta cada WIFI_RETRY_INTERVAL ms
 *  - NO usa WiFiManager, NO bloquea el loop
 */

#include <WiFi.h>
#include "NvsConfig.h"
#include "system_status.h"
#include "config.h"

// ============================================================================
// CONFIGURACIÓN
// ============================================================================

#define AP_SSID_PREFIX       CFG_NETWORK_AP_NAME_PREFIX
#define AP_PASSWORD          ""          // Sin contraseña en el AP
#define AP_CHANNEL           6
#define AP_IP                "192.168.4.1"

#define WIFI_RETRY_INTERVAL  CFG_SAMPLING_RECONNECT_INTERVAL_MS  // Reintentar STA cada 30 s
#define WIFI_CONNECT_TIMEOUT (15UL * 1000)   // Timeout por intento de conexión

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

bool wifi_connected  = false;   // true cuando STA está conectado

static char    apSsid[32];
static unsigned long lastWifiRetry    = 0;
static unsigned long staConnectStart  = 0;
static bool          staConnecting    = false;

static const char *resolveApPrefix() {
  const char *prefix = getApNamePrefix();
  return (prefix && prefix[0] != '\0') ? prefix : AP_SSID_PREFIX;
}

// ============================================================================
// DECLARACIONES
// ============================================================================

void     wifiSetup();
void     wifiLoop();          // Llamar en loop(), no bloquea
bool     wifiIsConnected();
String   wifiGetApIP();
String   wifiGetStaIP();
void     wifiStartSTAConnect();

// ============================================================================
// IMPLEMENTACIÓN
// ============================================================================

/**
 * Genera el SSID del AP a partir de la MAC y arranca ambos interfaces.
 * Debe llamarse una sola vez desde setup().
 */
void wifiSetup() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);   // No guardar credenciales en flash de WiFi

  // Calcular nombre del AP
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apSsid, sizeof(apSsid), "%s%02X",
           resolveApPrefix(), mac[5]);

  // Iniciar AP
  IPAddress apIP(192, 168, 4, 1);
  IPAddress apGW(192, 168, 4, 1);
  IPAddress apSN(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apGW, apSN);

  if (AP_PASSWORD[0] == '\0') {
    WiFi.softAP(apSsid);
  } else {
    WiFi.softAP(apSsid, AP_PASSWORD, AP_CHANNEL);
  }

  Serial.printf("\n╔══════════════════════════════════════╗\n");
  Serial.printf("║  AP iniciado: %-22s║\n", apSsid);
  Serial.printf("║  IP  AP     : %-22s║\n", AP_IP);
  Serial.printf("╚══════════════════════════════════════╝\n");

  // Si hay credenciales guardadas, intentar conectar al STA
  if (nvsGetWifiSsid()[0] != '\0') {
    wifiStartSTAConnect();
  } else {
    Serial.println("Sin credenciales WiFi – sólo modo AP.");
    Serial.println("Configura el WiFi en http://192.168.4.1");
  }
}

/** Inicia un intento de conexión al SSID guardado (no bloquea). */
void wifiStartSTAConnect() {
  if (nvsGetWifiSsid()[0] == '\0') return;

  // Cancelar cualquier intento previo antes de iniciar uno nuevo.
  // Sin esto, en modo AP_STA el SDK puede ignorar el WiFi.begin()
  // si ya tiene un intento en curso o una conexión previa colgada.
  WiFi.disconnect(true);  // true = borra credenciales del SDK (no del NVS nuestro)
  delay(100);             // Pausa para que el driver procese el disconnect

  Serial.printf("Conectando a SSID: %s …\n", nvsGetWifiSsid());
  WiFi.begin(nvsGetWifiSsid(), nvsGetWifiPassword());

  staConnecting   = true;
  staConnectStart = millis();
  lastWifiRetry   = millis();  // Evita reintento automático inmediato
  wifi_connected  = false;
}

/**
 * Debe llamarse en cada iteración de loop().
 * Gestiona: detección de conexión/desconexión STA, timeouts, reintentos.
 */
void wifiLoop() {
  unsigned long now = millis();
  wl_status_t   sta = WiFi.status();

  // ── Esperando resultado de un intento de conexión ────────────────────────
  if (staConnecting) {
    if (sta == WL_CONNECTED) {
      staConnecting  = false;
      wifi_connected = true;

      Serial.printf("✓ WiFi STA conectado → IP: %s\n",
                    WiFi.localIP().toString().c_str());

      sysStatusSetWifi(true, WiFi.RSSI(), WiFi.localIP().toString().c_str());
      lastWifiRetry = now;
    } else if (now - staConnectStart >= WIFI_CONNECT_TIMEOUT) {
      // Timeout: limpiar estado del SDK y reintentar después
      staConnecting = false;
      WiFi.disconnect(true);  // true = limpia estado interno del SDK
      Serial.println("✗ Timeout conectando WiFi STA – reintentará en 30 s");
      lastWifiRetry = now;
    }
    return;  // No hacer más por ahora
  }

  // ── Monitoreo de conexión activa ─────────────────────────────────────────
  if (wifi_connected) {
    if (sta != WL_CONNECTED) {
      wifi_connected = false;
      WiFi.disconnect(true);  // Limpiar estado SDK para que el reintento sea limpio
      Serial.println("✗ WiFi STA desconectado – reintentará en 30 s");
      sysStatusSetWifi(false, 0, "");
      lastWifiRetry = now;
    } else {
      // Conexión activa → actualizar RSSI
      sysStatusSetWifi(true, WiFi.RSSI(), WiFi.localIP().toString().c_str());
    }
    return;
  }

  // ── Sin conexión: reintentar periódicamente ──────────────────────────────
  if (nvsGetWifiSsid()[0] != '\0' &&
      (now - lastWifiRetry >= WIFI_RETRY_INTERVAL)) {
    lastWifiRetry = now;
    wifiStartSTAConnect();
  }
}

bool   wifiIsConnected() { return wifi_connected; }
String wifiGetApIP()     { return WiFi.softAPIP().toString(); }
String wifiGetStaIP()    { return WiFi.localIP().toString(); }

#endif // WIFI_MANAGER_H