// [REUSABLE] Configuración base + gestión genérica de persistencia con Preferences (NVS).
// Contiene BaseConfig (campos comunes a cualquier sistema ESP32) y el sistema de hooks
// para que el proyecto conecte su struct Config (que hereda BaseConfig) vía nvsInit().
//
// Uso básico (en mq_NVS.h, ANTES de loadConfig()):
//   nvsInit(&config, sizeof(config), "mi_ns", 1, CFG_AP_PREFIX);
//   nvsRegisterLoadHook([](Preferences& p)  { config.campo = p.getFloat("campo", 0); });
//   nvsRegisterSaveHook([](Preferences& p)  { p.putFloat("campo", config.campo); });
//   nvsRegisterResetHook([]()               { config.campo = 0; config.lat = 20.5f; });
//   nvsRegisterPrintHook([]()               { Serial.printf("Campo: %.2f\n", config.campo); });
#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <Preferences.h>
#include <Arduino.h>
#include <WiFi.h>

// ============================================================================
// BASE CONFIG — campos comunes a cualquier sistema de monitoreo ESP32.
// Para extender: definir struct Config : public BaseConfig en mq_NVS.h.
// ============================================================================

struct BaseConfig {
  char  wifi_ssid[64];
  char  wifi_password[64];
  char  ap_name_prefix[16];
  char  device_id[32];
  float latitude;
  float longitude;
  bool  configured;
};

// ============================================================================
// HOOK TYPES
// ============================================================================

typedef void (*NvsLoadHookFn) (Preferences&);  // carga campos extra desde NVS
typedef void (*NvsSaveHookFn) (Preferences&);  // guarda campos extra en NVS
typedef void (*NvsResetHookFn)();              // inicializa campos extra a defaults
typedef void (*NvsPrintHookFn)();              // imprime campos extra en Serial

// ============================================================================
// ESTADO INTERNO
// ============================================================================

static BaseConfig*    sNvsCfg       = nullptr;
static size_t         sNvsCfgSize   = sizeof(BaseConfig);
static const char*    sNvsNamespace = nullptr;
static uint8_t        sNvsVersion   = 0;
static const char*    sNvsApDefault = "Monitor-";
static Preferences    sNvsPrefs;

static NvsLoadHookFn  sNvsLoadHook  = nullptr;
static NvsSaveHookFn  sNvsSaveHook  = nullptr;
static NvsResetHookFn sNvsResetHook = nullptr;
static NvsPrintHookFn sNvsPrintHook = nullptr;

// ============================================================================
// INIT Y REGISTRO
// Llamar nvsInit() y nvsRegister*() ANTES de loadConfig().
// ============================================================================

inline void nvsInit(BaseConfig* cfg, size_t cfgSize, const char* ns, uint8_t version,
                    const char* defaultApPrefix = "Monitor-") {
  sNvsCfg       = cfg;
  sNvsCfgSize   = cfgSize;
  sNvsNamespace = ns;
  sNvsVersion   = version;
  sNvsApDefault = defaultApPrefix;
}

inline void nvsRegisterLoadHook (NvsLoadHookFn  fn) { sNvsLoadHook  = fn; }
inline void nvsRegisterSaveHook (NvsSaveHookFn  fn) { sNvsSaveHook  = fn; }
inline void nvsRegisterResetHook(NvsResetHookFn fn) { sNvsResetHook = fn; }
inline void nvsRegisterPrintHook(NvsPrintHookFn fn) { sNvsPrintHook = fn; }

// ============================================================================
// HELPERS DE CONFIG
// ============================================================================

inline const char* getApNamePrefix() {
  if (!sNvsCfg || !sNvsCfg->ap_name_prefix[0]) return sNvsApDefault;
  return sNvsCfg->ap_name_prefix;
}

// Accessors para que lib/infra/ pueda leer config sin depender del struct Config del proyecto.
inline const char* nvsGetDeviceId()     { return sNvsCfg ? sNvsCfg->device_id    : ""; }
inline float       nvsGetLatitude()     { return sNvsCfg ? sNvsCfg->latitude      : 0.0f; }
inline float       nvsGetLongitude()    { return sNvsCfg ? sNvsCfg->longitude     : 0.0f; }
inline const char* nvsGetWifiSsid()     { return sNvsCfg ? sNvsCfg->wifi_ssid     : ""; }
inline const char* nvsGetWifiPassword() { return sNvsCfg ? sNvsCfg->wifi_password : ""; }

// Setters para que lib/infra/ pueda escribir campos base sin depender del struct Config.
inline void nvsSetLatitude (float lat)        { if (sNvsCfg) sNvsCfg->latitude  = lat; }
inline void nvsSetLongitude(float lon)        { if (sNvsCfg) sNvsCfg->longitude = lon; }
inline void nvsSetDeviceId (const char* id)   {
  if (sNvsCfg && id) strlcpy(sNvsCfg->device_id, id, sizeof(BaseConfig::device_id));
}

inline void setApNamePrefix(const char* prefix, bool persist = true);  // forward decl
inline void setWiFiCredentials(const char* ssid, const char* password, bool persist = true);
inline void clearWiFiCredentials(bool persist = true);

// ============================================================================
// CICLO DE VIDA — orden de definición importa (callee antes que caller)
// ============================================================================

inline void initEEPROM() {
  Serial.println("Inicializando sistema de persistencia (Preferences/NVS)");
}

inline void printConfig() {
  if (!sNvsCfg) return;
  Serial.println("=== CONFIGURACIÓN ACTUAL ===");
  Serial.printf("Device ID   : %s\n", sNvsCfg->device_id);
  Serial.printf("AP Prefix   : %s\n", getApNamePrefix());
  Serial.printf("WiFi SSID   : %s\n", sNvsCfg->wifi_ssid[0]     ? sNvsCfg->wifi_ssid : "(vacío)");
  Serial.printf("WiFi Pass   : %s\n", sNvsCfg->wifi_password[0]  ? "***"               : "(vacío)");
  Serial.printf("Ubicación   : %.6f, %.6f\n", sNvsCfg->latitude, sNvsCfg->longitude);
  if (sNvsPrintHook) sNvsPrintHook();
  Serial.println("============================");
}

inline void saveConfig() {
  if (!sNvsCfg) return;
  sNvsPrefs.begin(sNvsNamespace, false);  // read-write
  sNvsPrefs.putUShort("version",    sNvsVersion);
  sNvsPrefs.putString("device_id",  sNvsCfg->device_id);
  sNvsPrefs.putString("wifi_ssid",  sNvsCfg->wifi_ssid);
  sNvsPrefs.putString("wifi_pass",  sNvsCfg->wifi_password);
  sNvsPrefs.putString("ap_prefix",  sNvsCfg->ap_name_prefix);
  sNvsPrefs.putFloat ("latitude",   sNvsCfg->latitude);
  sNvsPrefs.putFloat ("longitude",  sNvsCfg->longitude);
  sNvsPrefs.putBool  ("configured", sNvsCfg->configured);
  if (sNvsSaveHook) sNvsSaveHook(sNvsPrefs);
  sNvsPrefs.end();
  Serial.println("Configuración guardada en NVS");
}

inline void initDefaultConfig() {
  if (!sNvsCfg) return;
  memset(sNvsCfg, 0, sNvsCfgSize);

  sNvsCfg->latitude   = 0.0f;
  sNvsCfg->longitude  = 0.0f;
  sNvsCfg->configured = true;
  snprintf(sNvsCfg->ap_name_prefix, sizeof(sNvsCfg->ap_name_prefix), "%s", sNvsApDefault);
  sNvsCfg->wifi_ssid[0]     = '\0';
  sNvsCfg->wifi_password[0] = '\0';

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(sNvsCfg->device_id, sizeof(sNvsCfg->device_id),
           "%02X%02X%02X", mac[3], mac[4], mac[5]);

  // El proyecto inicializa sus propios campos (y puede sobreescribir lat/lon)
  if (sNvsResetHook) sNvsResetHook();

  Serial.println("Configuración inicializada con valores por defecto");
}

inline void loadConfig() {
  if (!sNvsCfg) return;
  sNvsPrefs.begin(sNvsNamespace, true);  // read-only
  uint16_t savedVersion = sNvsPrefs.getUShort("version", 0);

  if (savedVersion == sNvsVersion) {
    sNvsPrefs.getString("device_id", sNvsCfg->device_id,       sizeof(sNvsCfg->device_id));
    sNvsPrefs.getString("wifi_ssid", sNvsCfg->wifi_ssid,       sizeof(sNvsCfg->wifi_ssid));
    sNvsPrefs.getString("wifi_pass", sNvsCfg->wifi_password,    sizeof(sNvsCfg->wifi_password));
    sNvsPrefs.getString("ap_prefix", sNvsCfg->ap_name_prefix,   sizeof(sNvsCfg->ap_name_prefix));
    sNvsCfg->latitude   = sNvsPrefs.getFloat("latitude",   0.0f);
    sNvsCfg->longitude  = sNvsPrefs.getFloat("longitude",  0.0f);
    sNvsCfg->configured = sNvsPrefs.getBool ("configured", false);
    if (sNvsLoadHook) sNvsLoadHook(sNvsPrefs);
    sNvsPrefs.end();

    Serial.println("Configuración cargada desde NVS");
    printConfig();
  } else {
    sNvsPrefs.end();
    if (savedVersion > 0 && savedVersion != sNvsVersion) {
      Serial.printf("Versión de config antigua (%d), migrando a v%d\n",
                    savedVersion, (int)sNvsVersion);
    } else {
      Serial.println("No se encontró configuración válida en NVS");
    }
    initDefaultConfig();
    saveConfig();
  }
}

inline void resetConfig() {
  Serial.println("Reseteando configuración a valores por defecto");
  sNvsPrefs.begin(sNvsNamespace, false);
  sNvsPrefs.clear();
  sNvsPrefs.end();

  if (sNvsCfg) memset(sNvsCfg, 0, sNvsCfgSize);
  initDefaultConfig();
  saveConfig();
  Serial.println("✓ Configuración reseteada completamente");
}

inline bool validateConfig() {
  if (!sNvsCfg) return false;
  if (sNvsCfg->latitude  < -90  || sNvsCfg->latitude  > 90)  { Serial.println("✗ Latitud fuera de rango");  return false; }
  if (sNvsCfg->longitude < -180 || sNvsCfg->longitude > 180) { Serial.println("✗ Longitud fuera de rango"); return false; }
  return true;
}

// ============================================================================
// HELPERS DE CONFIG (definiciones — después de saveConfig)
// ============================================================================

inline void setWiFiCredentials(const char* ssid, const char* password, bool persist) {
  if (!sNvsCfg) return;
  if (ssid && ssid[0] != '\0') {
    strncpy(sNvsCfg->wifi_ssid, ssid, sizeof(sNvsCfg->wifi_ssid) - 1);
    sNvsCfg->wifi_ssid[sizeof(sNvsCfg->wifi_ssid) - 1] = '\0';
  }
  if (password) {
    strncpy(sNvsCfg->wifi_password, password, sizeof(sNvsCfg->wifi_password) - 1);
    sNvsCfg->wifi_password[sizeof(sNvsCfg->wifi_password) - 1] = '\0';
  }
  if (persist) saveConfig();
}

inline void clearWiFiCredentials(bool persist) {
  if (!sNvsCfg) return;
  sNvsCfg->wifi_ssid[0]     = '\0';
  sNvsCfg->wifi_password[0] = '\0';
  if (persist) saveConfig();
}

inline void setApNamePrefix(const char* prefix, bool persist) {
  if (!sNvsCfg) return;
  if (!prefix || prefix[0] == '\0') {
    snprintf(sNvsCfg->ap_name_prefix, sizeof(sNvsCfg->ap_name_prefix), "%s", sNvsApDefault);
  } else {
    strncpy(sNvsCfg->ap_name_prefix, prefix, sizeof(sNvsCfg->ap_name_prefix) - 1);
    sNvsCfg->ap_name_prefix[sizeof(sNvsCfg->ap_name_prefix) - 1] = '\0';
  }
  if (persist) saveConfig();
}

#endif // NVS_CONFIG_H
